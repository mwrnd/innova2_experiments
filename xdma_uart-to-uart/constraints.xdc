



# PCIe

set_property PACKAGE_PIN AA38 [get_ports {pcie_7x_mgt_rtl_0_rxp[7]}]
set_property PACKAGE_PIN AB36 [get_ports {pcie_7x_mgt_rtl_0_rxp[6]}]
set_property PACKAGE_PIN AC38 [get_ports {pcie_7x_mgt_rtl_0_rxp[5]}]
set_property PACKAGE_PIN AD36 [get_ports {pcie_7x_mgt_rtl_0_rxp[4]}]
set_property PACKAGE_PIN AE38 [get_ports {pcie_7x_mgt_rtl_0_rxp[3]}]
set_property PACKAGE_PIN AF36 [get_ports {pcie_7x_mgt_rtl_0_rxp[2]}]
set_property PACKAGE_PIN AG38 [get_ports {pcie_7x_mgt_rtl_0_rxp[1]}]
set_property PACKAGE_PIN AH36 [get_ports {pcie_7x_mgt_rtl_0_rxp[0]}]

set_property PACKAGE_PIN AB27 [get_ports {diff_clock_rtl_0_clk_p[0]}]
create_clock -name sys_clk -period 10.000 [get_ports diff_clock_rtl_0_clk_p]

# PCIe Reset - Active High
set_property PACKAGE_PIN F2 [get_ports reset_rtl_0]
set_property IOSTANDARD LVCMOS33 [get_ports reset_rtl_0]




# LEDs - Innova-2 Flex - A6=D19, B6=D18
set_property PACKAGE_PIN B6 [get_ports TxD_0]
set_property IOSTANDARD LVCMOS33 [get_ports TxD_0]




# Bitstream Memory Configuration File Settings

set_property CONFIG_MODE SPIx8 [current_design]
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

