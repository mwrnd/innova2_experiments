

# OpenCAPI - PCIE4 Location: X0Y3 - GTY Quads 131, 132
# GTY Quad 131 == Quad_X0Y4
# M36=MGTYRXP0=CHANNEL_X0Y16, L38=MGTYRXP1=CHANNEL_X0Y17,
# K36=MGTYRXP2=CHANNEL_X0Y18, J38=MGTYRXP3=CHANNEL_X0Y19
# GTY Quad 132 == Quad_X0Y5
# H36=MGTYRXP0=CHANNEL_X0Y20, G38=MGTYRXP1=CHANNEL_X0Y21,
# E38=MGTYRXP2=CHANNEL_X0Y22, C38=MGTYRXP3=CHANNEL_X0Y23
set_property PACKAGE_PIN M36 [get_ports {pcie_7x_mgt_rtl_0_rxp[0]}]
set_property PACKAGE_PIN L38 [get_ports {pcie_7x_mgt_rtl_0_rxp[1]}]
set_property PACKAGE_PIN K36 [get_ports {pcie_7x_mgt_rtl_0_rxp[2]}]
set_property PACKAGE_PIN J38 [get_ports {pcie_7x_mgt_rtl_0_rxp[3]}]
set_property PACKAGE_PIN H36 [get_ports {pcie_7x_mgt_rtl_0_rxp[4]}]
set_property PACKAGE_PIN G38 [get_ports {pcie_7x_mgt_rtl_0_rxp[5]}]
set_property PACKAGE_PIN E38 [get_ports {pcie_7x_mgt_rtl_0_rxp[6]}]
set_property PACKAGE_PIN C38 [get_ports {pcie_7x_mgt_rtl_0_rxp[7]}]

# OpenCAPI Clock Pins T27 and P27 are connected together
# OpenCAPI - Clock - T27 is MGTREFCLK0 in GTY_Quad/Bank 131 - X0Y4
set_property PACKAGE_PIN T27 [get_ports diff_clock_rtl_0_clk_p]
create_clock -name sys_clk -period 10.000 [get_ports diff_clock_rtl_0_clk_p]

# OpenCAPI - Clock - P27 is MGTREFCLK0 in GTY_Quad/Bank 132 - X0Y5
#set_property PACKAGE_PIN P27 [get_ports {diff_clock_rtl_0_clk_p[0]}]
#create_clock -name sys_clk -period 10.000 [get_ports diff_clock_rtl_0_clk_p]

# OpenCAPI - Reset - C4 is in Bank 90
set_property PACKAGE_PIN C4 [get_ports reset_rtl_0]
set_property IOSTANDARD LVCMOS33 [get_ports reset_rtl_0]
set_property PULLUP true [get_ports reset_rtl_0]
set_false_path -from [get_ports reset_rtl_0]



# Memory Configuration File Settings
set_property CONFIG_MODE SPIx8 [current_design]
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property CFGBVS GND [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 127.5 [current_design]
set_property BITSTREAM.CONFIG.CONFIGFALLBACK DISABLE [current_design]
set_property BITSTREAM.CONFIG.EXTMASTERCCLK_EN DISABLE [current_design]
set_property BITSTREAM.CONFIG.NEXT_CONFIG_REBOOT DISABLE [current_design]
set_property BITSTREAM.CONFIG.OVERTEMPSHUTDOWN ENABLE [current_design]
set_property BITSTREAM.CONFIG.SPI_32BIT_ADDR YES [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 8 [current_design]
set_property BITSTREAM.CONFIG.SPI_FALL_EDGE YES [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLUP [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.GENERAL.CRC ENABLE [current_design]
set_property BITSTREAM.GENERAL.JTAG_SYSMON ENABLE [current_design]
