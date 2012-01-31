#!/bin/sh

# link pipeline:
/usr/bin/media-ctl -d /dev/media0 -r -l '"OMAP4 ISS CSI2a":1 -> "OMAP4 ISS CSI2a output":0 [1]'

# set formats:
/usr/bin/media-ctl -d /dev/media0 -f '"ov5640 3-003c":0 [UYVY 640x480]','"OMAP4 ISS CSI2a":0 [UYVY 640x480]'

# print setup:
/usr/bin/media-ctl -d /dev/media0 -p

