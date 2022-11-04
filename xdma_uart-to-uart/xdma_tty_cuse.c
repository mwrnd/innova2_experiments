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

TODO: - first character in a sequence and occasional random characters get lost

Notes:
 This software is a workaround to avoid using a proper
 driver for a PCIe XDMA AXI UART to behave as a TTY.
 This will NOT work with Xilinx's AXI UART Lite (pg142) as it generates an
 AXI Bus Error if the status byte is read when the UART Lite FIFO is empty.
 A UART design that does not generate any AXI errors is required.
 Interaction with the AXI UART is byte-wise and sequential. The full 16-byte
 address space of the UART is read and processed for each data byte read.
 Reading the status byte before a data byte causes data loss. The XDMA driver
 triggers the UART to decrement its ring buffer index.
 The code uses an interrupt called by an interval timer to get data from the
 AXI UART. Main complication is that the XDMA reads and writes cannot be
 interrupted or they will fail.
 Note the XDMA driver and this software cannot deal with system suspend.

 Set the interval timer alarm to go off often enough to capture UART baud rate
 115200 / 16  char buffer = 7200 --> 1/8000s = 0.000125s =  125us
 115200 / 512 char buffer = 225  -->  1/256s = 0.003910s = 3910us
 A call to the interrupt takes up to 4000usec to complete
 Setting INTERRUPT_INTERVAL to 4000 just about works on a 3.7GHz system
 Need to compile with --std=gnu1? instead of C11+ for expected timer operation

Prerequisites:
 - Xilinx XDMA to AXI design with a non-blocking UART with a 512 byte buffer
   Set fifo_ptr_bits 9
   github.com/eugene-tarassov/vivado-risc-v/blob/v3.3.0/uart/uart.v#L93
 - XDMA Drivers from github.com/xilinx/dma_ip_drivers
   Install Instructions at github.com/mwrnd/innova2_flex_xcku15p_notes
 - ringbuf.c and ringbuf.h from github.com/dhess/c-ringbuf
 - Install libfuse2 and its development library:
   sudo apt install libfuse2 libfuse-dev

Compile with (make sure the -I path resolves to Xilinx's dma_ip_drivers):

 gcc xdma_tty_cuse.c ringbuf.c --std=gnu11 -g -Wall -latomic  \
 `pkg-config fuse --cflags --libs` -I`pwd`/dma_ip_drivers/ -o xdma_tty_cuse

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
#include <fuse/cuse_lowlevel.h>
#include <fuse/fuse_opt.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>

#include "ringbuf.h"

#include "XDMA/linux-kernel/tools/dma_utils.c"


#define INTERRUPT_INTERVAL 4000


#define SIZE_DEFAULT (16)

#define UART_RX_ADDR_OFFSET    0x0
#define UART_TX_ADDR_OFFSET    0x4
#define UART_STAT_ADDR_OFFSET  0x8
#define UART_CTRL_ADDR_OFFSET  0xC

void on_ctrl_backslash(int sig);


struct _XDMA {
	atomic_int initialized;
	int  addr;
	int  fd_read;
	int  fd_wrte;
	char *c2h_name;
	char *h2c_name;
} xdma;


#define RINGBUF_SIZE 4096
ringbuf_t rb_inp;
ringbuf_t rb_out;


atomic_int xdma_rd_running;
atomic_int xdma_wr_running;
atomic_int tx_fifo_not_full;

volatile unsigned int xdma_read_errors = 0;
#define  MAX_XDMA_READ_ERRORS   16



struct uartread {
	uint8_t status;
	uint8_t datavalid;
	uint8_t data;
};



int read_xdma_uart(char *readdevname, int readdev_fd,
		uint32_t xdma_axi_address, struct uartread *ur, int debug)
{

	uint8_t uart_status = 0;
	uint8_t uart_rx_fifo_data_valid = 0;
	uint8_t uart_rx_fifo_full = 0;
	uint8_t uart_tx_fifo_empty = 0;
	uint8_t uart_tx_fifo_full = 0;
	uint8_t uart_clear_to_send = 0;

	ssize_t rc = 0;
	char buffer[SIZE_DEFAULT];


	if (debug)
		fprintf(stderr, "reading %d bytes from %s at address 0x%x\n",
			SIZE_DEFAULT, readdevname, xdma_axi_address);

	// Read All UART Registers (SIZE_DEFAULT=16) in one call to avoid
	// multiple increments of the UART's internal ring buffer index
	rc = read_to_buffer(readdevname, readdev_fd, buffer,
			SIZE_DEFAULT, xdma_axi_address);

	if (rc < 0) {
		fprintf(stderr, "XDMA read failed with rc=%ld\n", rc);
		xdma_read_errors++;
		ur->status = 0;
		ur->datavalid = 0;
		ur->data = 0;
		return rc;
	}

	if (rc < SIZE_DEFAULT) {
		fprintf(stderr, "underflow %ld/%d.\n", rc, SIZE_DEFAULT);
	}

	if (xdma_read_errors > MAX_XDMA_READ_ERRORS) {
		printf("\nSomething is wrong with the XDMA driver. ");
		printf("%d XDMA read errors encountered.\n", xdma_read_errors);
		on_ctrl_backslash(0); // exit
	}


	uart_status = (uint8_t)(buffer[UART_STAT_ADDR_OFFSET]);

	uart_rx_fifo_data_valid = (uart_status & 0x01) >> 0;
	uart_rx_fifo_full       = (uart_status & 0x02) >> 1;
	uart_tx_fifo_empty      = (uart_status & 0x04) >> 2;
	uart_tx_fifo_full       = (uart_status & 0x08) >> 3;
	uart_clear_to_send      = (uart_status & 0x10) >> 4;

	// return processed UART data and status in the passed uartread struct
	ur->status = uart_status;
	ur->datavalid = uart_rx_fifo_data_valid;
	ur->data = (uint8_t)(buffer[UART_RX_ADDR_OFFSET]);
	atomic_store(&tx_fifo_not_full, (!uart_tx_fifo_full));

	// print human-readable breakdown of the status byte and data
	if (debug) {
		if (uart_rx_fifo_data_valid) {
			printf("data = 0x%02x = %c ; ", ur->data, ur->data);
		}
		printf("uart_status = 0x%02x\n", uart_status);
		printf("\tBit0 = uart_rx_fifo_data_valid = %d\n",
				uart_rx_fifo_data_valid);
		printf("\tBit1 = uart_rx_fifo_full       = %d\n",
				uart_rx_fifo_full);
		printf("\tBit2 = uart_tx_fifo_empty      = %d\n",
				uart_tx_fifo_empty);
		printf("\tBit3 = uart_tx_fifo_full       = %d\n",
				uart_tx_fifo_full);
		printf("\tBit4 = uart_clear_to_send      = %d\n",
				uart_clear_to_send);
		printf("\n");
	}

	// return number of bytes that were successfully read
	return rc;
}




static void xdma_tty_ioctl(fuse_req_t req, int cmd, void *arg,
		struct fuse_file_info *fi, unsigned flags,
		const void *in_buf, size_t in_bufsz, size_t out_bufsz)
{

	unsigned int signals;

	if (atomic_load(&xdma_rd_running)) {
		printf("\nFAULT: xdma_tty_ioctl called during read interrupt.\n");
	}
	if (atomic_load(&xdma_wr_running)) {
		printf("\nFAULT: xdma_tty_ioctl called during write.\n");
	}


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
		if ((size_t)arg == TCIFLUSH) {
			printf("TCIFLUSH: Reset TTY-to-UART Ring Buffer");
			ringbuf_reset(rb_out);
		} else if ((size_t)arg == TCOFLUSH) {
			printf("TCOFLUSH: Reset UART-to-TTY Ring Buffer");
			ringbuf_reset(rb_inp);
		} else if ((size_t)arg == TCIOFLUSH){
			printf("TCIOFLUSH: Reset all Ring Buffers");
			ringbuf_reset(rb_inp);
			ringbuf_reset(rb_out);
		}
		printf("\n");
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
	xdma.fd_wrte = open(xdma.h2c_name, O_RDWR);

	if (xdma.fd_wrte < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n",
			xdma.h2c_name, xdma.fd_wrte);
		perror("open device");
	}

	xdma.fd_read = open(xdma.c2h_name, O_RDWR);

	if (xdma.fd_read < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n",
			xdma.c2h_name, xdma.fd_read);
		perror("open device");
	}

	fuse_reply_open(req, fi);

	atomic_store(&xdma.initialized, 1);
}




static void xdma_tty_poll(fuse_req_t req, struct fuse_file_info *fi,
		struct fuse_pollhandle *ph)
{
	unsigned int pollmask = 0;

	if (!ringbuf_is_empty(rb_out)) {
		pollmask |= POLLIN;
	}

	if (!ringbuf_is_full(rb_inp)) {
		pollmask |= POLLOUT;
	}

	fuse_reply_poll(req, pollmask);
}






struct timeval timestr;
struct timeval timeend;
struct sigaction sigactalrm;
volatile long int max_interrupt_interval = 0;
volatile long int max_interrupt_runtime = 0;
volatile long int min_interrupt_interval = 999999999;
volatile int timeend_valid = 0;
atomic_int interrupt_running;


static void read_from_device_interrupt(int signum)
{
	long int seconds = 0;
	long int microseconds = 0;
	long int elapsed_usec = 0;
	struct itimerval it_new;
	struct uartread ur;
	ssize_t rc;
	atomic_int valid_data_read;
	atomic_init(&valid_data_read, 0);

	if (atomic_load(&interrupt_running)) {
		// TODO: The read and/or write to XDMA has timed out
		printf("\nINTERRUPT GOT INTERRUPTED - XDMA Timed Out\n");
	}
	atomic_store(&interrupt_running, 1);


	// xdma_tty_write is running, try again later
	if (atomic_load(&xdma_wr_running)) { return; }


	// pause interrupt as this function is not designed to be reentrant
	// Default XDMA communication timeout is 10s so 5s is enough
	// of a delay to pause this interrupt and ascertain that
	// XDMA communication has timed out (this function runs again)
	it_new.it_value.tv_sec  = 5; 
	it_new.it_value.tv_usec = 0;
	it_new.it_interval.tv_sec  = 5;
	it_new.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &it_new, NULL);


	// if this signal handler has run at least once, log time between runs
	gettimeofday(&timestr, 0);
	if (timeend_valid) {
		seconds = timestr.tv_sec - timeend.tv_sec;
		microseconds = timestr.tv_usec - timeend.tv_usec;
		elapsed_usec = (seconds * 1000000) + microseconds;
		if (elapsed_usec > max_interrupt_interval) {
			max_interrupt_interval = elapsed_usec;
		}
		if ((elapsed_usec < min_interrupt_interval) && (elapsed_usec > 0)) {
			min_interrupt_interval = elapsed_usec;
		}
	}

	// read data from the XDMA UART (C2H)
	if (atomic_load(&xdma.initialized) && !atomic_load(&xdma_wr_running))
	{
		atomic_store(&xdma_rd_running, 1);


		rc = read_xdma_uart(xdma.c2h_name, xdma.fd_read, xdma.addr, &ur, 0);
		if (ur.datavalid && (!ringbuf_is_full(rb_out))) {
			// stderr is not line buffered so will print immediately
			//fprintf(stderr, "%c", ur.data);
			ringbuf_memcpy_into(rb_out, &(ur.data), 1);
			atomic_store(&valid_data_read, 1);
		}

		if (rc < 0) {
			; // TODO - XDMA read failed but can ignore
		}


		atomic_store(&xdma_rd_running, 0);
	}

	// measure runtime of the above XDMA read
	gettimeofday(&timeend, 0);
	seconds = timeend.tv_sec - timestr.tv_sec;
	microseconds = timeend.tv_usec - timestr.tv_usec;
	elapsed_usec = (seconds * 1000000) + microseconds;
	if ((elapsed_usec > max_interrupt_runtime) && atomic_load(&valid_data_read)) {
		max_interrupt_runtime = elapsed_usec;
	}
	timeend_valid = 1; // enable timing the interval between runs of this handler

	// restart this interval timer interrupt
	it_new.it_value.tv_sec  = 0;
	it_new.it_value.tv_usec = INTERRUPT_INTERVAL;
	it_new.it_interval.tv_sec  = 0;
	it_new.it_interval.tv_usec = INTERRUPT_INTERVAL;

	setitimer(ITIMER_REAL, &it_new, NULL);

	// re-enable the recurring XDMA AXI UART read interrupt
	sigemptyset(&sigactalrm.sa_mask);
	sigactalrm.sa_flags = (SA_NODEFER); // NO SA_RESTART
	sigactalrm.sa_handler = read_from_device_interrupt;
	sigaction(SIGALRM, &sigactalrm, NULL);

	atomic_store(&interrupt_running, 0);
}




static void xdma_tty_read(fuse_req_t req, size_t size,
		off_t off, struct fuse_file_info *fi)
{
	if (atomic_load(&xdma_rd_running)) {
		printf("\nFAULT: xdma_tty_read called during read interrupt.\n");
	}
	if (atomic_load(&xdma_wr_running)) {
		printf("\nFAULT: xdma_tty_read called during write.\n");
	}


	// copy as much data from the XDMA ring buffer as possible

	char*  buffer;
	size_t rb_out_used = ringbuf_bytes_used(rb_out);
	size_t count = 0;

	if(size < rb_out_used) {
		count = size;
	} else {
		count = rb_out_used;
	}

	buffer = malloc(count);
	ringbuf_memcpy_from(buffer, rb_out, count);
	fuse_reply_buf(req, buffer, count);
	free(buffer);
}




static void xdma_tty_write(fuse_req_t req, const char *buf, size_t size,
		off_t off, struct fuse_file_info *fi)
{
	if (atomic_load(&xdma_rd_running)) {
		printf("\nFAULT: xdma_tty_write called during read interrupt.\n");
	}
	if (atomic_load(&xdma_wr_running)) {
		printf("\nFAULT: xdma_tty_write called during write.\n");
	}


	// copy data from TTY to the XDMA UART ring buffer
	size_t rb_inp_free = ringbuf_bytes_free(rb_inp);
	size_t count = 0;

	if(size < rb_inp_free) {
		count = size;
	} else {
		count = rb_inp_free;
	}

	ringbuf_memcpy_into(rb_inp, buf, count);


	char buffer[SIZE_DEFAULT];
	int  write_count = 0;
	struct uartread ur;
	ssize_t rc = 0;
	struct itimerval it_new;


	// pause read interrupt as it is not designed to be reentrant
	// Default XDMA communication timeout is 10s so 5s is enough
	// of a delay to pause the read interrupt
	it_new.it_value.tv_sec  = 5; 
	it_new.it_value.tv_usec = 0;
	it_new.it_interval.tv_sec  = 5;
	it_new.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &it_new, NULL);


	// move data from the TTY ring buffer to the XDMA UART
	if (atomic_load(&xdma.initialized) && (!atomic_load(&xdma_rd_running)))
	{
		atomic_store(&xdma_wr_running, 1);


		// send data to the XDMA UART (H2C)
		for (int i = 0 ;  i < count  ; i++)
		{
			// this reads the status byte and sets tx_fifo_not_full global
			rc = read_xdma_uart(xdma.c2h_name, xdma.fd_read, xdma.addr, &ur, 0);
			// take care of any valid data
			if (ur.datavalid && (!ringbuf_is_full(rb_out)))
			{
				// stderr is not line buffered so will print immediately
				//fprintf(stderr, "%c", ur.data);
				ringbuf_memcpy_into(rb_out, &(ur.data), 1);
			}


			// write a byte to the XDMA UART
			if ((!ringbuf_is_empty(rb_inp)) && atomic_load(&tx_fifo_not_full))
			{
				// copy a byte from the TTY buffer
				ringbuf_memcpy_from(buffer, rb_inp, 1);

				// stderr is not line buffered; will print immediately
				//fprintf(stderr, "%c", buffer[0]);

				rc = write_from_buffer(xdma.h2c_name, xdma.fd_wrte,
					buffer, 1, (xdma.addr + UART_TX_ADDR_OFFSET));

				if (rc < 0) {
					printf("\nWrite to XDMA Failed!\n");
				} else {
					write_count++;
				}
			}

		}


		atomic_store(&xdma_wr_running, 0);
	}


	// restart the read interrupt interval timer
	it_new.it_value.tv_sec  = 0;
	it_new.it_value.tv_usec = INTERRUPT_INTERVAL;
	it_new.it_interval.tv_sec  = 0;
	it_new.it_interval.tv_usec = INTERRUPT_INTERVAL;

	setitimer(ITIMER_REAL, &it_new, NULL);

	// re-enable the recurring XDMA AXI UART read interrupt
	sigemptyset(&sigactalrm.sa_mask);
	sigactalrm.sa_flags = (SA_NODEFER); // NO SA_RESTART
	sigactalrm.sa_handler = read_from_device_interrupt;
	sigaction(SIGALRM, &sigactalrm, NULL);


	// TODO: What if count and loop_count are different?
	if (count != write_count) {
		printf("\nFAULT: xdma_tty_write XDMA write overflow\n");
		printf("  intended count=%ld, write_count=%d\n", count, write_count);
	}

	// reply to FUSE with the number of bytes copied to the TTY ring buffer
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
	printf("\nQuiting...");
	printf("\n(Min, Max) Interrupt Interval = (%ld, %ld) usec.", \
		min_interrupt_interval, max_interrupt_interval);
	printf("\nWith INTERRUPT_INTERVAL = %d, max runtime = %ld usec.\n",
		INTERRUPT_INTERVAL, max_interrupt_runtime);
	close(xdma.fd_wrte);
	close(xdma.fd_read);
	ringbuf_free(&rb_inp);
	ringbuf_free(&rb_out);
	free(xdma.c2h_name);
	free(xdma.h2c_name);
	// FUSE uses INT signal to run exit functions - see fuse_common.h
	raise(SIGINT);
	exit(EXIT_SUCCESS);
}




int main(int argc, char **argv)
{
	// Define two ring buffers, one for data that will be received from
	// the XDMA UART (rb_inp), and a second for data that will be sent
	// to the XDMA UART from the TTY terminal (rb_out)
	rb_inp = ringbuf_new(RINGBUF_SIZE - 1);
	rb_out = ringbuf_new(RINGBUF_SIZE - 1);


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

	atomic_init(&interrupt_running, 0);
	atomic_init(&xdma_rd_running, 0);
	atomic_init(&xdma_wr_running, 0);
	atomic_init(&xdma.initialized, 0);
	atomic_init(&tx_fifo_not_full, 0);
	xdma.fd_read = 0;
	xdma.fd_wrte = 0;
	xdma.addr = strtol(argv[3], NULL, 16);
	xdma.c2h_name = malloc(strlen(argv[1]));
	xdma.h2c_name = malloc(strlen(argv[2]));
	strcpy(xdma.c2h_name, argv[1]);
	strcpy(xdma.h2c_name, argv[2]);


	const char* dev_name[1];
	char* temp;
	char name[512];
	sprintf(name, "DEVNAME=%s", argv[4]);
	temp = malloc(strlen(name));
	strcpy(temp, name);
	dev_name[0] = temp;


  	struct cuse_info cinfo;
	memset(&cinfo, 0x00, sizeof(cinfo));
	cinfo.flags = CUSE_UNRESTRICTED_IOCTL;
	cinfo.dev_info_argc = 1;
	cinfo.dev_info_argv = dev_name;

	printf("XDMA AXI UART to TTY using CUSE ");
	printf("(FUSE char device in userspace)\n");
	printf("Press CTRL-\\ to quit.\n");
	printf("C2H Device: %s\n", xdma.c2h_name);
	printf("H2C Device: %s\n", xdma.h2c_name);
	printf("AXI Base Address: %s = %d\n", argv[3], xdma.addr);
	printf("%s\n", cinfo.dev_info_argv[0]);
	printf("TTY Name: /dev/%s\n", argv[4]);
	printf("\n");

	// Set an alarm to go off often enough to capture 115200 bps
	struct itimerval it_new;
	struct itimerval it_old;

	it_new.it_value.tv_sec  = 1; // delay first read for CUSE init
	it_new.it_value.tv_usec = 0;
	it_new.it_interval.tv_sec  = 1;
	it_new.it_interval.tv_usec = 0;

	setitimer(ITIMER_REAL, &it_new, &it_old);

	// enable the recurring XDMA AXI UART read interrupt
	// stackoverflow.com/questions/8903448/libfuse-exiting-fuse-session-loop
	sigemptyset(&sigactalrm.sa_mask);
	sigactalrm.sa_flags = (SA_NODEFER); // NO SA_RESTART
	sigactalrm.sa_handler = read_from_device_interrupt;
	sigaction(SIGALRM, &sigactalrm, NULL);


	// handle CTRL-\ as quit; CUSE is set to run in foreground, need exit
	struct sigaction sigactquit;

	sigemptyset(&sigactquit.sa_mask);
	sigactquit.sa_flags = (SA_NODEFER | SA_RESETHAND); // NO SA_RESTART
	sigactquit.sa_handler = on_ctrl_backslash;
	sigaction(SIGQUIT, &sigactquit, NULL);

	// -s is for single-threaded mode
	// -f to run in foreground and use current directory, stderr, stdout
	// -d to enable FUSE debugging
	// use argv[4] as internal FUSE name to allow multiple instances
	//const char* cuse_args[] = {"cuse", "-s", "-f"}; // 3 args
	const char* cuse_args[] = {argv[4], "-s", "-f"}; // 3 args
	return cuse_lowlevel_main(3, (char**) &cuse_args,
					&cinfo, &xdma_tty_clop, NULL);

	on_ctrl_backslash(EXIT_SUCCESS); // use CTRL-\ handler for exit and cleanup
}

