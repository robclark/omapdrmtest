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

struct decoder {
	struct display *disp;
	struct demux *demux;
	struct buffer *framebuf;

	Engine_Handle engine;
	VIDDEC3_Handle codec;
	VIDDEC3_Params *params;
	VIDDEC3_DynamicParams *dynParams;
	VIDDEC3_Status *status;
	XDM2_BufDesc *inBufs;
	XDM2_BufDesc *outBufs;
	VIDDEC3_InArgs *inArgs;
	VIDDEC3_OutArgs *outArgs;

	char *input;
	struct omap_bo *input_bo;
	int input_sz;

	suseconds_t tdisp;

};

static void
usage(char *name)
{
	MSG("Usage: %s [OPTIONS] INFILE", name);
	MSG("Test of viddec3 decoder.");
	MSG("");
	MSG("viddec3test options:");
	MSG("\t-h, --help: Print this help and exit.");
	MSG("");
	disp_usage();
}

static void
decoder_close(struct decoder *decoder)
{
	if (decoder->codec)          VIDDEC3_delete(decoder->codec);
	if (decoder->engine)         Engine_close(decoder->engine);
	if (decoder->params)         dce_free(decoder->params);
	if (decoder->dynParams)      dce_free(decoder->dynParams);
	if (decoder->status)         dce_free(decoder->status);
	if (decoder->inBufs)         free(decoder->inBufs);
	if (decoder->outBufs)        free(decoder->outBufs);
	if (decoder->inArgs)         dce_free(decoder->inArgs);
	if (decoder->outArgs)        dce_free(decoder->outArgs);
	if (decoder->input_bo)       omap_bo_del(decoder->input_bo);
	if (decoder->demux)          demux_deinit(decoder->demux);
	if (decoder->disp)           disp_close(decoder->disp);

	free(decoder);
}

static struct decoder *
decoder_open(int argc, char **argv)
{
	struct decoder *decoder;
	char *infile = NULL;
	int i, num_buffers;
	int width, height, padded_width, padded_height;
	Engine_Error ec;
	XDAS_Int32 err;

	decoder = calloc(1, sizeof(*decoder));
	if (!decoder)
		return NULL;

	MSG("%p: Opening Display..", decoder);
	decoder->disp = disp_open(argc, argv);
	if (!decoder->disp)
		goto usage;

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

	if (check_args(argc, argv) || !infile)
		goto usage;

	MSG("%p: Opening Demuxer..", decoder);
	decoder->demux = demux_init(infile, &width, &height);
	if (!decoder->demux) {
		ERROR("%p: could not open demuxer", decoder);
		goto fail;
	}

	MSG("%p: infile=%s, width=%d, height=%d", decoder, infile, width, height);

	/* calculate output buffer parameters: */
	width  = ALIGN2 (width, 4);        /* round up to macroblocks */
	height = ALIGN2 (height, 4);       /* round up to macroblocks */
	padded_width  = ALIGN2 (width + (2*PADX), 7);
	padded_height = height + 4*PADY;
	num_buffers   = MIN(16, 32768 / ((width/16) * (height/16))) + 3;

	MSG("%p: padded_width=%d, padded_height=%d, num_buffers=%d",
			decoder, padded_width, padded_height, num_buffers);

	decoder->input_sz = width * height;
	decoder->input_bo = omap_bo_new(decoder->disp->dev,
			decoder->input_sz, OMAP_BO_WC);
	decoder->input = omap_bo_map(decoder->input_bo);

	decoder->framebuf = disp_get_fb(decoder->disp);

	if (! disp_get_vid_buffers(decoder->disp, num_buffers,
			FOURCC_STR("NV12"), padded_width, padded_height)) {
		ERROR("%p: could not allocate buffers", decoder);
		goto fail;
	}

	MSG("%p: Opening Engine..", decoder);
	dce_set_fd(decoder->disp->fd);
	decoder->engine = Engine_open("ivahd_vidsvr", NULL, &ec);
	if (!decoder->engine) {
		ERROR("%p: could not open engine", decoder);
		goto fail;
	}

	decoder->params = dce_alloc(sizeof(IVIDDEC3_Params));
	decoder->params->size = sizeof(IVIDDEC3_Params);

	decoder->params->maxWidth         = width;
	decoder->params->maxHeight        = height;
	decoder->params->maxFrameRate     = 30000;
	decoder->params->maxBitRate       = 10000000;
	decoder->params->dataEndianness   = XDM_BYTE;
	decoder->params->forceChromaFormat= XDM_YUV_420SP;
	decoder->params->operatingMode    = IVIDEO_DECODE_ONLY;
	decoder->params->displayDelay     = IVIDDEC3_DISPLAY_DELAY_AUTO;
	decoder->params->displayBufsMode  = IVIDDEC3_DISPLAYBUFS_EMBEDDED;
MSG("displayBufsMode: %d", decoder->params->displayBufsMode);
	decoder->params->inputDataMode    = IVIDEO_ENTIREFRAME;
	decoder->params->metadataType[0]  = IVIDEO_METADATAPLANE_NONE;
	decoder->params->metadataType[1]  = IVIDEO_METADATAPLANE_NONE;
	decoder->params->metadataType[2]  = IVIDEO_METADATAPLANE_NONE;
	decoder->params->numInputDataUnits= 0;
	decoder->params->outputDataMode   = IVIDEO_ENTIREFRAME;
	decoder->params->numOutputDataUnits = 0;
	decoder->params->errorInfoMode    = IVIDEO_ERRORINFO_OFF;

	decoder->codec = VIDDEC3_create(decoder->engine,
			"ivahd_h264dec", decoder->params);
	if (!decoder->codec) {
		ERROR("%p: could not create codec", decoder);
		goto fail;
	}

	decoder->dynParams = dce_alloc(sizeof(IVIDDEC3_DynamicParams));
	decoder->dynParams->size = sizeof(IVIDDEC3_DynamicParams);

	decoder->dynParams->decodeHeader  = XDM_DECODE_AU;

	/*Not Supported: Set default*/
	decoder->dynParams->displayWidth  = 0;
	decoder->dynParams->frameSkipMode = IVIDEO_NO_SKIP;
	decoder->dynParams->newFrameFlag  = XDAS_TRUE;

	decoder->status = dce_alloc(sizeof(IVIDDEC3_Status));
	decoder->status->size = sizeof(IVIDDEC3_Status);

	err = VIDDEC3_control(decoder->codec, XDM_SETPARAMS,
			decoder->dynParams, decoder->status);
	if (err) {
		ERROR("%p: fail: %d", decoder, err);
		goto fail;
	}

	/* not entirely sure why we need to call this here.. just copying omx.. */
	err = VIDDEC3_control(decoder->codec, XDM_GETBUFINFO,
			decoder->dynParams, decoder->status);
	if (err) {
		ERROR("%p: fail: %d", decoder, err);
		goto fail;
	}

	decoder->inBufs = calloc(1, sizeof(XDM2_BufDesc));
	decoder->inBufs->numBufs = 1;
	decoder->inBufs->descs[0].buf =
			(XDAS_Int8 *)omap_bo_handle(decoder->input_bo);
	decoder->inBufs->descs[0].bufSize.bytes = omap_bo_size(decoder->input_bo);
	decoder->inBufs->descs[0].memType = XDM_MEMTYPE_BO;

	decoder->outBufs = calloc(1, sizeof(XDM2_BufDesc));
	decoder->outBufs->numBufs = 2;
	decoder->outBufs->descs[0].memType = XDM_MEMTYPE_BO;
	decoder->outBufs->descs[1].memType = XDM_MEMTYPE_BO;

	decoder->inArgs = dce_alloc(sizeof(IVIDDEC3_InArgs));
	decoder->inArgs->size = sizeof(IVIDDEC3_InArgs);

	decoder->outArgs = dce_alloc(sizeof(IVIDDEC3_OutArgs));
	decoder->outArgs->size = sizeof(IVIDDEC3_OutArgs);

	decoder->tdisp = mark(NULL);

	return decoder;

usage:
	usage(argv[0]);
fail:
	if (decoder)
		decoder_close(decoder);
	return NULL;
}

static int
decoder_process(struct decoder *decoder)
{
	XDM2_BufDesc *inBufs = decoder->inBufs;
	XDM2_BufDesc *outBufs = decoder->outBufs;
	VIDDEC3_InArgs *inArgs = decoder->inArgs;
	VIDDEC3_OutArgs *outArgs = decoder->outArgs;
	XDAS_Int32 err;
	struct buffer *buf;
	int i, n;
	suseconds_t tproc;

	buf = disp_get_vid_buffer(decoder->disp);
	if (!buf) {
		ERROR("%p: fail: out of buffers", decoder);
		return -1;
	}

	n = demux_read(decoder->demux, decoder->input, decoder->input_sz);
	if (n) {
		inBufs->descs[0].bufSize.bytes = n;
		inArgs->numBytes = n;
		DBG("%p: push: %d bytes (%p)", decoder, n, buf);
	} else {
		/* end of input.. do we need to flush? */
		MSG("%p: end of input", decoder);
		inBufs->numBufs = 0;
		inArgs->inputID = 0;
	}

	inArgs->inputID = (XDAS_Int32)buf;
	outBufs->descs[0].buf = (XDAS_Int8 *)omap_bo_handle(buf->bo[0]);
	outBufs->descs[0].bufSize.bytes = omap_bo_size(buf->bo[0]);
	outBufs->descs[1].buf = (XDAS_Int8 *)omap_bo_handle(buf->bo[1]);
	outBufs->descs[1].bufSize.bytes = omap_bo_size(buf->bo[1]);

	tproc = mark(NULL);
	err = VIDDEC3_process(decoder->codec, inBufs, outBufs, inArgs, outArgs);
	DBG("%p: processed returned in: %ldus", decoder, (long int)mark(&tproc));
	if (err) {
		ERROR("%p: process returned error: %d", decoder, err);
		ERROR("%p: extendedError: %08x", decoder, outArgs->extendedError);
		if (XDM_ISFATALERROR(outArgs->extendedError))
			return -1;
	}

	for (i = 0; outArgs->outputID[i]; i++) {
		/* calculate offset to region of interest */
		XDM_Rect *r = &(outArgs->displayBufs.bufDesc[0].activeFrameRegion);

		/* get the output buffer and write it to file */
		buf = (struct buffer *)outArgs->outputID[i];
		DBG("%p: post buffer: %p %d,%d %d,%d", decoder, buf,
				r->topLeft.x, r->topLeft.y,
				r->bottomRight.x, r->bottomRight.y);
		disp_post_vid_buffer(decoder->disp, buf,
				r->topLeft.x, r->topLeft.y,
				r->bottomRight.x - r->topLeft.x,
				r->bottomRight.y - r->topLeft.y);
		DBG("%p: display in: %ldus", decoder, (long int)mark(&decoder->tdisp));
	}

	for (i = 0; outArgs->freeBufID[i]; i++) {
		buf = (struct buffer *)outArgs->freeBufID[i];
		disp_put_vid_buffer(decoder->disp, buf);
	}

	if (outArgs->outBufsInUseFlag) {
		MSG("%p: TODO... outBufsInUseFlag", decoder); // XXX
	}

	return (inBufs->numBufs > 0) ? 0 : -1;
}

int
main(int argc, char **argv)
{
	struct decoder *decoders[8] = {};
	int i, n, first = 0, ndecoders = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			exit(0);

		} else if (!strcmp(argv[i], "--")) {
			argv[first] = argv[0];
			decoders[ndecoders++] = decoder_open(i - first, &argv[first]);
			first = i;
		}
	}

	argv[first] = argv[0];
	decoders[ndecoders++] = decoder_open(i - first, &argv[first]);

	do {
		for (i = 0, n = 0; i < ndecoders; i++) {
			if (decoders[i]) {
				int ret = decoder_process(decoders[i]);
				if (ret) {
					decoder_close(decoders[i]);
					decoders[i] = NULL;
					continue;
				}
				n++;
			}
		}
	} while(n > 0);

	return 0;
}
