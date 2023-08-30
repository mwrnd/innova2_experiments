/*

Prerequisites:
 - Innova2 FPGA programmed with slow_clock_test project:
   github.com/mwrnd/innova2_experiments/slow_clock_test
 - XDMA Drivers from github.com/xilinx/dma_ip_drivers
   Install Instructions at github.com/mwrnd/innova2_flex_xcku15p_notes

Compile with:

  gcc  slow_clock_test.c  -g  -Wall  -o slow_clock_test

Run with:

  sudo ./slow_clock_test  /dev/xdma0_c2h_0 /dev/xdma0_h2c_0

*/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


// Using 8 kilobyte == 2^13 = 8192 byte array. Size was defined in the
// Vivado FPGA Project Block Diagram Address Editor as the Data Range for BRAM
// On Linux, read/write can transfer at most 0x7FFFF000 (2,147,479,552) bytes
#define DATA_SIZE 8192

// The XDMA AXI aclk depends on PCIe Lane Width, AXI Data Width, and
// Maximum Link Speed settings in the XDMA IP Block
#define XDMA_CLK_MHz	250




uint32_t diff(uint32_t a, uint32_t b)
{
	// Need difference of either 16-bit or 32-bit values

	if ((a < 0xFFFF) && (b < 0xFFFF)) {
		if (b >= a) {
			return (b - a);
		} else { // b < a
			return ((0xFFFF - a) + b);
		}

	} else { // a or b > 0xFFFF
		if (b >= a) {
			return (b - a);
		} else { // b < a
			return ((0xFFFFFFFF - a) + b);
		}
	}
}




int estimate_clock_MHz(uint8_t delay,
			 int xdma_fd_read,
			 char *xdma_c2h_name,
			 uint64_t axi_gpio_addr,
			 int flg_gpio_type)
{
	uint8_t read_data[16];
	uint32_t val = 0;
	uint32_t val1 = 0;
	uint32_t val2 = 0;
	uint32_t val3 = 0;
	uint32_t val4 = 0;
	uint32_t diff_clkA;
	uint32_t diff_clkB;
	ssize_t rc;
	double clk_ratio = 0;


	if (axi_gpio_addr == 0) { return -1; }


	// Read 4 bytes = 1 word using pread which combines lseek and read
	rc = pread(xdma_fd_read, read_data, 16, axi_gpio_addr);

	if (rc < 0) {

		fprintf(stderr, "%s, read data @ 0x%lX failed, %ld.\n",
			xdma_c2h_name, axi_gpio_addr, rc);
		perror("File Read");
		return -EIO;
	}


	if (flg_gpio_type == 1) {

		memcpy(&val, &read_data[0], 4);

		val1 = (val & 0x0000FFFF) >> 0;
		val2 = (val & 0xFFFF0000) >> 16;

		printf("\nt=0 clock counter values at 0x%lX : 0x%04X 0x%04X\n",
			axi_gpio_addr, val1, val2);

	} else {

		memcpy(&val1, &read_data[0], 4);
		memcpy(&val2, &read_data[8], 4);

		printf("\nt=0 clock counter values at 0x%lX : 0x%08X 0x%08X\n",
			axi_gpio_addr, val1, val2);
	}



	sleep(delay);



	rc = pread(xdma_fd_read, read_data, 16, axi_gpio_addr);

	if (rc < 0) {

		fprintf(stderr, "%s, read data @ 0x%lX failed, %ld.\n",
			xdma_c2h_name, axi_gpio_addr, rc);
		perror("File Read");
		return -EIO;
	}


	if (flg_gpio_type == 1) {

		memcpy(&val, &read_data[0], 4);

		val3 = (val & 0x0000FFFF) >> 0;
		val4 = (val & 0xFFFF0000) >> 16;

		printf("t=%d clock counter values at 0x%lX : 0x%04X 0x%04X\n",
			delay, axi_gpio_addr, val3, val4);

	} else {

		memcpy(&val3, &read_data[0], 4);
		memcpy(&val4, &read_data[8], 4);

		printf("t=%d clock counter values at 0x%lX : 0x%08X 0x%08X\n",
			delay, axi_gpio_addr, val3, val4);
	}


	diff_clkA = diff(val1, val3);
	diff_clkB = diff(val2, val4);
	clk_ratio =(((double)diff_clkB)/((double)diff_clkA));
	
	printf("    Clock at 0x%08lX has ratio = %lf, estimate %lf MHz",
		axi_gpio_addr, clk_ratio, (clk_ratio * XDMA_CLK_MHz));

	if ((clk_ratio * XDMA_CLK_MHz) < 1) {
		printf(" = % 9.0lf Hz\n",
			(clk_ratio * (XDMA_CLK_MHz * 1000000)));
	} else {
		printf("\n");
	}

	return 0;
}






int main(int argc, char **argv)
{
	uint64_t axi_bram_addr  = 0xC0000000;
	uint64_t axi_gpio0_addr = 0x40000000;
	uint64_t axi_gpio1_addr = 0x40010000;
	uint64_t axi_gpio2_addr = 0x40020000;
	uint64_t axi_gpio3_addr = 0x40030000;
	uint64_t axi_gpio4_addr = 0x40040000;
	uint64_t axi_gpio5_addr = 0x40050000;
	uint64_t axi_gpio6_addr = 0x40060000;
	uint64_t axi_gpio7_addr = 0x40070000;
	uint64_t axi_gpio8_addr = 0x40080000;
	uint64_t axi_gpio9_addr = 0x40090000;
	int xdma_fd_read;
	int xdma_fd_wrte;
	char *xdma_c2h_name;
	char *xdma_h2c_name;
	uint8_t wrte_data[DATA_SIZE];
	uint8_t read_data[DATA_SIZE];
	uint32_t val = 0;
	int errorcount = 0;
	ssize_t rc;




	// Display Program Usage Instructions

	if (argc != 3)
	{
		printf("%s is a simple FPGA XDMA Test Program\n", argv[0]);
		printf("that reads and writes to BRAM, toggles LEDs,\n");
		printf("and determines the period of various clocks.\n");
		printf("Usage:\n");
		printf(" sudo %s C2H_DEVICE_NAME   H2C_DEVICE_NAME", argv[0]);
		printf("\nExample:\n");
		printf(" sudo %s /dev/xdma0_c2h_0  /dev/xdma0_h2c_0", argv[0]);
		printf("\n");
		exit(EXIT_FAILURE);
	}




	// Read in Program Arguments

	xdma_fd_read = 0;
	xdma_fd_wrte = 0;
	xdma_c2h_name = malloc(strlen(argv[1]));
	if (!(xdma_c2h_name)) { printf("malloc failed in main, c2h");exit(-1);}
	xdma_h2c_name = malloc(strlen(argv[2]));
	if (!(xdma_h2c_name)) { printf("malloc failed in main, h2c");exit(-1);}
	strncpy(xdma_c2h_name, argv[1], strlen(argv[1]));
	strncpy(xdma_h2c_name, argv[2], strlen(argv[2]));

	printf("FPGA XDMA AXI BRAM GPIO Clock Test Program\n");
	printf("C2H Device: %s\n", xdma_c2h_name);
	printf("H2C Device: %s\n", xdma_h2c_name);
	printf("AXI BRAM Address:  0x%0lX = %ld\n",
		axi_bram_addr, axi_bram_addr);
	printf("AXI GPIO0 Address: 0x%0lX = %ld\n",
		axi_gpio0_addr, axi_gpio0_addr);
	printf("AXI GPIO1 Address: 0x%0lX = %ld\n",
		axi_gpio1_addr, axi_gpio1_addr);
	printf("AXI GPIO2 Address: 0x%0lX = %ld\n",
		axi_gpio2_addr, axi_gpio2_addr);
	printf("AXI GPIO3 Address: 0x%0lX = %ld\n",
		axi_gpio3_addr, axi_gpio3_addr);
	printf("AXI GPIO4 Address: 0x%0lX = %ld\n",
		axi_gpio4_addr, axi_gpio4_addr);
	printf("AXI GPIO5 Address: 0x%0lX = %ld\n",
		axi_gpio5_addr, axi_gpio5_addr);
	printf("AXI GPIO6 Address: 0x%0lX = %ld\n",
		axi_gpio6_addr, axi_gpio6_addr);
	printf("AXI GPIO7 Address: 0x%0lX = %ld\n",
		axi_gpio7_addr, axi_gpio7_addr);
	printf("AXI GPIO8 Address: 0x%0lX = %ld\n",
		axi_gpio8_addr, axi_gpio8_addr);
	printf("AXI GPIO9 Address: 0x%0lX = %ld\n",
		axi_gpio9_addr, axi_gpio9_addr);


	// Open the XDMA Files

	xdma_fd_wrte = open(xdma_h2c_name, O_WRONLY);

	if (xdma_fd_wrte < 0) {
		fprintf(stderr, "unable to open write device %s, %d.\n",
			xdma_h2c_name, xdma_fd_wrte);
		perror("File Open");
	}


	xdma_fd_read = open(xdma_c2h_name, O_RDONLY);

	if (xdma_fd_read < 0) {
		fprintf(stderr, "unable to open read device %s, %d.\n",
			xdma_c2h_name, xdma_fd_read);
		perror("File Open");
	}




	// Generate Random Data

	// Seed the random number generator with an address returned by malloc
	srandom((int)((long int)xdma_c2h_name));

	// Generate a random data array of size DATA_SIZE
	for (int indx = 0; indx < DATA_SIZE ; indx = indx + 4)
	{
		val = rand();
		memcpy(&wrte_data[indx], &val, 4);
	}




	// -------- AXI BRAM Write then Read Test ----------------------------

	// Write the random data to the FPGA's AXI BRAM

	rc = lseek(xdma_fd_wrte, axi_bram_addr, SEEK_SET);

	if (rc < 0) {
		fprintf(stderr, "%s, seek offset failed at 0x%lX, 0x%ld.\n",
				xdma_h2c_name, axi_bram_addr, rc);
		perror("File Seek");
		return -EIO;
	}

	rc = write(xdma_fd_wrte, wrte_data, DATA_SIZE);

	if (rc < 0) {
		fprintf(stderr, "%s, write data @ 0x%lX failed, %ld.\n",
			xdma_h2c_name, axi_bram_addr, rc);
		perror("File Write");
		return -EIO;
	}
	
	if (rc != DATA_SIZE) {
		fprintf(stderr, "%s, write underflow 0x%ld/0x%d @ 0x%lx.\n",
			xdma_h2c_name, rc, DATA_SIZE, axi_bram_addr);
		return -EIO;
	}

	printf("\nWrote %ld bytes to   %s at address 0x%lX\n",
		rc, xdma_h2c_name, axi_bram_addr);




	// Read data from the FPGA's AXI BRAM

	rc = lseek(xdma_fd_read, axi_bram_addr, SEEK_SET);

	if (rc < 0) {
		fprintf(stderr, "%s, seek offset failed at 0x%lX, 0x%ld.\n",
				xdma_c2h_name, axi_bram_addr, rc);
		perror("File Seek");
		return -EIO;

	}

	rc = read(xdma_fd_read, read_data, DATA_SIZE);

	if (rc < 0) {

		fprintf(stderr, "%s, read data @ 0x%lX failed, %ld.\n",
			xdma_c2h_name, axi_bram_addr, rc);
		perror("File Read");
		return -EIO;
	}

	if (rc != DATA_SIZE) {
		fprintf(stderr, "%s, read underflow 0x%ld/0x%d @ 0x%lx.\n",
			xdma_c2h_name, rc, DATA_SIZE, axi_bram_addr);

		return -EIO;
	}

	printf("\nRead  %ld bytes from %s at address 0x%lX\n",
		rc, xdma_c2h_name, axi_bram_addr);




	// Compare the Written and Read Data

	errorcount = 0;
	for (int indx = 0; indx < DATA_SIZE ; indx++)
	{
		if (read_data[indx] != wrte_data[indx])
		{
			errorcount++;
			printf("Data did not match at index %d, ", indx);
			printf("read_data = 0x%02X, wrte_data = 0x%02X\n",
				read_data[indx], wrte_data[indx]);
		}

		// too many errors, something is wrong, do not check any more
		if (errorcount > 7 ) { break; }
	}

	if (errorcount == 0)
	{
		printf("\nSuccess - Read Data matches Written Data!\n\n");
	} else {
		printf("Too many errors encountered, something is wrong.\n\n");
	}




	// -------- AXI GPIO0 LED Test ----------------------------------------

	// Toggle LED once a second for 6 seconds
	for (int indx = 0; indx < 7 ; indx++)
	{
		// LED in design is controlled by the LSB of the first byte
		wrte_data[0] = (0x01 & (uint8_t)indx);

		// Write 1 byte using pwrite, which combines lseek and write
		rc = pwrite(xdma_fd_wrte, (char *)wrte_data, 1, axi_gpio0_addr);
		//fsync(xdma_fd_wrte);

		if (rc < 0) {
			fprintf(stderr, "%s, write byte @ 0x%lX failed, %ld.\n",
				xdma_h2c_name, axi_gpio0_addr, rc);
			perror("File Write");
			return -EIO;
		}


		printf("Wrote 0x%02X to %s at address 0x%lX",
			wrte_data[0], xdma_h2c_name, axi_gpio0_addr);

		if (wrte_data[0]) {
			printf(", LED D18 should be ON.\n");
		} else {
			printf(", LED D18 should be OFF.\n");
		}


		sleep(1);
	}




	printf("\n");




	// -------- AXI GPIO8 LED Test ----------------------------------------

	// Toggle LED once a second for 6 seconds
	for (int indx = 0; indx < 7 ; indx++)
	{
		// LED in design is controlled by the LSB of the first byte
		wrte_data[0] = (0x01 & (uint8_t)indx);

		// Write 1 byte using pwrite, which combines lseek and write
		rc = pwrite(xdma_fd_wrte, (char *)wrte_data, 1, axi_gpio8_addr);
		//fsync(xdma_fd_wrte);

		if (rc < 0) {
			fprintf(stderr, "%s, write byte @ 0x%lX failed, %ld.\n",
				xdma_h2c_name, axi_gpio8_addr, rc);
			perror("File Write");
			return -EIO;
		}


		printf("Wrote 0x%02X to %s at address 0x%lX",
			wrte_data[0], xdma_h2c_name, axi_gpio8_addr);

		if (wrte_data[0]) {
			printf(", LED D19 should be ON.\n");
		} else {
			printf(", LED D19 should be OFF.\n");
		}


		sleep(1);
	}




	printf("\n\nCompare clocks against %dMHz XDMA axi_aclk:", XDMA_CLK_MHz);

	for (int i = 1 ; i < 4 ; i++) {
		printf("\n------------------------------------------------------\n");
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio1_addr, 1);
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio2_addr, 1);
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio3_addr, 1);
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio4_addr, 2);
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio5_addr, 2);
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio6_addr, 2);
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio7_addr, 1);
		estimate_clock_MHz(i, xdma_fd_read, xdma_c2h_name, axi_gpio9_addr, 2);
	}




	printf("\n");

	close(xdma_fd_wrte);
	close(xdma_fd_read);
	exit(EXIT_SUCCESS);
}

