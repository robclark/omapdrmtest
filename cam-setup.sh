#!/bin/sh

#/usr/bin/media-ctl -d /dev/media0 -r -l '"OMAP4 ISS CSI2a":1 -> "OMAP4 ISS CSI2a output":0 [1]'
#/usr/bin/media-ctl -d /dev/media0 -f '"ov5640 3-003c":0 [UYVY 640x480]','"OMAP4 ISS CSI2a":0 [UYVY 640x480]'

# CSI2a -> IPIPEIF -> RESIZER -> MEM (OV5640) NV12!!!
#media-ctl -r -l '"OMAP4 ISS CSI2a":1 -> "OMAP4 ISS ISP IPIPEIF":0 [1]','"OMAP4 ISS ISP IPIPEIF":2 -> "OMAP4 ISS ISP resizer":0 [1]','"OMAP4 ISS ISP resizer":1 -> "OMAP4 ISS ISP resizer a output":0 [1]'
#media-ctl -f '"ov5640 3-003c":0 [UYVY 640x480]','"OMAP4 ISS CSI2a":0 [UYVY 640x480]','"OMAP4 ISS ISP IPIPEIF":0 [UYVY 640x480]','"OMAP4 ISS ISP resizer":0 [UYVY 640x480]','"OMAP4 ISS ISP resizer":1 [YUYV1_5X8 640x480]'

# CSI2a -> IPIPEIF -> RESIZER -> MEM (OV5640)
media-ctl -r -l '"OMAP4 ISS CSI2a":1 -> "OMAP4 ISS ISP IPIPEIF":0 [1]','"OMAP4 ISS ISP IPIPEIF":2 -> "OMAP4 ISS ISP resizer":0 [1]','"OMAP4 ISS ISP resizer":1 -> "OMAP4 ISS ISP resizer a output":0 [1]'
media-ctl -f '"ov5640 3-003c":0 [UYVY 640x480]','"OMAP4 ISS CSI2a":0 [UYVY 640x480]','"OMAP4 ISS ISP IPIPEIF":0 [UYVY 640x480]','"OMAP4 ISS ISP resizer":0 [UYVY 640x480]'

# print setup:
/usr/bin/media-ctl -d /dev/media0 -p

