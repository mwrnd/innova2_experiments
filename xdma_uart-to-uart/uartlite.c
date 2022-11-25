/*
 * BSD 2-Clause License

 xdma_uart-to-uart UARTlite Debug


TODO: - Can write up to count=25 bytes of data to XDMA0
        Then successfully read up to count=17 bytes of data from XDMA1
        How? UART FIFO length is 2^4 = 16

Notes:
 This software is a workaround to avoid using a proper
 driver for a PCIe XDMA AXI UART to behave as a TTY.
 A UART design that does not generate any AXI errors is required.
 This will NOT work with Xilinx's AXI UART Lite (pg142) as it generates an
 AXI Bus Error if the status byte is read when the UART Lite FIFO is empty.
 Interaction with the AXI UART is byte-wise and sequential. There is a lot of
 overhead as each byte read or written requires a status byte read.
 Note the XDMA driver and this software cannot deal with system suspend.
 Compile with --std=gnu1x for Atomics and GNU Libc signal support.

Prerequisites:
 - Xilinx XDMA to AXI FPGA project with a non-blocking UART such as
   github.com/eugene-tarassov/vivado-risc-v/blob/v3.3.0/uart/uart.v
 - XDMA Drivers from github.com/xilinx/dma_ip_drivers
   Install Instructions at github.com/mwrnd/innova2_flex_xcku15p_notes


Compile with:

 gcc uartlite.c `pkg-config fuse --cflags --libs` --std=gnu17 -g -Wall -latomic -o uartlite

Run with:

 sudo ./uartlite /dev/xdma0_c2h_0 /dev/xdma0_h2c_0 0x60300000 /dev/xdma0_c2h_1 /dev/xdma0_h2c_1 0x60310000

*/

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>


// refer to uart.v for register addresses/offsets
// github.com/eugene-tarassov/vivado-risc-v/blob/v3.3.0/uart/uart.v
#define UART_RX_ADDR_OFFSET    0x0
#define UART_TX_ADDR_OFFSET    0x4
#define UART_STAT_ADDR_OFFSET  0x8
#define UART_CTRL_ADDR_OFFSET  0xC


void on_ctrl_backslash(int sig);


struct _XDMA {
	atomic_int initialized;
	uint64_t  addr;
	int  fd_read;
	int  fd_wrte;
	char *c2h_name;
	char *h2c_name;
	char *title;
	volatile uint8_t uart_rx_has_data;
	volatile uint8_t uart_tx_fifo_not_full;
	volatile uint8_t uart_clear_to_send;
} xdma0, xdma1;


volatile unsigned int _xdma_read_errors  = 0;
#define  MAX_xdma_read_errors   16
volatile unsigned int _xdma_write_errors = 0;
#define  MAX_xdma_write_errors  16




ssize_t read_byte_from_xdma(struct _XDMA *xdma, uint64_t offset, char *buffer)
{
	ssize_t rc;
	char *buf = buffer;

	if (atomic_load(&xdma->initialized))
	{
		/*
		rc = lseek(xdma->fd_read, (xdma->addr + offset), SEEK_SET);

		if (rc < 0) {
			fprintf(stderr, "%s, seek offset 0x%lx != 0x%lx.\n",
					xdma->c2h_name, offset, rc);
			perror("File Seek");
			return -EIO;
		}
		*/
		//rc = read(xdma->fd_read, buf, 1);
		rc = pread(xdma->fd_read, buf, 1, (xdma->addr + offset));

		if (rc < 0) {
			fprintf(stderr, "%s, read byte @ 0x%lx failed %ld.\n",
				xdma->c2h_name, (xdma->addr + offset), rc);
			perror("File Read");
			return -EIO;
		}

	}

	return 1;
}




ssize_t write_byte_to_xdma(struct _XDMA *xdma, uint64_t offset, char *buffer)
{
	ssize_t rc;
	char *buf = buffer;

	if (atomic_load(&xdma->initialized))
	{
/*
		rc = lseek(xdma->fd_wrte, (xdma->addr + offset), SEEK_SET);

		if (rc < 0) {
			fprintf(stderr, "%s, seek offset 0x%lx != 0x%lx.\n",
					xdma->h2c_name, offset, rc);
			perror("File Seek");
			return -EIO;
		}
*/
		//rc = write(xdma->fd_wrte, buf, 1);
		rc = pwrite(xdma->fd_wrte, buf, 1, (xdma->addr + offset));
		fsync(xdma->fd_wrte);

		if (rc < 0) {
			fprintf(stderr, "%s, write byte @ 0x%lx failed %ld.\n",
				xdma->h2c_name, (xdma->addr + offset), rc);
			perror("write file");
			return -EIO;
		}
	}

	return 1;
}








void print_gpio2_state(struct _XDMA *xdma)
{
	char readbuffer[1];
	uint8_t gpio2;
	ssize_t rc = 0;
	uint8_t async_resetn = 0;
	uint8_t uart0_DBG_tx_empty = 0;
	uint8_t uart0_DBG_rx_full = 0;
	uint8_t uart0_DBG_tx_full = 0;
	uint8_t uart1_DBG_tx_empty = 0;
	uint8_t uart1_DBG_rx_full = 0;
	uint8_t uart1_DBG_tx_full = 0;
	uint8_t uart1_RTSn = 0;



	printf("\n");

	// Read GPIO2
	rc = lseek(xdma->fd_read, (uint64_t)0x60220000, SEEK_SET);
	if (rc < 0) { printf("GPIO2 seek failed\n");  }

	rc = read(xdma->fd_read, readbuffer, 1);

	if (rc < 0) {
		printf("GPIO2 read failed\n");
	} else {

		gpio2 = (uint8_t)(readbuffer[0]);

		async_resetn        = (gpio2 & 0x01) >> 0;
		uart0_DBG_tx_empty  = (gpio2 & 0x02) >> 1;
		uart0_DBG_rx_full   = (gpio2 & 0x04) >> 2;
		uart0_DBG_tx_full   = (gpio2 & 0x08) >> 3;
		uart1_DBG_tx_empty  = (gpio2 & 0x10) >> 4;
		uart1_DBG_rx_full   = (gpio2 & 0x20) >> 5;
		uart1_DBG_tx_full   = (gpio2 & 0x40) >> 6;
		uart1_RTSn          = (gpio2 & 0x80) >> 7;

		printf("\nGPIO2 = 0x%02x\n", gpio2);
		printf("\tBit0 = async_resetn       = %d\n",
				async_resetn);
		printf("\tBit1 = uart0_DBG_tx_empty = %d\n",
				uart0_DBG_tx_empty);
		printf("\tBit2 = uart0_DBG_rx_full  = %d\n",
				uart0_DBG_rx_full);
		printf("\tBit3 = uart0_DBG_tx_full  = %d\n",
				uart0_DBG_tx_full);
		printf("\tBit4 = uart1_DBG_tx_empty = %d\n",
				uart1_DBG_tx_empty);
		printf("\tBit5 = uart1_DBG_rx_full  = %d\n",
				uart1_DBG_rx_full);
		printf("\tBit6 = uart1_DBG_tx_full  = %d\n",
				uart1_DBG_tx_full);
		printf("\tBit7 = uart1_RTSn         = %d\n",
				uart1_RTSn);
		printf("\n");
	}
}









void print_debug_info(struct _XDMA *xdma)
{
	uint8_t uart_status = 0;
	uint8_t uart_rx_fifo_data_valid = 0;
	uint8_t uart_rx_fifo_full = 0;
	uint8_t uart_tx_fifo_empty = 0;
	uint8_t uart_tx_fifo_full = 0;
	uint8_t uart_intr_enabled = 0;
	uint8_t uart_overrun_error = 0;
	uint8_t uart_frame_error = 0;
	uint8_t uart_parity_error = 0;
	ssize_t rc = 0;
	char buffer[1];

	if (atomic_load(&xdma->initialized))
	{
		// read STATUS byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(xdma, UART_STAT_ADDR_OFFSET, buffer);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read in debug ");
			fprintf(stderr, "failed with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		uart_status = (uint8_t)(buffer[0]);

		// refer to uart.v for STATUS byte bit offsets and definitions
		// github.com/eugene-tarassov/vivado-risc-v/blob/v3.3.0/uart/uart.v
		uart_rx_fifo_data_valid = (uart_status & 0x01) >> 0;
		uart_rx_fifo_full       = (uart_status & 0x02) >> 1;
		uart_tx_fifo_empty      = (uart_status & 0x04) >> 2;
		uart_tx_fifo_full       = (uart_status & 0x08) >> 3;
		uart_intr_enabled       = (uart_status & 0x10) >> 4;
		uart_overrun_error      = (uart_status & 0x20) >> 5;
		uart_frame_error        = (uart_status & 0x40) >> 6;
		uart_parity_error       = (uart_status & 0x80) >> 7;

		printf("\n%s uart_status = 0x%02x\n", xdma->title, uart_status);
		printf("\tBit0 = uart_rx_fifo_data_valid = %d\n",
				uart_rx_fifo_data_valid);
		printf("\tBit1 = uart_rx_fifo_full       = %d\n",
				uart_rx_fifo_full);
		printf("\tBit2 = uart_tx_fifo_empty      = %d\n",
				uart_tx_fifo_empty);
		printf("\tBit3 = uart_tx_fifo_full       = %d\n",
				uart_tx_fifo_full);
		printf("\tBit4 = uart_intr_enabled       = %d\n",
				uart_intr_enabled);
		printf("\tBit5 = uart_overrun_error      = %d\n",
				uart_overrun_error);
		printf("\tBit6 = uart_frame_error        = %d\n",
				uart_frame_error);
		printf("\tBit7 = uart_parity_error       = %d\n",
				uart_parity_error);


		xdma->uart_rx_has_data = uart_rx_fifo_data_valid;
		//xdma.uart_tx_fifo_not_full = !uart_tx_fifo_full;
		xdma->uart_tx_fifo_not_full =
				!((uart_status & 0x08) >> 3);

		printf("\n");

		printf("%s xdma.uart_rx_has_data = %d\n", xdma->title, xdma->uart_rx_has_data);
		printf("%s xdma.uart_tx_fifo_not_full = %d\n",
			xdma->title, xdma->uart_tx_fifo_not_full);


		printf("\n");

		printf("%s xdma->fd_read = %d, xdma->fd_wrte = %d\n",
			xdma->title, xdma->fd_read, xdma->fd_wrte);

		printf("%s struct address = %lX\n",
			xdma->title, (uint64_t)(xdma));

		printf("\n");
	}
}








static void xdma_tty_open(struct _XDMA *xdma)
{

	printf("%s Opening C2H Device: %s\n", xdma->title, xdma->c2h_name);
	printf("%s Opening H2C Device: %s\n", xdma->title, xdma->h2c_name);


	xdma->fd_wrte = open(xdma->h2c_name, O_WRONLY);

	if (xdma->fd_wrte < 0) {
		fprintf(stderr, "unable to open write device %s, %d.\n",
			xdma->h2c_name, xdma->fd_wrte);
		perror("open device");
	}

	xdma->fd_read = open(xdma->c2h_name, O_RDONLY);

	if (xdma->fd_read < 0) {
		fprintf(stderr, "unable to open read device %s, %d.\n",
			xdma->c2h_name, xdma->fd_read);
		perror("open device");
	}

	printf("%s Read File Descriptor  = %d\n", xdma->title, xdma->fd_read);
	printf("%s Write File Descriptor = %d\n", xdma->title, xdma->fd_wrte);

	atomic_store(&xdma->initialized, 1);

}




struct timeval polltimestr;
struct timeval polltimeend;
volatile long int max_poll_interval = 0;
volatile long int min_poll_interval = 999999999;
volatile int polltimeend_valid = 0;
volatile long int max_poll_runtime = 0;


static void xdma_tty_poll(struct _XDMA *xdma)
{
	long int seconds = 0;
	long int microseconds = 0;
	long int elapsed_usec = 0;

	// if this poll function has run at least once, log time between runs
	gettimeofday(&polltimestr, 0);
	if (polltimeend_valid) {
		seconds = polltimestr.tv_sec - polltimeend.tv_sec;
		microseconds = polltimestr.tv_usec - polltimeend.tv_usec;
		elapsed_usec = (seconds * 1000000) + microseconds;
		if (elapsed_usec > max_poll_interval) {
			max_poll_interval = elapsed_usec;
		}
		if (elapsed_usec < min_poll_interval) {
			min_poll_interval = elapsed_usec;
		}
	}


	uint8_t uart_status = 0;
	uint8_t uart_rx_fifo_data_valid = 0;
	//uint8_t uart_rx_fifo_full = 0;
	//uint8_t uart_tx_fifo_empty = 0;
	uint8_t uart_tx_fifo_full = 0;
	uint8_t uart_clear_to_send = 0;
	ssize_t rc = 0;
	char buffer[1];


	// read status byte from the XDMA UART (C2H)
	if (atomic_load(&xdma->initialized))
	{

		rc = read_byte_from_xdma(xdma, UART_STAT_ADDR_OFFSET, buffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "S-%02x-", buffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read in _poll ");
			fprintf(stderr, "failed with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		if (_xdma_read_errors > MAX_xdma_read_errors) {
			printf("\nSomething is wrong with the XDMA driver. ");
			printf("%d XDMA read errors encountered.\n",
				_xdma_read_errors);
			on_ctrl_backslash(0); // exit
		}


		uart_status = (uint8_t)(buffer[0]);

		// refer to uart.v for STATUS byte bit offsets and definitions
		// github.com/eugene-tarassov/vivado-risc-v/blob/v3.3.0/uart/uart.v
		uart_rx_fifo_data_valid = (uart_status & 0x01) >> 0;
		//uart_rx_fifo_full       = (uart_status & 0x02) >> 1;
		//uart_tx_fifo_empty      = (uart_status & 0x04) >> 2;
		uart_tx_fifo_full       = (uart_status & 0x08) >> 3;
		uart_clear_to_send      = (uart_status & 0x10) >> 4;

		xdma->uart_rx_has_data = uart_rx_fifo_data_valid;
		xdma->uart_tx_fifo_not_full = !uart_tx_fifo_full;
		xdma->uart_clear_to_send = uart_clear_to_send;

	}


	// time the _poll function
	gettimeofday(&polltimeend, 0);
	seconds = polltimeend.tv_sec - polltimestr.tv_sec;
	microseconds = polltimeend.tv_usec - polltimestr.tv_usec;
	elapsed_usec = (seconds * 1000000) + microseconds;
	if ((elapsed_usec > max_poll_runtime)) {
		max_poll_runtime = elapsed_usec;
	}
	polltimeend_valid = 1; // enable timing interval between calls to _poll

}




static void xdma_tty_read(struct _XDMA *xdma, size_t size)
{
	int count = 0;
	char readbuffer[1];
	ssize_t rc = 0;
	char*  buffer;
	buffer = malloc((size+1));

	if (!buffer) { printf("malloc failed in read"); on_ctrl_backslash(1); }

	// if _read is running, then _poll has read the STATUS byte
	// and there is data in the UART FIFO
	while (atomic_load(&xdma->initialized) &&
		(xdma->uart_rx_has_data) && (count < size))
	{
		// read RX DATA byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(xdma, UART_RX_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "D-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART data read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		buffer[count] = readbuffer[0];

		count++;

		// read STATUS byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(xdma, UART_STAT_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "S-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		xdma->uart_rx_has_data      =
				 ((((uint8_t)(readbuffer[0])) & 0x01) >> 0);
		xdma->uart_tx_fifo_not_full =
				!((((uint8_t)(readbuffer[0])) & 0x08) >> 3);

	}

	// return the number of bytes successfully read from the UART FIFO
	free(buffer);
}




static void xdma_tty_write(struct _XDMA *xdma, const char *buf, size_t size)
{
	int count = 0;
	char readbuffer[1];
	char writebuffer[1];
	ssize_t rc = 0;


	if (atomic_load(&xdma->initialized))
	{
		// before writing, confirm TX can be written to
		// read STATUS byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(xdma, UART_STAT_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "S-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		xdma->uart_rx_has_data      =
				 ((((uint8_t)(readbuffer[0])) & 0x01) >> 0);
		xdma->uart_tx_fifo_not_full =
				!((((uint8_t)(readbuffer[0])) & 0x08) >> 3);
	}


	// if _write is running, then _poll has read the STATUS byte
	// and there is room for data in the UART FIFO
	while (atomic_load(&xdma->initialized) &&
		(xdma->uart_tx_fifo_not_full) && (count <= size))
	{
		// write TX DATA byte to the XDMA UART (H2C)

		writebuffer[0] = buf[count];
		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "W-%c-", buf[count]);

		rc = write_byte_to_xdma(xdma, UART_TX_ADDR_OFFSET, writebuffer);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART write failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_write_errors++;
		}

		count++;


		// read STATUS byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(xdma, UART_STAT_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "S-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		xdma->uart_rx_has_data      =
				 ((((uint8_t)(readbuffer[0])) & 0x01) >> 0);
		xdma->uart_tx_fifo_not_full =
				!((((uint8_t)(readbuffer[0])) & 0x08) >> 3);
	}


	if (_xdma_write_errors > MAX_xdma_write_errors) {
		printf("\nSomething is wrong with the XDMA driver. ");
		printf("%d XDMA write errors encountered.\n", _xdma_write_errors);
		on_ctrl_backslash(0); // exit
	}


}





void on_ctrl_backslash(int sig)
{
	// also used as cleanup function
	atomic_store(&xdma0.initialized, 0);
	atomic_store(&xdma1.initialized, 0);
	if (sig == SIGQUIT) {
		printf("\nCTRL-\\ (SIGQUIT) Pressed.");
	}
	printf("\nQuitting...");
	printf("\nXDMA: Read Errors = %d, Write Errors = %d",
		_xdma_read_errors, _xdma_write_errors);
	printf("\n(Min, Max) Poll Interval = (%ld, %ld) usec and ",
		min_poll_interval, max_poll_interval);
	printf("max run time = %ld usec\n", max_poll_runtime);
	if(xdma0.fd_wrte)  close(xdma0.fd_wrte);
	if(xdma0.fd_read)  close(xdma0.fd_read);
	if(xdma0.c2h_name)  free(xdma0.c2h_name);
	if(xdma0.h2c_name)  free(xdma0.h2c_name);
	if(xdma1.fd_wrte)  close(xdma1.fd_wrte);
	if(xdma1.fd_read)  close(xdma1.fd_read);
	if(xdma1.c2h_name)  free(xdma1.c2h_name);
	if(xdma1.h2c_name)  free(xdma1.h2c_name);
	exit(EXIT_SUCCESS);
}




int main(int argc, char **argv)
{

	ssize_t rc = 0;

	if (argc != 7)
	{
		printf("%s test an XDMA AXI UART Design.\n", argv[0]);
		printf("Press CTRL-\\ to quit.\n");
		printf("Usage:\n");
		printf("  sudo %s  C2H_DEVICE0_NAME  ", argv[0]);
		printf("H2C_DEVICE0_NAME  AXI_ADDR0   C2H_DEVICE1_NAME");
		printf("  H2C_DEVICE1_NAME  AXI_ADDR1\nExample:\n");
		printf("  sudo %s  /dev/xdma0_c2h_0  ", argv[0]);
		printf("/dev/xdma0_h2c_0  0x60100000  /dev/xdma0_c2h_1");
		printf("  /dev/xdma0_h2c_1  0x60110000\n");
		printf("\n");
		exit(EXIT_FAILURE);
	}


	// TODO - sprintf and strcpy are unsafe for use
	// with unsanitized and/or untrusted inputs

	atomic_init(&xdma0.initialized, 0);
	xdma0.fd_read = 0;
	xdma0.fd_wrte = 0;
	xdma0.uart_rx_has_data = 0;
	xdma0.uart_tx_fifo_not_full = 0;
	xdma0.addr = (uint64_t)strtol(argv[3], NULL, 16);
	xdma0.c2h_name = malloc(strlen(argv[1]));
	if (!(xdma0.c2h_name)) { printf("malloc failed in main"); exit(-1); }
	xdma0.h2c_name = malloc(strlen(argv[2]));
	if (!(xdma0.h2c_name)) { printf("malloc failed in main"); exit(-1); }
	strcpy(xdma0.c2h_name, argv[1]);
	strcpy(xdma0.h2c_name, argv[2]);

	xdma0.title = malloc(32);
	if (!(xdma0.title)) { printf("malloc failed in main"); exit(-1); }
	strcpy(xdma0.title, "XDMA0");
	xdma1.title = malloc(32);
	if (!(xdma1.title)) { printf("malloc failed in main"); exit(-1); }
	strcpy(xdma1.title, "XDMA1");


	atomic_init(&xdma1.initialized, 0);
	xdma1.fd_read = 0;
	xdma1.fd_wrte = 0;
	xdma1.uart_rx_has_data = 0;
	xdma1.uart_tx_fifo_not_full = 0;
	xdma1.addr = (uint64_t)strtol(argv[6], NULL, 16);
	xdma1.c2h_name = malloc(strlen(argv[1]));
	if (!(xdma1.c2h_name)) { printf("malloc failed in main"); exit(-1); }
	xdma1.h2c_name = malloc(strlen(argv[2]));
	if (!(xdma1.h2c_name)) { printf("malloc failed in main"); exit(-1); }
	strcpy(xdma1.c2h_name, argv[4]);
	strcpy(xdma1.h2c_name, argv[5]);


	printf("XDMA AXI UART Test.\n");
	printf("Press CTRL-\\ to quit.\n");
	printf("C2H Device 0: %s\n", xdma0.c2h_name);
	printf("H2C Device 0: %s\n", xdma0.h2c_name);
	printf("AXI Base Address 0: %s = %ld\n", argv[3], xdma0.addr);
	printf("C2H Device 1: %s\n", xdma1.c2h_name);
	printf("H2C Device 1: %s\n", xdma1.h2c_name);
	printf("AXI Base Address 1: %s = %ld\n", argv[6], xdma1.addr);


	// handle CTRL-\ as quit; CUSE is set to run in foreground, need exit
	// stackoverflow.com/questions/8903448/libfuse-exiting-fuse-session-loop
	struct sigaction sigactquit;

	sigemptyset(&sigactquit.sa_mask);
	sigactquit.sa_flags = (SA_NODEFER | SA_RESETHAND); // NO SA_RESTART
	sigactquit.sa_handler = on_ctrl_backslash;
	rc = sigaction(SIGQUIT, &sigactquit, NULL);

	if (rc < 0) {
		printf("\nCannot install SIGQUIT handler, exiting...\n");
		on_ctrl_backslash(EXIT_FAILURE);
	}




	xdma_tty_open(&xdma0);
	xdma_tty_open(&xdma1);
	printf("XDMA0 read fd: %d\n",  xdma0.fd_read);
	printf("XDMA0 write fd: %d\n", xdma0.fd_wrte);
	printf("XDMA1 read fd: %d\n",  xdma1.fd_read);
	printf("XDMA1 write fd: %d\n", xdma1.fd_wrte);
	printf("\n");

	printf("%s struct address = %lX\n", xdma0.title, (uint64_t)(&xdma0));
	printf("%s struct address = %lX\n", xdma1.title, (uint64_t)(&xdma1));
	printf("\n");




	// Reset the AXI UART Blocks
	char databuf[1];
	printf("\n");

	// Read GPIO2 to check state of RESET signal
	printf("\nGPIO2 Reset Signal Initial Value");
	print_gpio2_state(&xdma0);


	// Read GPIO0 to check state of RESET signal
	rc = lseek(xdma0.fd_read, (uint64_t)0x60200000, SEEK_SET);
	if (rc < 0) { printf("GPIO0 seek failed\n");  }

	rc = read(xdma0.fd_read, databuf, 1);
	if (rc < 0) { printf("GPIO0 read failed\n");  } else
	{ printf("GPIO0 Reset Signal Initial Value = %d\n", (uint8_t)databuf[0]); }



	// Set GPIO0 to 1; Reset the AXI UART Blocks
	rc = lseek(xdma0.fd_wrte, (uint64_t)0x60200000, SEEK_SET);
	if (rc < 0) { printf("GPIO0 seek failed\n");  }

	databuf[0] = (uint8_t)0xFF;
	rc = write(xdma0.fd_wrte, databuf, 1);
	if (rc < 0) { printf("GPIO0 write failed\n");  }



	// Read GPIO2 to confirm RESET signal is Active
	printf("\nGPIO2 After GPIO0 Write 0xFF");
	print_gpio2_state(&xdma0);



	// Read GPIO0 to check state of RESET signal
	rc = lseek(xdma0.fd_read, (uint64_t)0x60200000, SEEK_SET);
	if (rc < 0) { printf("GPIO0 seek failed\n");  }

	rc = read(xdma0.fd_read, databuf, 1);
	if (rc < 0) { printf("GPIO0 read failed\n");  } else
	{ printf("GPIO0 Reset Signal Value after write = %d\n", (uint8_t)databuf[0]); }



	// Set GPIO0 to 0 to de-assert RESET Signal
	rc = lseek(xdma0.fd_wrte, (uint64_t)0x60200000, SEEK_SET);
	if (rc < 0) { printf("GPIO0 seek failed\n");  }

	databuf[0] = (uint8_t)0x00;
	rc = write(xdma0.fd_wrte, databuf, 1);
	if (rc < 0) { printf("GPIO0 write failed\n");  }



	// Read GPIO2 to confirm RESET signal is de-activeted
	printf("\nGPIO2 After GPIO0 Write 0x00");
	print_gpio2_state(&xdma0);



	// Read GPIO0 to check state of RESET signal
	rc = lseek(xdma0.fd_read, (uint64_t)0x60200000, SEEK_SET);
	if (rc < 0) { printf("GPIO0 seek failed\n");  }

	rc = read(xdma0.fd_read, databuf, 1);
	if (rc < 0) { printf("GPIO0 read failed\n");  }
	
	printf("GPIO0 Reset Signal Final Value = %d\n", (uint8_t)databuf[0]);


	printf("\n");



	int count = 0;
	char readbuffer[1];
	char writebuffer[1];

	#define BUFSIZE  1024
	char buf[BUFSIZE];
	char readstring[BUFSIZE];


writebuffer[0] = (char)0x41;
write_byte_to_xdma(&xdma0, UART_TX_ADDR_OFFSET, writebuffer);


	printf("-----------------------------------------------\n");
	print_debug_info(&xdma0);
	printf("-----------------------------------------------\n");
	print_debug_info(&xdma1);
	printf("-----------------------------------------------\n");



	xdma_tty_poll(&xdma0);
//	printf("--------Resetting FIFOs------------------------\n");
	buf[0] = 3; // reset all FIFOs and disable interrupts
//	write_byte_to_xdma(&xdma0, UART_CTRL_ADDR_OFFSET, buf);
//	write_byte_to_xdma(&xdma1, UART_CTRL_ADDR_OFFSET, buf);

	usleep(1000); // allow the FIFO reset to take effect

	count = 65;
	for (int i = 0; i < BUFSIZE; i++) {
		buf[i] = count;
		count++;
		if (count > 122) { count = 65; }
	}


	printf("============Writing Data to XDMA0==============\n");

	count = 0;
	while (xdma0.uart_tx_fifo_not_full)
	{

		// write TX DATA byte to the XDMA UART (H2C)
		writebuffer[0] = buf[count];
		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "W-%c-", buf[count]);

		rc = write_byte_to_xdma(&xdma0, UART_TX_ADDR_OFFSET, writebuffer);
		usleep(4000);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART write failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_write_errors++;
		}

		count++;
		xdma_tty_poll(&xdma0);
	}

	buf[count] = '\0';
	printf("\n\nWrote count = %d bytes of data to XDMA0 : %s\n\n", count, buf);

	// Read GPIO2 to confirm state of signals
	printf("\nGPIO2 After Write");
	print_gpio2_state(&xdma0);


	printf("-----------------------------------------------\n");
	print_debug_info(&xdma0);
	printf("-----------------------------------------------\n");
	print_debug_info(&xdma1);
	printf("-----------------------------------------------\n");


	printf("============Reading Data from XDMA1============\n");


	// zero out text buffers
	for (int i = 0; i < BUFSIZE; i++) {
		buf[i] = '\0';
		readstring[i] = '\0';
	}
	printf("initial readstring = %s\n", readstring);

	// Read GPIO2 to confirm state of signals
	printf("\nGPIO2 After GPIO0 Write 0x00");
	print_gpio2_state(&xdma0);

	xdma_tty_poll(&xdma1);

	count = 0;
	while (xdma1.uart_rx_has_data)
	{

		// read RX DATA byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(&xdma1, UART_RX_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "D-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART data read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		readstring[count] = readbuffer[0];

		count++;

		xdma_tty_poll(&xdma1);
	}



	printf("\n\nRead count = %d bytes of data from XDMA1, readstring = %s\n", count, readstring);

	// Read GPIO2 to confirm state of signals
	printf("\nGPIO2 After XDMA1 Read");
	print_gpio2_state(&xdma0);

	print_debug_info(&xdma1);


	on_ctrl_backslash(EXIT_SUCCESS); // use CTRL-\ handler for exit and cleanup
}

