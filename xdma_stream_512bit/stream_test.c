/*

Prerequisites:
 - Vivado XDMA 512-Bit AXI4-Stream Project:
   github.com/mwrnd/innova2_experiments/tree/main/xdma_stream_512bit
 - XDMA Drivers from github.com/xilinx/dma_ip_drivers
   Install Instructions at github.com/mwrnd/innova2_flex_xcku15p_notes


Compile with:

  gcc -Wall stream_test.c -o stream_test -lm

Run with:

  sudo ./stream_test

*/

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>


// The PCIe to AXI Translation Offset for the PCIe to AXI-Lite Interface
#define XDMA_PCIe_to_AXI_Translation_Offset 0x40000000

// The AXI Data Width is 256-Bit=32-Byte
// There is a 32*256-Bit=1024-byte FIFO for the H2C stream
//      and a 16*256-Bit= 512-byte FIFO for the C2H stream
// The design multiplies pairs of floating point values; 256 FP --> 128 FP
// As each buffer can fill before it needs to be emptied,
// can transfer twice the FIFO depth
#define H2C_FIFO_DEPTH_BYTES 2048
#define C2H_FIFO_DEPTH_BYTES 1024


// Each single-precision floating point value takes up 4 bytes
#define H2C_FLOAT_COUNT (H2C_FIFO_DEPTH_BYTES / 4)
#define C2H_FLOAT_COUNT (C2H_FIFO_DEPTH_BYTES / 4)

// Number of times that H2C or C2H transfers should be tried
// Note that by default XDMA Drivers have a 10 second timeout
#define MAX_ATTEMPTS 5




// Global struct for XDMA Device files
struct _XDMA {
	char *userfilename;
	char *h2cfilename;
	char *c2hfilename;
	int userfd;
	int h2cfd;
	int c2hfd;
} xdma;




ssize_t read_axilite_word(uint64_t address, uint32_t *read_word)
{
	ssize_t rc = 0;

	rc = pread(xdma.userfd, read_word, 4,
		(address - XDMA_PCIe_to_AXI_Translation_Offset));
	if (rc < 0) {
		fprintf(stderr, "%s, read data @ 0x%lX failed, %ld.\n",
			xdma.userfilename, address, rc);
		perror("File Read");
		return -EIO;
	}
	if (rc != 4) {
		fprintf(stderr, "%s, read underflow @ 0x%lX, read %ld/4.\n",
			xdma.userfilename, address, rc);
		perror("Read Underflow");
	}
	
	return rc;
}




ssize_t write_axilite_word(uint64_t address, uint32_t write_word)
{
	ssize_t rc;

	rc = pwrite(xdma.userfd, &write_word, 4,
		(address - XDMA_PCIe_to_AXI_Translation_Offset));
	if (rc < 0) {
		fprintf(stderr, "%s, write data @ 0x%lX failed, %ld.\n",
			xdma.userfilename, address, rc);
		perror("File Write");
		return -EIO;
	}
	if (rc != 4) {
		fprintf(stderr, "%s, write underflow @ 0x%lX, %ld/4.\n",
			xdma.userfilename, address, rc);
		perror("Write Underflow");
	}

	return rc;
}




ssize_t read_from_axi(uint64_t address, size_t bytes, void *buffer)
{
	ssize_t rc;

	rc = pread(xdma.c2hfd, buffer, bytes, address);
	fsync(xdma.c2hfd);

	for (int i = 0; i < MAX_ATTEMPTS; i++) {
		if (rc < 0) {
			fprintf(stderr, "%s, read data @ 0x%lX failed, %ld.\n",
				xdma.c2hfilename, address, rc);
			perror("File Read");
			//return -EIO;
		}
		if (rc != bytes) {
			fprintf(stderr, "%s, read underflow @ 0x%lX, %ld/%ld.\n",
				xdma.c2hfilename, address, rc, bytes);
			perror("Read Underflow");
		}
		if (rc == bytes) { break; }
	}

	return rc;
}




ssize_t write_to_axi(uint64_t address, size_t bytes, void *buffer)
{
	ssize_t rc;

	rc = pwrite(xdma.h2cfd, buffer, bytes, address);
	fsync(xdma.h2cfd);

	for (int i = 0; i < MAX_ATTEMPTS; i++) {
		if (rc < 0) {
			fprintf(stderr, "%s, write data @ 0x%lX failed, %ld.\n",
				xdma.h2cfilename, address, rc);
			perror("File Write");
			//return -EIO;
		}
		if (rc != bytes) {
			fprintf(stderr, "%s, write underflow @ 0x%lX, %ld/%ld.\n",
				xdma.h2cfilename, address, rc, bytes);
			perror("Write Underflow");
		}
		if (rc == bytes) { break; }
	}

	return rc;
}




// which_stats is the OR of:
//	0b00001 == 0x01 --> Print contents of AXILite GPIO Block
//	0b00010 == 0x02 --> Print status of reset signals
//	0b00100 == 0x04 --> Print clock counts and whether clock is locked
//	0b01000 == 0x08 --> Print FPGA design version
//	0b10000 == 0x10 --> Print run status
// which_stats == 0b11111 == 0x1F prints all
void print_status(uint32_t which_stats)
{
	uint32_t data_word = 0;
	uint64_t address = 0x40012000;

	read_axilite_word(address, &data_word);

	if (((which_stats & 0x00000001) >> 0)) {
		printf("AXILite Address 0x%08lX has value: 0x%08X\n",
			address, data_word);
	}
	int axi_aresetn     = (data_word & 0x00000001) >> 0;
	int stream_rstn_125 = (data_word & 0x00000002) >> 1;
	int aux_reset_in    = (data_word & 0x00000004) >> 2;
	int stream_rstn_250 = (data_word & 0x00000008) >> 3;
	int clk_125_locked  = (data_word & 0x00000010) >> 4;

	if (((which_stats & 0x00000002) >> 1)) {
		printf("  aux_reset_in = %d, stream_rstn_125 = %d, ",
			aux_reset_in, stream_rstn_125);
		printf("stream_rstn_250 = %d, axi_resetn = %d\n",
			stream_rstn_250, axi_aresetn);
	}

	int clk_125_count  = (data_word & 0xFFF00000) >> 24;
	int axi_aclk_count = (data_word & 0x000FFF00) >> 8;

	if (((which_stats & 0x00000004) >> 2)) {
		printf("  axi_aclk_count = 0x%03X, clk_125_count = 0x%03X, ",
			axi_aclk_count, clk_125_count);
		printf("clk_125_locked = %d\n", clk_125_locked);
	}

	if (((which_stats & 0x00000008) >> 3)) {
		int version = (data_word & 0x000000E0) >> 5;
		printf("  Version = %d\n", version);
	}

	if (((which_stats & 0x00000010) >> 4)) {
		if (stream_rstn_125 && axi_aresetn &&
				clk_125_locked && stream_rstn_250) {
			printf("  |--> Design running normally.");
		} else if (!stream_rstn_125 && !stream_rstn_250 &&
				axi_aresetn && clk_125_locked) {
			printf("  |--> Stream blocks in RESET.");
		} else if (!clk_125_locked) {
			printf("  |--> Design stopped, clk_125 not locked.");
		} else {
			printf("  |--> Design in ERROR state.");
		}
	}

	printf("\n");
}





void reset_stream_blocks(void)
{
	printf("\nResetting AXI4-Stream Blocks ...\n");
	print_status(0x12);

	// Set ext_reset_in, GPIO0 Bit0, to 0 as reset is Active-Low
	write_axilite_word(0x40011000, 0x00000000);

	print_status(0x12);
	// give the reset time to take effect
	sleep(1);

	// Set ext_reset_in, GPIO0 Bit0, back to 1
	write_axilite_word(0x40011000, 0x00000001);

	print_status(0x12);
	printf("\n");
}




void print_and_test_results(uint32_t i, float *fh2c, float *fc2h)
{
	uint32_t j = floor((i / 2));

	if (i & 0x00000001) {
		printf("Index %d is incompatible, should be even.\n", i);
		return;
	}

	if (i > (H2C_FLOAT_COUNT-1)) {
		printf("Index %d is out of range.\n", i);
		return;
	}

	printf("%d, %d, f_h2c[%d]*[%d] = f_c2h[%d]  =  %f*%f = %f",
		i, j, i, (i+1), j, fh2c[i], fh2c[(i+1)], fc2h[j]);
	if (fabs((fh2c[i] * fh2c[(i+1)]) - fc2h[j]) > 0.01) {
		float expected = (fh2c[i] * fh2c[(i+1)]);
		printf(" -- ERROR, was expecting %f", expected);
		// search for the correct value
		int k = 0;
		for (k = 0; k < (C2H_FLOAT_COUNT-1); k++) {
			if (fabs(expected - fc2h[k]) < 0.01) {
				printf(" which is at index %d", k);
			}
		}
		printf("\n");
		// print neighbouring array values
		printf("    C2H: ");
		if (j>1) {
			printf("[%d]=%f ", (j-2), fc2h[(j-2)]);
		}
		if (j>0) {
			printf("[%d]=%f ", (j-1), fc2h[(j-1)]);
		}
		printf("[%d]=%f ", j, fc2h[j]);
		if (j<(C2H_FLOAT_COUNT-1)) {
			printf("[%d]=%f ", (j+1), fc2h[(j+1)]);
		}
		if (j<(C2H_FLOAT_COUNT-2)) {
			printf("[%d]=%f", (j+2), fc2h[(j+2)]);
		}
		printf("\n    H2C: ");
		if (i>1) {
			printf("[%d]=%f ", (i-2), fh2c[(i-2)]);
		}
		if (i>0) {
			printf("[%d]=%f ", (i-1), fh2c[(i-1)]);
		}
		printf("[%d]=%f ", i, fh2c[i]);
		printf("[%d]=%f ", (i+1), fh2c[(i+1)]);
		if (i<(H2C_FLOAT_COUNT-3)) {
			printf("[%d]=%f", (i+2), fh2c[(i+2)]);
		}
		printf("\n");
	}
	printf("\n");
}




int main(int argc, char **argv)
{
	float f_h2c[H2C_FLOAT_COUNT];
	float f_c2h[C2H_FLOAT_COUNT];
	struct timespec ts_start, ts_end;
	float bandwidth = 0;
	ssize_t rc;


	printf("AXI4-Stream 512-Bit Wide Demo\n");
	printf("H2C_FLOAT_COUNT = %d, C2H_FLOAT_COUNT = %d\n",
		H2C_FLOAT_COUNT, C2H_FLOAT_COUNT);


	// Open M_AXI_LITE Device as Read-Write
	xdma.userfilename = "/dev/xdma0_user";

	xdma.userfd = open(xdma.userfilename, O_RDWR);

	if (xdma.userfd < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n",
			xdma.userfilename, xdma.userfd);
		perror("File Open");
		exit(EXIT_FAILURE);
	}


	// Open M_AXIS_H2C Host-to-Card Device as Write-Only
	xdma.h2cfilename = "/dev/xdma0_h2c_0";

	xdma.h2cfd = open(xdma.h2cfilename, O_WRONLY);

	if (xdma.h2cfd < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n",
			xdma.h2cfilename, xdma.h2cfd);
		perror("File Open");
		exit(EXIT_FAILURE);
	}


	// Open M_AXIS_C2H Card-to-Host Device as Read-Only
	xdma.c2hfilename = "/dev/xdma0_c2h_0";

	xdma.c2hfd = open(xdma.c2hfilename, O_RDONLY);

	if (xdma.c2hfd < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n",
			xdma.c2hfilename, xdma.c2hfd);
		perror("File Open");
		exit(EXIT_FAILURE);
	}

	printf("\n");




	print_status(0x1F);
	reset_stream_blocks();




	// fill the H2C buffer with floating-point values
	for (int i = 0; i < H2C_FLOAT_COUNT ; i++) {
		f_h2c[i] = (1.01 * (i + 1));
	}




	// Test a Single Transfer
	printf("Test a single transfer; write to stream then read from it:\n");

	// start timing of the transfer and data processing
	rc = clock_gettime(CLOCK_MONOTONIC, &ts_start);


	// send the H2C float values into the AXI4-Stream
	rc = write_to_axi(0, H2C_FIFO_DEPTH_BYTES, f_h2c);
	if (rc < 0) { printf("ERROR sending (H2C) to stream.\n"); }


	// receive the C2H float values from the AXI4-Stream
	rc = read_from_axi(0, C2H_FIFO_DEPTH_BYTES, f_c2h);
	if (rc < 0) { printf("ERROR receiving (C2H) from stream.\n"); }


	// end timing of the data processing
	rc = clock_gettime(CLOCK_MONOTONIC, &ts_end);
	ts_end.tv_sec = abs(ts_end.tv_sec - ts_start.tv_sec);
	ts_end.tv_nsec = abs(ts_end.tv_nsec - ts_start.tv_nsec);
	printf("Data transfer+processing took %ld.%09ld seconds.\n",
		ts_end.tv_sec, ts_end.tv_nsec);
	bandwidth = (float)((((double)H2C_FIFO_DEPTH_BYTES) /
		(((double)((ts_end.tv_nsec))) / 1000000000.0)) /
		((double)(1024*1024)));
	printf("Bandwidth is approximately %f MB/s for %d floats.\n\n",
		bandwidth, H2C_FLOAT_COUNT);




	// print the debugging info for some test values
	print_and_test_results(0, f_h2c, f_c2h);
	print_and_test_results(126, f_h2c, f_c2h);
	print_and_test_results(254, f_h2c, f_c2h);

	// seed psuedo-random number generator with arbitrary value, an address
	srandom((unsigned int)((long int)xdma.h2cfilename));

	uint32_t index = 0;
	index = (((uint32_t)rand()) & (H2C_FIFO_DEPTH_BYTES-1)) & 0x000000FE;
	print_and_test_results(index, f_h2c, f_c2h);
	index = (((uint32_t)rand()) & (H2C_FIFO_DEPTH_BYTES-1)) & 0x000000FE;
	print_and_test_results(index, f_h2c, f_c2h);


	// check results
	printf("\nChecking Results...\n");
	uint32_t j = 0;
	uint32_t errorcount = 0;
	for (int i = 0; i < (H2C_FLOAT_COUNT-1); i = i+2) {
		j = floor((i / 2));
		if (fabs((f_h2c[i] * f_h2c[(i+1)]) - f_c2h[j]) > 0.01) {
			print_and_test_results(i, f_h2c, f_c2h);
			errorcount++;
		}
	}

	printf("Found %d errors.\n", errorcount);




	// Test many consecutive transfers
	long int max_transfer_time_ns = 0;
	long int min_transfer_time_ns = 0xFFFFFFFFFFFFFFF;
	int max_time_index = 0;
	int min_time_index = 0;
	uint64_t transfer_time_ns_sum = 0;
	int k = 0;
	for (k = 0; k < 50000 ; k++) {

		// start timing of the data processing
		rc = clock_gettime(CLOCK_MONOTONIC, &ts_start);

		// send the H2C float values into the AXI4-Stream
		rc = write_to_axi(0, H2C_FIFO_DEPTH_BYTES, f_h2c);
		if (rc < 0) { printf("ERROR sending (H2C) to stream.\n"); }

		// receive the C2H float values from the AXI4-Stream
		rc = read_from_axi(0, C2H_FIFO_DEPTH_BYTES, f_c2h);
		if (rc < 0) { printf("ERROR receiving (C2H) from stream.\n"); }

		// end timing of the data processing
		rc = clock_gettime(CLOCK_MONOTONIC, &ts_end);
		ts_end.tv_nsec = abs(ts_end.tv_nsec - ts_start.tv_nsec);
		if (ts_end.tv_nsec > max_transfer_time_ns) {
			max_transfer_time_ns = ts_end.tv_nsec;
			max_time_index = k;
		}
		if (min_transfer_time_ns > ts_end.tv_nsec) {
			min_transfer_time_ns = ts_end.tv_nsec;
			min_time_index = k;
		}
		transfer_time_ns_sum += (uint64_t)(ts_end.tv_nsec);


		// check the results
		for (int i = 0; i < (H2C_FLOAT_COUNT-1); i = i+2) {
			j = floor((i / 2));
			if (fabs((f_h2c[i] * f_h2c[(i+1)]) - f_c2h[j]) > 0.01) {
				print_and_test_results(i, f_h2c, f_c2h);
				printf("Found an error at %d, exiting...\n", k);
				close(xdma.userfd);
				close(xdma.h2cfd);
				close(xdma.c2hfd);
				exit(EXIT_FAILURE);
			}
		}

	}


	bandwidth = (float)((((double)H2C_FIFO_DEPTH_BYTES) /
		(((double)((((double)transfer_time_ns_sum) / ((double)k))))
		/ ((double)(1000000000.0)))) / ((double)(1024*1024)));
	printf("\nAfter %d transfers, min = %ld ns, max = %ld ns,",
		k, min_transfer_time_ns, max_transfer_time_ns);
	printf(" AVG BW = %f MB/s\n", bandwidth);
	bandwidth = (float)((((double)H2C_FIFO_DEPTH_BYTES) /
		(((double)(max_transfer_time_ns)) / 1000000000.0)) /
		((double)(1024*1024)));
	printf("Minimum Bandwidth was approximately %f MB/s for %d floats",
		bandwidth, H2C_FLOAT_COUNT);
	printf(" at transfer %d\n", min_time_index);
	bandwidth = (float)((((double)H2C_FIFO_DEPTH_BYTES) /
		(((double)(min_transfer_time_ns)) / 1000000000.0)) /
		((double)(1024*1024)));
	printf("Maximum Bandwidth was approximately %f MB/s for %d floats",
		bandwidth, H2C_FLOAT_COUNT);
	printf(" at transfer %d\n", max_time_index);
	printf("\n");




	close(xdma.userfd);
	close(xdma.h2cfd);
	close(xdma.c2hfd);
	exit(EXIT_SUCCESS);
}

