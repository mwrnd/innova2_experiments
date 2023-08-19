//Copyright 1986-2021 Xilinx, Inc. All Rights Reserved.
//--------------------------------------------------------------------------------
//Tool Version: Vivado v.2021.1 (lin64) Build 3247384 Thu Jun 10 19:36:07 MDT 2021
//Date        : Fri Aug 18 14:49:30 2023
//Host        : Host running 64-bit Ubuntu 20.04.6 LTS
//Command     : generate_target qdma_wrapper.bd
//Design      : qdma_wrapper
//Purpose     : IP block netlist
//--------------------------------------------------------------------------------
`timescale 1 ps / 1 ps

module qdma_wrapper
   (Dout_0,
    diff_clock_rtl_0_clk_n,
    diff_clock_rtl_0_clk_p,
    gpio_io_o_0,
    pcie_7x_mgt_rtl_0_rxn,
    pcie_7x_mgt_rtl_0_rxp,
    pcie_7x_mgt_rtl_0_txn,
    pcie_7x_mgt_rtl_0_txp,
    reset_rtl_0);
  output [0:0]Dout_0;
  input [0:0]diff_clock_rtl_0_clk_n;
  input [0:0]diff_clock_rtl_0_clk_p;
  output [0:0]gpio_io_o_0;
  input [7:0]pcie_7x_mgt_rtl_0_rxn;
  input [7:0]pcie_7x_mgt_rtl_0_rxp;
  output [7:0]pcie_7x_mgt_rtl_0_txn;
  output [7:0]pcie_7x_mgt_rtl_0_txp;
  input reset_rtl_0;

  wire [0:0]Dout_0;
  wire [0:0]diff_clock_rtl_0_clk_n;
  wire [0:0]diff_clock_rtl_0_clk_p;
  wire [0:0]gpio_io_o_0;
  wire [7:0]pcie_7x_mgt_rtl_0_rxn;
  wire [7:0]pcie_7x_mgt_rtl_0_rxp;
  wire [7:0]pcie_7x_mgt_rtl_0_txn;
  wire [7:0]pcie_7x_mgt_rtl_0_txp;
  wire reset_rtl_0;

  qdma qdma_i
       (.Dout_0(Dout_0),
        .diff_clock_rtl_0_clk_n(diff_clock_rtl_0_clk_n),
        .diff_clock_rtl_0_clk_p(diff_clock_rtl_0_clk_p),
        .gpio_io_o_0(gpio_io_o_0),
        .pcie_7x_mgt_rtl_0_rxn(pcie_7x_mgt_rtl_0_rxn),
        .pcie_7x_mgt_rtl_0_rxp(pcie_7x_mgt_rtl_0_rxp),
        .pcie_7x_mgt_rtl_0_txn(pcie_7x_mgt_rtl_0_txn),
        .pcie_7x_mgt_rtl_0_txp(pcie_7x_mgt_rtl_0_txp),
        .reset_rtl_0(reset_rtl_0));
endmodule
