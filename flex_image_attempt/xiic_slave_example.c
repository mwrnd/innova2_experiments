
/* SPDX-License-Identifier: MIT
 * Copyright (C) 2006 - 2021 Xilinx, Inc.  All rights reserved.
 * Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
 * Copyright (c) 2024 Matthew Wielgus (github.com/mwrnd)

   This is code for a MicroBlaze Soft Processor that behaves like the I2C
   interface of the Innova-2 Flex Image to enable the ConnectX-5 to have
   the same communication sequence.
   See ProcessBuffers function for more information.
*/

#include "xparameters.h"
#include "xiic.h"
#include "xiic_i.h"
#include "xintc.h"
#include "xinterrupt_wrap.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xil_types.h"



#define XIIC_BASEADDRESS         XPAR_XIIC_0_BASEADDR

//Address 0x40 is 0x80 as an 8 bit number
#define SLAVE_ADDRESS            0x40

#define RECEIVE_COUNT            8
#define SEND_COUNT               4



int SlaveWriteData(u16 ByteCount);
int SlaveReadData(u8 *BufferPtr, u16 ByteCount);
static void StatusHandler(XIic *InstancePtr, int Event);
static void SendHandler(XIic *InstancePtr);
static void ReceiveHandler(XIic *InstancePtr);
void ProcessBuffers();



XIic IicInstance;

u8 WriteBuffer[SEND_COUNT];
u8 ReadBuffer[RECEIVE_COUNT];

volatile u8 TransmitComplete;
volatile u8 ReceiveComplete;

volatile u8 SlaveRead;
volatile u8 SlaveWrite;



int main(void)
{
    int Status;
    u8 Index;
    XIic_Config *ConfigPtr;

    ConfigPtr = XIic_LookupConfig(XIIC_BASEADDRESS);

    if (ConfigPtr == NULL) {
        xil_printf("ERROR: Could not find IIC\r\n");
        return XST_FAILURE;
    }

    Status = XIic_CfgInitialize(&IicInstance, ConfigPtr, ConfigPtr->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Could not initialize IIC\r\n");
        return XST_FAILURE;
    }

    Status = XSetupInterruptSystem(&IicInstance, &XIic_InterruptHandler,
                    ConfigPtr->IntrId, ConfigPtr->IntrParent,
                    XINTERRUPT_DEFAULT_PRIORITY);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Could not set up AXI Interrupt Controller\r\n");
        return XST_FAILURE;
    }

    XIic_SlaveInclude();

    XIic_SetStatusHandler(&IicInstance, &IicInstance,
                    (XIic_StatusHandler) StatusHandler);
    XIic_SetSendHandler(&IicInstance, &IicInstance,
                    (XIic_Handler) SendHandler);
    XIic_SetRecvHandler(&IicInstance, &IicInstance,
                    (XIic_Handler) ReceiveHandler);

    Status = XIic_SetAddress(&IicInstance, XII_ADDR_TO_RESPOND_TYPE,
                    SLAVE_ADDRESS);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Could not set IIC Address\r\n");
        return XST_FAILURE;
    }

    xil_printf("\r\nStarted Innova-2 Flex Image I2C Interface\r\n");

    while (1) {
        // wait for a read request
        SlaveReadData(ReadBuffer, RECEIVE_COUNT);
    }


    xil_printf("ERROR: IIC Exited\r\n");
    return XST_FAILURE;
}



void ProcessBuffers()
{
    // The following is a sorted and uniq'ed I2C sniffer log of the
    // Innova-2 ConnectX-5 to Flex Image communication to be replicated
    // Address is 0x80 + R/W
    // s = START CONDITION
    // p = STOP CONDITION
    // a = ACK DETECTED
    // n = NAK DETECTED
    // s80a00a00a00a00a00a00a00a24as81a00a00a00a00np
    // s80a00a00a00a00a00a90a00a00as81a00a00a00aC1np
    // s80a00a00a00a00a00a90a00a04as81a27a11a20a18np
    // s80a00a00a00a00a00a90a00a08as81a00a11a56a32np
    // s80a00a00a00a00a00a90a00a0Cas81a00a00a00a04np
    // s80a00a00a00a00a00a90a00a10as81a00a00a00a0Anp
    // s80a00a00a00a00a00a90a00a6Cas81a00a00a00a02np
    // s80a8BaADaF0a0Da8BaADaF0a0Das81a00a00a00a00np
    // s80a8BaADaF0a0Da8BaADaF0a11as81a00a00a00a00np
    // s80a8BaADaF0a0Da8BaADaF0a15as81a00a00a00a00np

    if (ReadBuffer[5] == 0x90)
    {
        if (ReadBuffer[7] == 0x00)
        {
            WriteBuffer[0] = 0x00;
            WriteBuffer[1] = 0x00;
            WriteBuffer[2] = 0x00;
            WriteBuffer[3] = 0xC1;
        }
        else if (ReadBuffer[7] == 0x04)
        {
            WriteBuffer[0] = 0x27;
            WriteBuffer[1] = 0x11;
            WriteBuffer[2] = 0x20;
            WriteBuffer[3] = 0x18;
        }
        else if (ReadBuffer[7] == 0x08)
        {
            WriteBuffer[0] = 0x00;
            WriteBuffer[1] = 0x11;
            WriteBuffer[2] = 0x56;
            WriteBuffer[3] = 0x32;
        }
        else if (ReadBuffer[7] == 0x0C)
        {
            WriteBuffer[0] = 0x00;
            WriteBuffer[1] = 0x00;
            WriteBuffer[2] = 0x00;
            WriteBuffer[3] = 0x04;
        }
        else if (ReadBuffer[7] == 0x10)
        {
            WriteBuffer[0] = 0x00;
            WriteBuffer[1] = 0x00;
            WriteBuffer[2] = 0x00;
            WriteBuffer[3] = 0x0A;
        }
        else if (ReadBuffer[7] == 0x6C)
        {
            WriteBuffer[0] = 0x00;
            WriteBuffer[1] = 0x00;
            WriteBuffer[2] = 0x00;
            WriteBuffer[3] = 0x02;
        }
        else if ((ReadBuffer[7] > 0x13) && (ReadBuffer[7] < 0x35))
        {
            // Addresses 0x00900014 to 0x00900034
            // return 0x8BADF00D
            WriteBuffer[0] = 0x8B;
            WriteBuffer[1] = 0xAD;
            WriteBuffer[2] = 0xF0;
            WriteBuffer[3] = 0x0D;
        }
        else
        {
            WriteBuffer[0] = 0x00;
            WriteBuffer[1] = 0x00;
            WriteBuffer[2] = 0x00;
            WriteBuffer[3] = 0x00;
        }

    }
    else if ((ReadBuffer[5] == 0x00) && (ReadBuffer[7] == 0x24))
    {
        WriteBuffer[0] = 0x00;
        WriteBuffer[1] = 0x00;
        WriteBuffer[2] = 0x00;
        WriteBuffer[3] = 0x00;
    }
    else
    {
        // Addresses 0x8BADF0xx and all
        // others return 0x00000000
        WriteBuffer[0] = 0x00;
        WriteBuffer[1] = 0x00;
        WriteBuffer[2] = 0x00;
        WriteBuffer[3] = 0x00;
    }

}



// Write I2C Data when a read operation has been initiated
int SlaveWriteData(u16 ByteCount)
{
    int Status;

    TransmitComplete = 1;

    Status = XIic_Start(&IicInstance);
    if (Status != XST_SUCCESS) { return XST_FAILURE; }

    XIic_IntrGlobalEnable(IicInstance.BaseAddress);

    // Wait for Addresses-As-Slave Interrupt and Transmission to Complete
    while ((TransmitComplete) || (XIic_IsIicBusy(&IicInstance) == TRUE)) {
        if (SlaveWrite) {
            XIic_SlaveSend(&IicInstance, WriteBuffer, SEND_COUNT);
            SlaveWrite = 0;
            // TODO: Why is TransmitComplete not being cleared in time by SendHandler?
            TransmitComplete = 0;
        }
    }

    XIic_IntrGlobalDisable(IicInstance.BaseAddress);

    Status = XIic_Stop(&IicInstance);
    if (Status != XST_SUCCESS) { return XST_FAILURE; }

    return XST_SUCCESS;
}



// Read I2C Data when a write operation has been initiated
int SlaveReadData(u8 *BufferPtr, u16 ByteCount)
{
    int Status;

    ReceiveComplete = 1;

    Status = XIic_Start(&IicInstance);
    if (Status != XST_SUCCESS) { return XST_FAILURE; }

    XIic_IntrGlobalEnable(IicInstance.BaseAddress);
    XIic_SetOptions(&IicInstance, XII_REPEATED_START_OPTION);

    while ((ReceiveComplete) || (XIic_IsIicBusy(&IicInstance) == TRUE)) {
        if (SlaveRead) {
            XIic_SlaveRecv(&IicInstance, ReadBuffer, RECEIVE_COUNT);
            SlaveRead = 0;
        }
    }

    XIic_IntrGlobalDisable(IicInstance.BaseAddress);

    // DO NOT call Stop as that will send a STOP and end current response,
    // whereas the goal is Repeated Starts
    // Status = XIic_Stop(&IicInstance);
    // if (Status != XST_SUCCESS) { return XST_FAILURE; }

    XIic_SetOptions(&IicInstance, XII_REPEATED_START_OPTION);

    return XST_SUCCESS;
}



// Callback (from library interrupt) that indicates current I2C event
static void StatusHandler(XIic *InstancePtr, int Event)
{
    if (Event == XII_MASTER_WRITE_EVENT) {
        SlaveRead = 1;
    } else if (Event == XII_MASTER_READ_EVENT) {
        SlaveWrite = 1;
        SlaveWriteData(SEND_COUNT);
    } else {
        // XII_GENERAL_CALL_EVENT or Error
    }
}



// Callback (from library interrupt) that is run after data is sent
static void SendHandler(XIic *InstancePtr)
{
    TransmitComplete = 0;
}



// Callback (from library interrupt) that is run after data is received
static void ReceiveHandler(XIic *InstancePtr)
{
    ReceiveComplete = 0;
    ProcessBuffers();
}
