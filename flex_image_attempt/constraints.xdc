
# Nvidia Mellanox Innova-2 Flex Open Programmable SmartNIC
# Kintex UltraScale+ xcku15p-ffve1517-2-i
# https://www.nvidia.com/en-us/networking/ethernet/innova-2-flex/


# I2C to ConnectX-5 - Bank 90
set_property PACKAGE_PIN D1 [get_ports iic_rtl_0_sda_io]
set_property PACKAGE_PIN D2 [get_ports iic_rtl_0_scl_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_rtl_0_scl_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_rtl_0_sda_io]


# OpenCAPI I2C Pins being used as UART - B2 is SDA/RXD, C2 is SCL/TXD - Bank 90
set_property PACKAGE_PIN B2 [get_ports uart_rtl_0_rxd]
set_property PACKAGE_PIN C2 [get_ports uart_rtl_0_txd]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rtl_0_rxd]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rtl_0_txd]


# LEDs - A6=LED_D19, B6=LED_D18 - Bank 90
set_property PACKAGE_PIN A6 [get_ports LED_D19[0]]
set_property IOSTANDARD LVCMOS33 [get_ports LED_D19[0]]
set_property OFFCHIP_TERM NONE [get_ports LED_D19[0]]
set_property PACKAGE_PIN B6 [get_ports LED_D18[0]]
set_property IOSTANDARD LVCMOS33 [get_ports LED_D18[0]]
set_property OFFCHIP_TERM NONE [get_ports LED_D18[0]]


# F1 - _MAY_ interact with Flex Image - Bank 90
set_property PACKAGE_PIN F1 [get_ports IO_F1]
set_property IOSTANDARD LVCMOS33 [get_ports IO_F1]


# Differential System Clock 100MHz - Bank 65
set_property PACKAGE_PIN AR14 [get_ports {sys_clk_100MHz_clk_p[0]}]
set_property IOSTANDARD DIFF_SSTL12 [get_ports {sys_clk_100MHz_clk_p[0]}]
create_clock -period 10.000 -name sys_clk_100mhz [get_ports sys_clk_100MHz_clk_p]


# Secondary Quad SPI Configuration Flash - Bank 65
# Primary Quad SPI Configuration Flash pins are single-purpose in STARTUPE3
set_property PACKAGE_PIN AM12    [get_ports spi_rtl_0_io0_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_0_io0_io]
set_property PACKAGE_PIN AN12    [get_ports spi_rtl_0_io1_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_0_io1_io]
set_property PACKAGE_PIN AR13    [get_ports spi_rtl_0_io2_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_0_io2_io]
set_property PACKAGE_PIN AR12    [get_ports spi_rtl_0_io3_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_0_io3_io]
set_property PACKAGE_PIN AV11    [get_ports spi_rtl_0_ss_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_0_ss_io]


# PCIe - PCIE4 Location: X0Y2 - GTY Quads 127, 128
# GTY Quad 127 == Quad_X0Y0
# AH36=MGTYRXP0=CHANNEL_X0Y0, AG38=MGTYRXP1=CHANNEL_X0Y1,
# AF36=MGTYRXP2=CHANNEL_X0Y2, AE38=MGTYRXP3=CHANNEL_X0Y3
# GTY Quad 128 == Quad_X0Y1
# AD36=MGTYRXP0=CHANNEL_X0Y4, AC38=MGTYRXP1=CHANNEL_X0Y5,
# AB36=MGTYRXP2=CHANNEL_X0Y6, AA38=MGTYRXP3=CHANNEL_X0Y7
set_property PACKAGE_PIN AA38 [get_ports {pcie_7x_mgt_rtl_0_rxp[7]}]
set_property PACKAGE_PIN AB36 [get_ports {pcie_7x_mgt_rtl_0_rxp[6]}]
set_property PACKAGE_PIN AC38 [get_ports {pcie_7x_mgt_rtl_0_rxp[5]}]
set_property PACKAGE_PIN AD36 [get_ports {pcie_7x_mgt_rtl_0_rxp[4]}]
set_property PACKAGE_PIN AE38 [get_ports {pcie_7x_mgt_rtl_0_rxp[3]}]
set_property PACKAGE_PIN AF36 [get_ports {pcie_7x_mgt_rtl_0_rxp[2]}]
set_property PACKAGE_PIN AG38 [get_ports {pcie_7x_mgt_rtl_0_rxp[1]}]
set_property PACKAGE_PIN AH36 [get_ports {pcie_7x_mgt_rtl_0_rxp[0]}]

# PCIe Clock - AB27 is MGTREFCLK0 in GTY Quad/Bank 128 - X0Y1
set_property PACKAGE_PIN AB27 [get_ports {diff_clock_rtl_0_clk_p[0]}]
create_clock -name sys_clk -period 10.000 [get_ports diff_clock_rtl_0_clk_p]

# PCIe Reset - F2 is in Bank 90
set_property PACKAGE_PIN F2 [get_ports reset_rtl_0]
set_property IOSTANDARD LVCMOS33 [get_ports reset_rtl_0]
set_property PULLUP true [get_ports reset_rtl_0]
set_false_path -from [get_ports reset_rtl_0]


# Memory Configuration File Settings
set_property CONFIG_MODE SPIx8 [current_design]
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property CFGBVS GND [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 127.5 [current_design]
set_property BITSTREAM.CONFIG.SPI_32BIT_ADDR YES [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 8 [current_design]
set_property BITSTREAM.CONFIG.SPI_FALL_EDGE YES [current_design]
set_property BITSTREAM.CONFIG.CONFIGFALLBACK DISABLE [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLUP [current_design]
set_property BITSTREAM.CONFIG.OVERTEMPSHUTDOWN ENABLE [current_design]
set_property BITSTREAM.CONFIG.EXTMASTERCCLK_EN DISABLE [current_design]
set_property BITSTREAM.GENERAL.CRC ENABLE [current_design]
set_property BITSTREAM.CONFIG.NEXT_CONFIG_REBOOT DISABLE [current_design]

