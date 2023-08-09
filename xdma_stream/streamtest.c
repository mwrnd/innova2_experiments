/*

Prerequisites:
 - Xilinx XDMA AXI Stream Project:
   github.com/mwrnd/innova2_experiments/xdma_stream
 - XDMA Drivers from github.com/xilinx/dma_ip_drivers
   Install Instructions at github.com/mwrnd/innova2_flex_xcku15p_notes

Compile with:

  gcc -Wall streamtest.c -o streamtest -lm

Run with:

  sudo ./streamtest

*/

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



#define DATA_SIZE 32



int main(int argc, char **argv)
{
	uint64_t addr = 0;
	int xdma_fd_read;
	int xdma_fd_wrte;
	int count = 0;
	uint8_t wrte_data[DATA_SIZE];
	uint8_t read_data[DATA_SIZE];
	ssize_t rc;
	float num = 3.14;


	char* xdma_h2c_name = "/dev/xdma0_h2c_0";
	char* xdma_c2h_name = "/dev/xdma0_c2h_0";




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




	printf("XDMA AXI Stream Test\n");
	printf("C2H Device: %s\n", xdma_c2h_name);
	printf("H2C Device: %s\n", xdma_h2c_name);


	// fill the write data buffer with floating point values
	printf("\nInput Floating-Point Values:\n");
	count = 1;
	num = 1.123;
	for (int i = 0 ; i < DATA_SIZE; i = i + 4)
	{
		memcpy(&wrte_data[i], &num, 4);
		printf("%02d: num = %f\n", count, num);
		num = num * 2.01;
		count++;
	}
	printf("\n");



	// write data buffer to the AXI Stream
	rc = pwrite(xdma_fd_wrte, wrte_data, DATA_SIZE, addr);
	if (rc < 0) {
		fprintf(stderr, "%s, write data @ 0x%lX failed, %ld.\n",
			xdma_h2c_name, addr, rc);
		perror("File Write");
		return -EIO;
	}




	// read data from the AXI Stream into buffer
	printf("\nOutput Floating-Point Values:\n");
	rc = pread(xdma_fd_read, read_data, DATA_SIZE, addr);
	if (rc < 0) {
		fprintf(stderr, "%s, read data @ 0x%lX failed, %ld.\n",
			xdma_c2h_name, addr, rc);
		perror("File Read");
		return -EIO;
	}




	// print the data in the return data buffer
	count = 1;
	for (int i = 0 ; i < DATA_SIZE; i = i + 4)
	{
		memcpy(&num, &read_data[i], 4);
		printf("%02d: num = %f, sqrt(num)=%f\n",
			count, num, sqrt(num));
		count++;
	}




	printf("\n");

	close(xdma_fd_wrte);
	close(xdma_fd_read);
	exit(EXIT_SUCCESS);
}

