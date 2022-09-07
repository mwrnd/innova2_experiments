/*

Test XDMA-to-UART Communication

File assumes installed dma_ip_drivers exist in the same directory. Compile then run with:

  gcc -Wall -Wno-unused-variable -Wno-unused-but-set-variable  -lrt -o uart uart.c
  sudo ./uart


In a seperate terminal, test with:

  cd  dma_ip_drivers/XDMA/linux-kernel/tools/

  echo -n -e "\xa5" >a5.bin   ;   xxd a5.bin

  sudo ./dma_to_device   -v -d /dev/xdma0_h2c_0 -a 0x60100004 -s 1 -f a5.bin
  sudo ./dma_to_device   -v -d /dev/xdma0_h2c_0 -a 0x60110004 -s 1 -f a5.bin
  sudo ./dma_from_device -v -d /dev/xdma0_c2h_0 -a 0x60110008 -s 4 -f RECV ; xxd -b RECV
  sudo ./dma_from_device -v -d /dev/xdma0_c2h_0 -a 0x60110000 -s 4 -f RECV ; xxd RECV
  sudo ./dma_from_device -v -d /dev/xdma0_c2h_0 -a 0x60100008 -s 4 -f RECV ; xxd -b RECV
  sudo ./dma_from_device -v -d /dev/xdma0_c2h_0 -a 0x60100000 -s 4 -f RECV ; xxd RECV

*/




#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>


#include "dma_ip_drivers/XDMA/linux-kernel/xdma/cdev_sgdma.h"
#include "dma_ip_drivers/XDMA/linux-kernel/tools/dma_utils.c"


// dpkg -l | grep libfuse
//   ii  libfuse2:amd64  2.9.9-3  amd64  Filesystem in Userspace (library)
//#include <fuse_opt.h>
//#include <cuse_lowlevel.h>
//#include "fioc.h"




/* xdma_uart-to-uart has 4 each of C2H and H2C DMA channels */
/* Using Channel 3 on assumption other channels are being used elsewhere */
#define C2H_DEVICE_NAME "/dev/xdma0_c2h_3"
#define H2C_DEVICE_NAME "/dev/xdma0_h2c_3"

/* SIZE_DEFAULT < 4096 , note 4 bytes = 32 bits */
#define SIZE_DEFAULT (4)
#define COUNT_DEFAULT (1)

#define UART_RX_ADDRESS    0x60100000
#define UART_TX_ADDRESS    0x60100004
#define UART_STAT_ADDRESS  0x60100008
#define UART_CTRL_ADDRESS  0x6010000C


#define UART2_RX_ADDRESS   0x60110000
#define UART2_TX_ADDRESS   0x60110004
#define UART2_STAT_ADDRESS 0x60110008
#define UART2_CTRL_ADDRESS 0x6011000C







int read_uart_status(char *readdevname, int readdev_fd, uint32_t axi_status_address, int debug)
{

	uint8_t uart_status = 0;
	uint8_t uart_char = 0;
	uint8_t uart_rx_fifo_data_valid = 0;
	uint8_t uart_rx_fifo_full = 0;
	uint8_t uart_tx_fifo_empty = 0;
	uint8_t uart_tx_fifo_full = 0;
	uint8_t uart_clear_to_send = 0;

	ssize_t rc = 0;
	size_t out_offset = 0;
	char buffer[SIZE_DEFAULT];


	/* Read Status Register */
	if (debug)
		printf("reading %d bytes from %s at address 0x%x\n", SIZE_DEFAULT, readdevname, axi_status_address);

	rc = read_to_buffer(readdevname, readdev_fd, buffer, SIZE_DEFAULT, axi_status_address);
	if (rc < 0)
		return rc;

	if (rc < SIZE_DEFAULT) {
		fprintf(stderr, "underflow %ld/%ld.\n", rc, (size_t)SIZE_DEFAULT);
	}

	uart_status = (uint8_t)(buffer[0]);

	/* Bit0 = Rx FIFO Valid Data */
	uart_rx_fifo_data_valid = (uart_status & 0x01) >> 0;
	uart_rx_fifo_full       = (uart_status & 0x02) >> 1;
	uart_tx_fifo_empty      = (uart_status & 0x04) >> 2;
	uart_tx_fifo_full       = (uart_status & 0x08) >> 3;
	uart_clear_to_send      = (uart_status & 0x10) >> 4;

	/* print the data */
	if (debug) {
		printf("uart_status = 0x%02x\n", uart_status);
		printf("\tBit0 = uart_rx_fifo_data_valid = %d\n", uart_rx_fifo_data_valid);
		printf("\tBit1 = uart_rx_fifo_full       = %d\n", uart_rx_fifo_full);
		printf("\tBit2 = uart_tx_fifo_empty      = %d\n", uart_tx_fifo_empty);
		printf("\tBit3 = uart_tx_fifo_full       = %d\n", uart_tx_fifo_full);
		printf("\tBit4 = uart_clear_to_send      = %d\n", uart_clear_to_send);
		printf("\n");
	}

	return uart_rx_fifo_data_valid;
}



int read_uart_char(char *readdevname, int readdev_fd, uint32_t axi_uart_rx_address, int debug)
{

	uint8_t uart_char = 0;
	ssize_t rc = 0;
	char buffer[SIZE_DEFAULT];

	if (debug)
		printf("reading %d bytes from %s at address 0x%x\n", SIZE_DEFAULT, readdevname, axi_uart_rx_address);

	rc = read_to_buffer(readdevname, readdev_fd, buffer, SIZE_DEFAULT, axi_uart_rx_address);
	if (rc < 0)
		return rc;

	if (rc < SIZE_DEFAULT) {
		fprintf(stderr, "underflow %ld/%ld.\n", rc, (size_t)SIZE_DEFAULT);
	}

	if (rc > 0) {
		uart_char = (uint8_t)(buffer[0]);

		if (debug)
			printf("uart_char = 0x%02x\n\n", uart_char);
	}


	return uart_char;
}



int main(int argc, char *argv[])
{

	char *readdevname  = C2H_DEVICE_NAME;
	char *writedevname = H2C_DEVICE_NAME;
	uint64_t address = 0;
	uint64_t aperture = 0;
	uint64_t size = SIZE_DEFAULT;
	uint64_t offset = 0;
	uint64_t count = COUNT_DEFAULT;
	char *ofname = NULL;

	ssize_t rc = 0;
	size_t out_offset = 0;
	size_t bytes_done = 0;
	uint64_t i;
	char buffer[SIZE_DEFAULT];
	//char *buffer = NULL;
	//char *allocated = NULL;
	struct timespec ts_start, ts_end;
	int out_fd = -1;
	int fpga_fd_rd;
	int fpga_fd_wr;
	long total_time = 0;
	float result;
	float avg_time = 0;
	int underflow = 0;
	






	/* C2H Read Input Device File Open */
	fprintf(stdout,
		"dev %s, addr 0x%lx, aperture 0x%lx, size 0x%lx, offset 0x%lx, "
		"count %lu\n",
		readdevname, address, aperture, size, offset, count);

	fpga_fd_rd = open(readdevname, O_RDWR);

	if (fpga_fd_rd < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n", readdevname, fpga_fd_rd);
		perror("open device");
		return -EINVAL;
	}




	/* H2C Write Output Device File Open */
	fprintf(stdout,
		"dev %s, addr 0x%lx, aperture 0x%lx, size 0x%lx, offset 0x%lx, "
		"count %lu\n",
		writedevname, address, aperture, size, offset, count);

	fpga_fd_wr = open(writedevname, O_RDWR);

	if (fpga_fd_wr < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n", writedevname, fpga_fd_wr);
		perror("open device");
		return -EINVAL;
	}


	printf("\n\n");






	if (read_uart_status(readdevname, fpga_fd_rd, (uint32_t)UART_STAT_ADDRESS, 1))
		read_uart_char(readdevname, fpga_fd_rd, (uint32_t)UART_RX_ADDRESS, 1);


	printf("\n");


	if (read_uart_status(readdevname, fpga_fd_rd, (uint32_t)UART2_STAT_ADDRESS, 1))
		read_uart_char(readdevname, fpga_fd_rd, (uint32_t)UART2_RX_ADDRESS, 1);

	printf("------------\n");





	uint8_t uchr = 0;

	while (1) {

		if (read_uart_status(readdevname, fpga_fd_rd, UART_STAT_ADDRESS, 0)) {
			uchr = read_uart_char(readdevname, fpga_fd_rd, UART_RX_ADDRESS, 0);
			fprintf(stderr, "0x%02x,", uchr);
		}

		if (read_uart_status(readdevname, fpga_fd_rd, UART2_STAT_ADDRESS, 0)) {
			uchr = read_uart_char(readdevname, fpga_fd_rd, UART2_RX_ADDRESS, 0);
			fprintf(stderr, "0x%02x,", uchr);
		}

		uchr = 0;
	}












	close(fpga_fd_rd);
	close(fpga_fd_wr);

	return rc;
}

