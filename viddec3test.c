/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <dce.h>
#include <xdc/std.h>
#include <ti/xdais/xdas.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/ce/video3/viddec3.h>

#include "util.h"
#include "demux.h"

/* Padding for width as per Codec Requirement (for h264) */
#define PADX  32
/* Padding for height as per Codec requirement (for h264)*/
#define PADY  24

static void
usage(char *name)
{
	MSG("Usage: %s [OPTIONS] INFILE", name);
	MSG("Test of viddec3 decoder.");
	MSG("");
	disp_usage();
}

int
main(int argc, char **argv)
{
	struct display *disp;
	struct demux *demux;
	struct buffer *framebuf;
	char *infile = NULL;
	char *input = NULL;
	struct omap_bo *input_bo = NULL;
	int ret = 1, i, input_sz, num_buffers;
	int width, height, padded_width, padded_height;
	Engine_Error ec;
	XDAS_Int32 err;
	Engine_Handle engine;
	VIDDEC3_Handle codec;
	VIDDEC3_Params *params;
	VIDDEC3_DynamicParams *dynParams;
	VIDDEC3_Status *status;
	XDM2_BufDesc *inBufs;
	XDM2_BufDesc *outBufs;
	VIDDEC3_InArgs *inArgs;
	VIDDEC3_OutArgs *outArgs;
	suseconds_t tdisp;

	MSG("Opening Display..");
	disp = disp_open(argc, argv);
	if (!disp) {
		usage(argv[0]);
		return 1;
	}

	/* loop thru args, find input file.. */
	for (i = 1; i < argc; i++) {
		int fd;
		if (!argv[i]) {
			continue;
		}
		fd = open(argv[i], 0);
		if (fd > 0) {
			infile = argv[i];
			argv[i] = NULL;
			close(fd);
			break;
		}
		break;
	}

	if (check_args(argc, argv) || !infile) {
		ERROR("invalid args");
		goto usage;
	}

	MSG("Opening Demuxer..");
	demux = demux_init(infile, &width, &height);
	if (!demux) {
		goto usage;
	}

	MSG("infile=%s, width=%d, height=%d", infile, width, height);

	/* calculate output buffer parameters: */
	width  = ALIGN2 (width, 4);        /* round up to macroblocks */
	height = ALIGN2 (height, 4);       /* round up to macroblocks */
	padded_width  = ALIGN2 (width + (2*PADX), 7);
	padded_height = height + 4*PADY;
	num_buffers   = MIN(16, 32768 / ((width/16) * (height/16))) + 3;

	MSG("padded_width=%d, padded_height=%d, num_buffers=%d",
			padded_width, padded_height, num_buffers);

	input_sz = width * height;
	input_bo = omap_bo_new(disp->dev, input_sz, OMAP_BO_WC);
	input = omap_bo_map(input_bo);

	framebuf = disp_get_fb(disp);

	if (! disp_get_vid_buffers(disp, num_buffers, FOURCC_STR("NV12"),
			padded_width, padded_height)) {
		goto out;
	}

	MSG("Opening Engine..");
	dce_set_fd(disp->fd);
	engine = Engine_open("ivahd_vidsvr", NULL, &ec);
	if (!engine) {
		ERROR("fail");
		goto out;
	}

	params = dce_alloc(sizeof(IVIDDEC3_Params));
	params->size = sizeof(IVIDDEC3_Params);

	params->maxWidth         = width;
	params->maxHeight        = height;
	params->maxFrameRate     = 30000;
	params->maxBitRate       = 10000000;
	params->dataEndianness   = XDM_BYTE;
	params->forceChromaFormat= XDM_YUV_420SP;
	params->operatingMode    = IVIDEO_DECODE_ONLY;
	params->displayDelay     = IVIDDEC3_DISPLAY_DELAY_AUTO;
	params->displayBufsMode  = IVIDDEC3_DISPLAYBUFS_EMBEDDED;
	params->inputDataMode    = IVIDEO_ENTIREFRAME;
	params->metadataType[0]  = IVIDEO_METADATAPLANE_NONE;
	params->metadataType[1]  = IVIDEO_METADATAPLANE_NONE;
	params->metadataType[2]  = IVIDEO_METADATAPLANE_NONE;
	params->numInputDataUnits= 0;
	params->outputDataMode   = IVIDEO_ENTIREFRAME;
	params->numOutputDataUnits = 0;
	params->errorInfoMode    = IVIDEO_ERRORINFO_OFF;

	codec = VIDDEC3_create(engine, "ivahd_h264dec", params);
	if (!codec) {
		ERROR("fail");
		goto out;
	}

	dynParams = dce_alloc(sizeof(IVIDDEC3_DynamicParams));
	dynParams->size = sizeof(IVIDDEC3_DynamicParams);

	dynParams->decodeHeader  = XDM_DECODE_AU;

	/*Not Supported: Set default*/
	dynParams->displayWidth  = 0;
	dynParams->frameSkipMode = IVIDEO_NO_SKIP;
	dynParams->newFrameFlag  = XDAS_TRUE;

	status = dce_alloc(sizeof(IVIDDEC3_Status));
	status->size = sizeof(IVIDDEC3_Status);

	err = VIDDEC3_control(codec, XDM_SETPARAMS, dynParams, status);
	if (err) {
		ERROR("fail: %d", err);
		goto out;
	}

	/* not entirely sure why we need to call this here.. just copying omx.. */
	err = VIDDEC3_control(codec, XDM_GETBUFINFO, dynParams, status);
	if (err) {
		ERROR("fail: %d", err);
		goto out;
	}

	inBufs = malloc(sizeof(XDM2_BufDesc));
	inBufs->numBufs = 1;
	inBufs->descs[0].buf = (XDAS_Int8 *)omap_bo_handle(input_bo);
	inBufs->descs[0].memType = XDM_MEMTYPE_BO;

	outBufs = malloc(sizeof(XDM2_BufDesc));
	outBufs->numBufs = 2;
	outBufs->descs[0].memType = XDM_MEMTYPE_BO;
	outBufs->descs[1].memType = XDM_MEMTYPE_BO;

	inArgs = dce_alloc(sizeof(IVIDDEC3_InArgs));
	inArgs->size = sizeof(IVIDDEC3_InArgs);

	outArgs = dce_alloc(sizeof(IVIDDEC3_OutArgs));
	outArgs->size = sizeof(IVIDDEC3_OutArgs);

	tdisp = mark(NULL);

	while (inBufs->numBufs && outBufs->numBufs) {
		struct buffer *buf;
		int n;
		suseconds_t tproc;

		buf = disp_get_vid_buffer(disp);
		if (!buf) {
			ERROR("fail: out of buffers");
			goto shutdown;
		}

		n = demux_read(demux, input, input_sz);
		if (n) {
			inBufs->descs[0].bufSize.bytes = n;
			inArgs->numBytes = n;
			MSG("push: %d bytes (%p)", n, buf);
		} else {
			/* end of input.. do we need to flush? */
			MSG("end of input");
			inBufs->numBufs = 0;
			inArgs->inputID = 0;
		}

		inArgs->inputID = (XDAS_Int32)buf;
		outBufs->descs[0].buf = (XDAS_Int8 *)omap_bo_handle(buf->bo[0]);
		outBufs->descs[1].buf = (XDAS_Int8 *)omap_bo_handle(buf->bo[1]);

		tproc = mark(NULL);
		err = VIDDEC3_process(codec, inBufs, outBufs, inArgs, outArgs);
		MSG("processed returned in: %ldus", (long int)mark(&tproc));
		if (err) {
			ERROR("process returned error: %d", err);
			ERROR("extendedError: %08x", outArgs->extendedError);
			if (XDM_ISFATALERROR(outArgs->extendedError))
				goto shutdown;
		}

		for (i = 0; outArgs->outputID[i]; i++) {
			/* calculate offset to region of interest */
			XDM_Rect *r = &(outArgs->displayBufs.bufDesc[0].activeFrameRegion);

			/* get the output buffer and write it to file */
			buf = (struct buffer *)outArgs->outputID[i];
			MSG("post buffer: %p %d,%d %d,%d", buf,
					r->topLeft.x, r->topLeft.y,
					r->bottomRight.x, r->bottomRight.y);
			disp_post_vid_buffer(disp, buf, r->topLeft.x, r->topLeft.y,
					r->bottomRight.x - r->topLeft.x,
					r->bottomRight.y - r->topLeft.y);
			MSG("display in: %ldus", (long int)mark(&tdisp));
		}

		for (i = 0; outArgs->freeBufID[i]; i++) {
			buf = (struct buffer *)outArgs->freeBufID[i];
			disp_put_vid_buffer(disp, buf);
		}

		if (outArgs->outBufsInUseFlag) {
			MSG("TODO... outBufsInUseFlag"); // XXX
		}
	}

	MSG("Ok!");
	ret = 0;

shutdown:
	VIDDEC3_delete(codec);

out:
	if (engine)         Engine_close(engine);
	if (params)         dce_free(params);
	if (dynParams)      dce_free(dynParams);
	if (status)         dce_free(status);
	if (inBufs)         free(inBufs);
	if (outBufs)        free(outBufs);
	if (inArgs)         dce_free(inArgs);
	if (outArgs)        dce_free(outArgs);
	if (input_bo)       omap_bo_del(input_bo);
	if (demux)          demux_deinit(demux);

	return ret;

usage:
	usage(argv[0]);
	return ret;
}
