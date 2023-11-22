/*****************************************************************************
 * XDMA AXI4-Stream with 512-Bit H2C and 256-Bit C2H Test Software           *
 *                                                                           *
 * BSD 2-Clause License                                                      *
 *                                                                           *
 * Copyright (c) 2023 Matthew Wielgus (mwrnd.github@gmail.com)               *
 * https://github.com/mwrnd/xdma_stream_512bit                               *
 *                                                                           *
 * Redistribution and use in source and binary forms, with or without        *
 * modification, are permitted provided that the following conditions        *
 * are met:                                                                  *
 *                                                                           *
 * 1. Redistributions of source code must retain the above copyright notice, *
 *    this list of conditions and the following disclaimer.                  *
 *                                                                           *
 * 2. Redistributions in binary form must reproduce the above copyright      *
 *    notice, this list of conditions and the following disclaimer in the    *
 *    documentation and/or other materials provided with the distribution.   *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       *
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED *
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A           *
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER *
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,  *
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,       *
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR        *
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    *
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      *
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        *
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              *
 *****************************************************************************
 */

/*

Prerequisites:
 - Vivado XDMA Project with a 512-Bit Wide AXI4Stream design:
   github.com/mwrnd/xdma_stream_512bit
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
// There is a 32*256-Bit FIFO for H2C transfers and a 16*256-Bit FIFO for C2H
// The design multiplies pairs of floating point values; 256 FP --> 128 FP
#define H2C_FIFO_DEPTH_BYTES 1024
#define C2H_FIFO_DEPTH_BYTES 512

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




void print_status(void)
{
	uint32_t data_word = 0;
	uint64_t address = 0x40012000;

	read_axilite_word(address, &data_word);

	printf("AXILite Address 0x%08lX has value: 0x%08X\n",
		address, data_word);
	int axi_aresetn     = (data_word & 0x00000001) >> 0;
	int stream_rstn_125 = (data_word & 0x00000002) >> 1;
	int aux_reset_in    = (data_word & 0x00000004) >> 2;
	int stream_rstn_250 = (data_word & 0x00000008) >> 3;
	int clk_125_locked  = (data_word & 0x00000010) >> 4;

	printf("  aux_reset_in = %d, stream_rstn_125 = %d, ",
		aux_reset_in, stream_rstn_125);
	printf("stream_rstn_250 = %d, axi_resetn = %d\n",
		stream_rstn_250, axi_aresetn);

	int clk_125_count  = (data_word & 0xFFF00000) >> 24;
	int axi_aclk_count = (data_word & 0x000FFF00) >> 8;

	printf("  axi_aclk_count = 0x%03X, clk_125_count = 0x%03X, ",
		axi_aclk_count, clk_125_count);
	printf("clk_125_locked = %d\n", clk_125_locked);

	int version = (data_word & 0x000000E0) >> 5;
	printf("  Version = %d, ", version);

	if (stream_rstn_125 && axi_aresetn &&
			clk_125_locked && stream_rstn_250) {
		printf("design running normally.");
	} else if (!stream_rstn_125 && !stream_rstn_250 &&
			axi_aresetn && clk_125_locked) {
		printf("stream blocks in RESET.");
	} else if (!clk_125_locked) {
		printf("design stopped, 125MHz clock is not locked.");
	} else {
		printf("design in ERROR state.");
	}

	printf("\n");
}





void reset_stream_blocks(void)
{
	printf("\nResetting AXI4-Stream Blocks ...\n");
	print_status();

	// Set ext_reset_in, GPIO0 Bit0, to 0 as reset is Active-Low
	write_axilite_word(0x40011000, 0x00000000);

	print_status();
	// give the reset time to take effect
	sleep(1);

	// Set ext_reset_in, GPIO0 Bit0, back to 1
	write_axilite_word(0x40011000, 0x00000001);

	print_status();
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


	print_status();
	reset_stream_blocks();




	// fill the H2C buffer with floating-point values
	for (int i = 0; i < H2C_FLOAT_COUNT ; i++) {f_h2c[i] = (1.01*(i + 1));}




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
	ts_end.tv_sec = (ts_end.tv_sec - ts_start.tv_sec);
	ts_end.tv_nsec = (ts_end.tv_nsec - ts_start.tv_nsec);
	printf("Data transfer+processing took %ld.%09ld seconds.\n",
		ts_end.tv_sec, ts_end.tv_nsec);
	bandwidth = (((float)H2C_FIFO_DEPTH_BYTES) * 1024.0) /
		((float)(ts_end.tv_nsec));
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




	close(xdma.userfd);
	close(xdma.h2cfd);
	close(xdma.c2hfd);
	exit(EXIT_SUCCESS);
}

