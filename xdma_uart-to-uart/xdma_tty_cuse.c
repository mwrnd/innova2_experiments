/*****************************************************************************
 * XDMA AXI UART to TTY Bridge using CUSE (FUSE char device in userspace)    *
 *                                                                           *
 * BSD 2-Clause License                                                      *
 *                                                                           *
 * Copyright (c) 2022 Matthew Wielgus (mwrnd.github@gmail.com)               *
 * https://github.com/mwrnd/innova2_experiments/tree/main/xdma_uart-to-uart  *
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

TODO: - Can only send a maximum packet size of 4096 bytes, which is
        the Linux TTY internal buffer size.
        Send RAW File command ends in an error regardless of data length sent.


Notes:
 This software is a userspace driver for UART over XDMA. It allows a host
 computer with a Xilinx PCIe-based FPGA to communicate with UART modules
 implemented in the FPGA using standard Linux TTY tools (gtkterm, minicom).
 An AXI UART design that does not generate any AXI errors is required.
 This will NOT work with Xilinx's AXI UART Lite (pg142) as it generates an
 AXI Bus Error if the status byte is read when the UART Lite FIFO is empty.
 Interaction with the AXI UART is byte-wise and sequential. There is a lot of
 overhead as each byte read or written requires a STATUS byte read.
 Each byte write also requires a delay to allow for the symbol to be sent.
 Note the XDMA driver and this software cannot deal with system suspend.
 Compile with --std=gnu1x for Atomics and GNU Libc signal support.
 CUSE periodically calls the _poll function to determine if there are any
 receive bytes available and if there is room in the transmit FIFO. CUSE
 then calls _read and _write when and if appropriate.

Prerequisites:
 - Xilinx XDMA to AXI FPGA project with a non-blocking UART such as
   github.com/eugene-tarassov/vivado-risc-v/blob/v3.4.0/uart/uart.v
 - XDMA Drivers from github.com/xilinx/dma_ip_drivers
   Install Instructions at github.com/mwrnd/innova2_flex_xcku15p_notes
 - libfuse2 and its development library:
   sudo apt install libfuse2 libfuse-dev

Compile with:

 gcc  xdma_tty_cuse.c  `pkg-config fuse --cflags --libs`  \
 --std=gnu17  -g  -Wall  -latomic  -o xdma_tty_cuse

Run with:

 sudo ./xdma_tty_cuse  /dev/xdma0_c2h_0  /dev/xdma0_h2c_0  0x60100000 ttyCUSE0

In a second terminal:

 sudo gtkterm --port /dev/ttyCUSE0

If using with github.com/mwrnd/innova2_experiments/tree/main/xdma_uart-to-uart
run a second instance for loopback testing:

 sudo ./xdma_tty_cuse  /dev/xdma0_c2h_1  /dev/xdma0_h2c_1  0x60110000 ttyCUSE1
 sudo gtkterm --port /dev/ttyCUSE1

*/

#define FUSE_USE_VERSION  29
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <fuse/cuse_lowlevel.h>
#include <fuse/fuse_opt.h>
#include <poll.h>
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
// github.com/eugene-tarassov/vivado-risc-v/blob/v3.4.0/uart/uart.v
#define UART_RX_ADDR_OFFSET    0x0
#define UART_TX_ADDR_OFFSET    0x4
#define UART_STAT_ADDR_OFFSET  0x8
#define UART_CTRL_ADDR_OFFSET  0xC


// Need to wait the inter-symbol time between writes or data will be lost.
// In a driver the correct method would be to wait for tx_fifo_empty in the
// STATUS register to be true again which means the data was sent. However,
// that would not use the full TX buffer. Instead, wait long enough for a
// full FIFO buffer of data to transfer to catch all data.
// 115200 / 16  char buffer = 7200 --> 1/7200s = 0.000139s =  139us
// 115200 / 512 char buffer = 225  -->  1/256s = 0.003910s = 3910us
// 9600   / 16  char buffer = 600  -->  1/600s = 0.001667s = 1667us
#define INTERSYMBOL_DELAY	139


void on_ctrl_backslash(int sig);


struct _XDMA {
	atomic_int initialized;
	uint64_t  addr;
	int  fd_read;
	int  fd_wrte;
	char *c2h_name;
	char *h2c_name;
	volatile uint8_t uart_rx_has_data;
	volatile uint8_t uart_tx_fifo_not_full;
	volatile uint8_t uart_clear_to_send;
} xdma;


volatile unsigned int _xdma_read_errors  = 0;
#define  MAX_xdma_read_errors   16
volatile unsigned int _xdma_write_errors = 0;
#define  MAX_xdma_write_errors  16




ssize_t read_byte_from_xdma(uint64_t offset, char *buffer)
{
	ssize_t rc;
	char *buf = buffer;

	if (atomic_load(&xdma.initialized))
	{
		rc = pread(xdma.fd_read, buf, 1, (xdma.addr + offset));

		if (rc < 0) {
			fprintf(stderr, "%s, read byte @ 0x%lx failed %ld.\n",
				xdma.c2h_name, (xdma.addr + offset), rc);
			perror("File Read");
			return -EIO;
		}
	}

	return 1;
}




ssize_t write_byte_to_xdma(uint64_t offset, char *buffer)
{
	ssize_t rc;
	char *buf = buffer;

	if (atomic_load(&xdma.initialized))
	{
		rc = pwrite(xdma.fd_wrte, buf, 1, (xdma.addr + offset));
		fsync(xdma.fd_wrte);

		if (rc < 0) {
			fprintf(stderr, "%s, write byte @ 0x%lx failed %ld.\n",
				xdma.h2c_name, (xdma.addr + offset), rc);
			perror("write file");
			return -EIO;
		}
	}

	return 1;
}




void print_debug_info(void)
{
	uint8_t uart_status = 0;
	uint8_t uart_rx_fifo_data_valid = 0;
	uint8_t uart_rx_fifo_full = 0;
	uint8_t uart_tx_fifo_empty = 0;
	uint8_t uart_tx_fifo_full = 0;
	uint8_t uart_clear_to_send = 0;
	uint8_t uart_control = 0;
	uint8_t rx_irq_enabled = 0;
	uint8_t tx_irq_enabled = 0;
	uint8_t tx_stop = 0;
	ssize_t rc = 0;
	char buffer[1];

	if (atomic_load(&xdma.initialized))
	{
		// read STATUS byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(UART_STAT_ADDR_OFFSET, buffer);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read in debug ");
			fprintf(stderr, "failed with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		uart_status = (uint8_t)(buffer[0]);

		// refer to uart.v for STATUS byte bit offsets and definitions
		// github.com/eugene-tarassov/vivado-risc-v/blob/v3.4.0/uart/uart.v
		uart_rx_fifo_data_valid = (uart_status & 0x01) >> 0;
		uart_rx_fifo_full       = (uart_status & 0x02) >> 1;
		uart_tx_fifo_empty      = (uart_status & 0x04) >> 2;
		uart_tx_fifo_full       = (uart_status & 0x08) >> 3;
		uart_clear_to_send      = (uart_status & 0x10) >> 4;

		printf("\n\tuart_status = 0x%02x\n", uart_status);
		printf("\t\tBit0 = uart_rx_fifo_data_valid = %d\n",
				uart_rx_fifo_data_valid);
		printf("\t\tBit1 = uart_rx_fifo_full       = %d\n",
				uart_rx_fifo_full);
		printf("\t\tBit2 = uart_tx_fifo_empty      = %d\n",
				uart_tx_fifo_empty);
		printf("\t\tBit3 = uart_tx_fifo_full       = %d\n",
				uart_tx_fifo_full);
		printf("\t\tBit4 = uart_clear_to_send      = %d\n",
				uart_clear_to_send);

		xdma.uart_rx_has_data = uart_rx_fifo_data_valid;
		//xdma.uart_tx_fifo_not_full = !uart_tx_fifo_full;
		xdma.uart_tx_fifo_not_full =
				!((uart_status & 0x08) >> 3);

		printf("\txdma.uart_rx_has_data = %d\n", xdma.uart_rx_has_data);
		printf("\txdma.uart_tx_fifo_not_full = %d\n",
			xdma.uart_tx_fifo_not_full);


		// read CONTROL byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(UART_CTRL_ADDR_OFFSET, buffer);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read in debug ");
			fprintf(stderr, "failed with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		uart_control = (uint8_t)(buffer[0]);

		rx_irq_enabled = (uart_control & 0x10) >> 4;
		tx_irq_enabled = (uart_control & 0x20) >> 5;
		tx_stop        = (uart_control & 0x40) >> 6;

		printf("\n\tuart_control = 0x%02x\n", uart_control);
		printf("\t\tBit4 = rx_irq_enabled = %d\n",
				rx_irq_enabled);
		printf("\t\tBit5 = tx_irq_enabled = %d\n",
				tx_irq_enabled);
		printf("\t\tBit6 = tx_stop        = %d\n",
				tx_stop);


		printf("\txdma.fd_read = %d, xdma.fd_wrte = %d",
			xdma.fd_read, xdma.fd_wrte);

		printf("\n\n");
	}
}




static void xdma_tty_ioctl(fuse_req_t req, int cmd, void *arg,
		struct fuse_file_info *fi, unsigned flags,
		const void *in_buf, size_t in_bufsz, size_t out_bufsz)
{

	unsigned int signals;
	char buf[1];

	if (flags & FUSE_IOCTL_COMPAT) {
		fuse_reply_err(req, ENOSYS);
		return;
	}

	// refer to LDD3 tiny_tty.c  and  man 3 termios
	// B115200 | CS8 | CREAD = Enable 8-Bit byte Receiver at 115200 baud
	// CLOCAL = Ignore modem control lines
	// IGNPAR = Ignore framing errors
	// IGNBRK = Ignore BREAK condition on input
	// IGNCR  = Ignore carriage return on input
	static struct termios termios_settings;
	termios_settings.c_cflag = B115200 | CS8 | CREAD | CLOCAL;
	termios_settings.c_lflag = 0;
	termios_settings.c_iflag = IGNPAR | IGNBRK | IGNCR;
	termios_settings.c_oflag = 0;


	// refer to linux-stable/include/uapi/asm-generic/ioctls.h
	switch (cmd)
	{
	case TCGETS:
		if(out_bufsz) {
			printf("Setting termios\n");
			fuse_reply_ioctl(req, 0, &termios_settings,
				sizeof(struct termio));
		} else {
			struct iovec iov = { arg, sizeof(struct termio) };
			fuse_reply_ioctl_retry(req , NULL, 0, &iov, 1);
		}
		return;

	case TCFLSH:
		if (atomic_load(&xdma.initialized))
		{
			if ((size_t)arg == TCIFLUSH) {
				printf("TCIFLUSH: Reset XDMA UART RX FIFO\n");
				buf[0] = 1; // Bit0=1 resets RX FIFO
				write_byte_to_xdma(UART_CTRL_ADDR_OFFSET, buf);
			} else if ((size_t)arg == TCOFLUSH) {
				printf("TCOFLUSH: Reset XDMA UART TX FIFO\n");
				buf[0] = 2; // Bit1=1 resets TX FIFO
				write_byte_to_xdma(UART_CTRL_ADDR_OFFSET, buf);
			} else if ((size_t)arg == TCIOFLUSH) {
				printf("TCIOFLUSH: Reset XDMA UART FIFOs\n");
				buf[0] = 3; // Bit0=1, Bit1=1 resets both
				write_byte_to_xdma(UART_CTRL_ADDR_OFFSET, buf);
			}
		}
		fuse_reply_ioctl(req, 0, 0, 0);
		return;

	case TIOCMGET:
		// Only RX and TX signals are used
		signals = 0;
		if(out_bufsz){
			fuse_reply_ioctl(req, 0, &signals, sizeof(unsigned int));
		} else {
			struct iovec iov = { arg, sizeof(unsigned int) };
			fuse_reply_ioctl_retry(req , 0, 0, &iov, 1);
		}
		return;

	case TCSETS:
	case TCSETSW:
	case TCSETSF:
	case TIOCMBIS:
	case TIOCMSET:
	case TCSBRK:
		// these IOCTLs are valid but have no effect for an XDMA UART
		fuse_reply_ioctl(req, 0, 0, 0);
		return;

	default:
		printf("\nxdma_tty_ioctl: Unhandled cmd = 0x%x\n", cmd);
		fuse_reply_err(req, ENOSYS); // Function Not Implemented
		//fuse_reply_err(req, EINVAL); // Invalid Argument - cusexmp.c
	}

	return;
}




static void xdma_tty_release(fuse_req_t req, struct fuse_file_info *fi)
{
	atomic_store(&xdma.initialized, 0);
	fuse_reply_err(req, 0);
	close(xdma.fd_wrte);
	close(xdma.fd_read);
	printf("\nClosed TTY\n");
}




static void xdma_tty_open(fuse_req_t req, struct fuse_file_info *fi)
{

	xdma.fd_wrte = open(xdma.h2c_name, O_WRONLY);

	if (xdma.fd_wrte < 0) {
		fprintf(stderr, "unable to open write device %s, %d.\n",
			xdma.h2c_name, xdma.fd_wrte);
		perror("open device");
	}

	xdma.fd_read = open(xdma.c2h_name, O_RDONLY);

	if (xdma.fd_read < 0) {
		fprintf(stderr, "unable to open read device %s, %d.\n",
			xdma.c2h_name, xdma.fd_read);
		perror("open device");
	}

	atomic_store(&xdma.initialized, 1);

	fuse_reply_open(req, fi);
}




struct timeval polltimestr;
struct timeval polltimeend;
volatile long int max_poll_interval = 0;
volatile long int min_poll_interval = 999999999;
volatile int polltimeend_valid = 0;
volatile long int max_poll_runtime = 0;


static void xdma_tty_poll(fuse_req_t req, struct fuse_file_info *fi,
		struct fuse_pollhandle *ph)
{
	unsigned int pollmask = 0;
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
	if (atomic_load(&xdma.initialized))
	{

		rc = read_byte_from_xdma(UART_STAT_ADDR_OFFSET, buffer);

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
		// github.com/eugene-tarassov/vivado-risc-v/blob/v3.4.0/uart/uart.v
		uart_rx_fifo_data_valid = (uart_status & 0x01) >> 0;
		//uart_rx_fifo_full       = (uart_status & 0x02) >> 1;
		//uart_tx_fifo_empty      = (uart_status & 0x04) >> 2;
		uart_tx_fifo_full       = (uart_status & 0x08) >> 3;
		uart_clear_to_send      = (uart_status & 0x10) >> 4;

		xdma.uart_rx_has_data = uart_rx_fifo_data_valid;
		xdma.uart_tx_fifo_not_full = !uart_tx_fifo_full;
		xdma.uart_clear_to_send = uart_clear_to_send;

		// Let CUSE know there is data in the XDMA UART RX FIFO
		if (xdma.uart_rx_has_data) {
			pollmask |= POLLIN;
		}

		// If the UART TX FIFO is not full and Clear-To-Send
		// CUSE will be able to call _write when it has data ready
		if ((xdma.uart_tx_fifo_not_full) && (xdma.uart_clear_to_send)) {
			pollmask |= POLLOUT;
		}

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

	fuse_reply_poll(req, pollmask);
}




static void xdma_tty_read(fuse_req_t req, size_t size,
		off_t off, struct fuse_file_info *fi)
{
	int count = 0;
	char readbuffer[1];
	ssize_t rc = 0;
	char*  buffer;
	buffer = malloc((size+1));

	if (!buffer) { printf("malloc failed in read"); on_ctrl_backslash(1); }

	// if _read is running, then _poll has read the STATUS byte
	// and there is data in the UART FIFO
	while (atomic_load(&xdma.initialized) &&
		(xdma.uart_rx_has_data) && (count < size))
	{
		// read RX DATA byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(UART_RX_ADDR_OFFSET, readbuffer);

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
		rc = read_byte_from_xdma(UART_STAT_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "S-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		xdma.uart_rx_has_data      =
				 ((((uint8_t)(readbuffer[0])) & 0x01) >> 0);
		xdma.uart_tx_fifo_not_full =
				!((((uint8_t)(readbuffer[0])) & 0x08) >> 3);

	}

	// return the number of bytes successfully read from the UART FIFO
	fuse_reply_buf(req, buffer, count);
	free(buffer);
}




static void xdma_tty_write(fuse_req_t req, const char *buf, size_t size,
		off_t off, struct fuse_file_info *fi)
{
	int count = 0;
	char readbuffer[1];
	char writebuffer[1];
	ssize_t rc = 0;


	if (atomic_load(&xdma.initialized))
	{
		// before writing, confirm TX can be written to
		// read STATUS byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(UART_STAT_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "S-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		xdma.uart_rx_has_data      =
				 ((((uint8_t)(readbuffer[0])) & 0x01) >> 0);
		xdma.uart_tx_fifo_not_full =
				!((((uint8_t)(readbuffer[0])) & 0x08) >> 3);
	}


	// if _write is running, then _poll has read the STATUS byte
	// and there is room for data in the UART FIFO
	while (atomic_load(&xdma.initialized) &&
		(xdma.uart_tx_fifo_not_full) && (count <= size))
	{
		// write TX DATA byte to the XDMA UART (H2C)

		writebuffer[0] = buf[count];
		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "W-%c-", buf[count]);

		rc = write_byte_to_xdma(UART_TX_ADDR_OFFSET, writebuffer);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART write failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_write_errors++;
		}

		// Need to wait the inter-symbol time between writes or data
		// will be lost. In a driver the correct method would be to
		// wait for tx_fifo_empty in the STATUS register to be true
		// again which means the data was sent. However, that would
		// lock up this function and not use the full TX buffer.
		// Instead, the while loop checks the FIFO buffer is not full.
		usleep(INTERSYMBOL_DELAY);

		count++;


		// read STATUS byte from the XDMA UART (C2H)
		rc = read_byte_from_xdma(UART_STAT_ADDR_OFFSET, readbuffer);

		// stderr is not line buffered; will print immediately
		//fprintf(stderr, "S-%02x-", readbuffer[0]);

		if (rc < 0) {
			fprintf(stderr, "\nXDMA UART status read failed ");
			fprintf(stderr, "with rc=%ld\n", rc);
			_xdma_read_errors++;
		}

		xdma.uart_rx_has_data      =
				 ((((uint8_t)(readbuffer[0])) & 0x01) >> 0);
		xdma.uart_tx_fifo_not_full =
				!((((uint8_t)(readbuffer[0])) & 0x08) >> 3);
	}


	if (_xdma_write_errors > MAX_xdma_write_errors) {
		printf("\nSomething is wrong with the XDMA driver. ");
		printf("%d XDMA write errors encountered.\n", _xdma_write_errors);
		on_ctrl_backslash(0); // exit
	}


	// reply to FUSE with number of bytes written to the XDMA UART
	fuse_reply_write(req, count);
}




static const struct cuse_lowlevel_ops xdma_tty_clop = {
	.open    = xdma_tty_open,
	.read    = xdma_tty_read,
	.write   = xdma_tty_write,
	.ioctl   = xdma_tty_ioctl,
	.release = xdma_tty_release,
	.poll    = xdma_tty_poll
};




void on_ctrl_backslash(int sig)
{
	// also used as cleanup function
	atomic_store(&xdma.initialized, 0);
	if (sig == SIGQUIT) {
		printf("\nCTRL-\\ (SIGQUIT) Pressed.");
	}
	printf("\nQuitting...");
	printf("\nXDMA: Read Errors = %d, Write Errors = %d",
		_xdma_read_errors, _xdma_write_errors);
	printf("\n(Min, Max) Poll Interval = (%ld, %ld) usec and ",
		min_poll_interval, max_poll_interval);
	printf("max run time = %ld usec\n", max_poll_runtime);
	if(xdma.fd_wrte)  close(xdma.fd_wrte);
	if(xdma.fd_read)  close(xdma.fd_read);
	if(xdma.c2h_name)  free(xdma.c2h_name);
	if(xdma.h2c_name)  free(xdma.h2c_name);
	// FUSE uses INT signal to run exit functions - see fuse_common.h
	raise(SIGINT);
	exit(EXIT_SUCCESS);
}




int main(int argc, char **argv)
{

	if (argc != 5)
	{
		printf("%s is a bridge between a host ", argv[0]);
		printf("TTY and an FPGA XDMA AXI UART.\n");
		printf("Press CTRL-\\ to quit.\n");
		printf("Usage:\n");
		printf("  sudo %s C2H_DEVICE_NAME  ", argv[0]);
		printf("H2C_DEVICE_NAME  AXI_BASE_ADDR TTY_NAME\n");
		printf("Example:\n");
		printf("  sudo %s /dev/xdma0_c2h_0 ", argv[0]);
		printf("/dev/xdma0_h2c_0 0x60100000    ttyCUSE0\n");
		printf("\n");
		exit(EXIT_FAILURE);
	}


	// TODO - sprintf and strcpy are unsafe for use
	// with unsanitized and/or untrusted inputs

	atomic_init(&xdma.initialized, 0);
	xdma.fd_read = 0;
	xdma.fd_wrte = 0;
	xdma.uart_rx_has_data = 0;
	xdma.uart_tx_fifo_not_full = 0;
	xdma.addr = (uint64_t)strtol(argv[3], NULL, 16);
	xdma.c2h_name = malloc(strlen(argv[1]));
	if (!(xdma.c2h_name)) { printf("malloc failed in main"); exit(-1); }
	xdma.h2c_name = malloc(strlen(argv[2]));
	if (!(xdma.h2c_name)) { printf("malloc failed in main"); exit(-1); }
	strcpy(xdma.c2h_name, argv[1]);
	strcpy(xdma.h2c_name, argv[2]);


	const char* dev_name[1];
	char* temp;
	int rc = 0;
	char name[512];
	sprintf(name, "DEVNAME=%s", argv[4]);
	temp = malloc(strlen(name));
	if (!temp) { printf("malloc failed in main"); exit(-1); }
	strcpy(temp, name);
	dev_name[0] = temp;


  	struct cuse_info cinfo;
	memset(&cinfo, 0x00, sizeof(cinfo));
	cinfo.flags = CUSE_UNRESTRICTED_IOCTL; // allows TTY commands
	cinfo.dev_info_argc = 1;
	cinfo.dev_info_argv = dev_name;

	printf("XDMA AXI UART to TTY using CUSE ");
	printf("(FUSE char device in userspace)\n");
	printf("Press CTRL-\\ to quit.\n");
	printf("Card-To-Host Device: %s\n", xdma.c2h_name);
	printf("Host-To-Card Device: %s\n", xdma.h2c_name);
	printf("AXI UART Base Address: %s = 0x%lX = %ld\n",
		argv[3], xdma.addr, xdma.addr);
	printf("%s\n", cinfo.dev_info_argv[0]);
	printf("TTY Name: /dev/%s\n", argv[4]);
	printf("Delay between UART TX writes = %d us\n", INTERSYMBOL_DELAY);
	printf("Maximum Baud Rate ~= %d", ((16*1000000)/INTERSYMBOL_DELAY));
	printf(" baud if the UART FIFO has a 16 byte depth.\n");
	printf("Note (1/(115200baud/16char_FIFO)) = 0.000139s = 139us\n");
	printf("\n");


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


	// Run FUSE/CUSE in single-threaded mode as the XDMA driver cannot
	// handle simultaneous AXI read/write operations
	// -s is for single-threaded mode
	// -f to run in foreground and use current directory, stderr, stdout
	// -d to enable FUSE debugging
	// use argv[4] as internal FUSE name
	//const char* cuse_args[] = {"cuse", "-s", "-f"}; // 3 args
	const char* cuse_args[] = {argv[4], "-s", "-f"}; // 3 args
	return cuse_lowlevel_main(3, (char**) &cuse_args,
					&cinfo, &xdma_tty_clop, NULL);

	on_ctrl_backslash(EXIT_SUCCESS); // use CTRL-\ handler for exit and cleanup
}

