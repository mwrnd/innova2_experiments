**Work-In-Progress**:

# Innova2 XCKU15P XDMA Using OpenCAPI Connector

[PCIe XDMA](https://docs.xilinx.com/r/en-US/pg195-pcie-dma/Introduction) demo project for the [Innova-2](https://www.nvidia.com/en-us/networking/ethernet/innova-2-flex/)'s OpenCAPI connector.




## PCIe Link Downgrade Issues

Experiencing issues with PCIe Link Downgrades, [1](https://support.xilinx.com/s/question/0D54U00007XJ4l1SAD/how-to-change-to-pcie-gen2-x4-lane-or-pcie-gen2-x4-lane-while-operating-in-pcie-gen3-x4-lane?language=en_US), [2](https://support.xilinx.com/s/question/0D54U00007HjpxzSAB/when-placing-a-gen3-x8-configured-pcie-board-into-a-gen4gen5-compatible-x8-slot-link-width-recognition-issue-occurs?language=en_US), [3](https://support.xilinx.com/s/question/0D54U00007950HjSAI/xilinx-pcie-endpoint-is-no-longer-recognized-by-the-system-after-a-warm-reset?language=en_US).



## Board Setup

A [PCIe_x8_Breakout](https://github.com/mwrnd/PCIe_x8_Breakout) board along with an [OpenCAPI_Breakout](https://github.com/mwrnd/OpenCAPI_Breakout) board are connected to an [Innova-2](https://www.nvidia.com/en-us/networking/ethernet/innova-2-flex/) using a 
 [SlimSAS SFF-8654 8i 85-Ohm Cable](https://www.sfpcables.com/24g-internal-slimsas-sff-8654-to-sff-8654-8i-cable-straight-to-90-degree-left-angle-8x-12-sas-4-0-85-ohm-0-5-1-meter)([Archived](https://web.archive.org/web/20210121175017/https://www.sfpcables.com/24g-internal-slimsas-sff-8654-to-sff-8654-8i-cable-straight-to-90-degree-left-angle-8x-12-sas-4-0-85-ohm-0-5-1-meter)). [RSL74-0540](http://www.amphenol-ast.com/v3/en/product_view.aspx?id=235) or [8ES8-1DF21-0.50](https://www.3m.com/3M/en_US/p/d/b5000000278/)([Datasheet](https://multimedia.3m.com/mws/media/1398233O/3m-slimline-twin-ax-assembly-sff-8654-x8-30awg-78-5100-2665-8.pdf)) should also work.

![PCIe x8 Breakout and OpenCAPI Breakout](img/PCIe_and_OpenCAPI_Breakout.jpg)

Note the [RX U.FL-to-U.FL cables](https://www.digikey.com/en/products/detail/te-connectivity-amp-connectors/2118651-6/11205742) are all the same length as each other and likewise all [TX cables](https://www.digikey.com/en/products/detail/te-connectivity-amp-connectors/2015698-2/1249186) are the same length but RX and TX are different lengths as that is what I had access to. RX on the PCIe board connects to RX on the OpenCAPI board as it uses the OpenCAPI Host pinout. Standard [0.1" M-F Jumpers](https://www.digikey.com/en/products/detail/adafruit-industries-llc/1954/6827087) are used for the PCIe Reset Signal and its GND.

![PCIe x8 Breakout and OpenCAPI Breakout In System](img/PCIe_and_OpenCAPI_Breakout_in_System.jpg)




