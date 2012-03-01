/*
 * Copyright (c) 2011, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "util.h"

struct demux {
	AVFormatContext *afc;
	AVStream *st;
	AVCodecContext *cc;
	AVBitStreamFilterContext *bsf;
};

static AVFormatContext *
open_file(const char *filename)
{
	AVFormatContext *afc;
	int err = av_open_input_file(&afc, filename, NULL, 0, NULL);

	if (!err)
		err = av_find_stream_info(afc);

	if (err < 0) {
		ERROR("%s: lavf error %d", filename, err);
		exit(1);
	}

	dump_format(afc, 0, filename, 0);

	return afc;
}

static AVStream *
find_stream(AVFormatContext *afc)
{
	AVStream *st = NULL;
	unsigned int i;

	for (i = 0; i < afc->nb_streams; i++) {
		if (afc->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && !st)
			st = afc->streams[i];
		else
			afc->streams[i]->discard = AVDISCARD_ALL;
	}

	return st;
}

static struct demux * open_stream(const char * filename, int *width, int *height)
{
	AVFormatContext *afc = open_file(filename);
	AVStream *st = find_stream(afc);
	AVCodecContext *cc = st->codec;
	AVBitStreamFilterContext *bsf = NULL;
	struct demux *demux;

	if (cc->codec_id != CODEC_ID_H264) {
		ERROR("could not open '%s': unsupported codec %d", filename, cc->codec_id);
		return NULL;
	}

	if (cc->extradata && cc->extradata_size > 0 && cc->extradata[0] == 1) {
		MSG("initializing bitstream filter");
		bsf = av_bitstream_filter_init("h264_mp4toannexb");
		if (!bsf) {
			ERROR("could not open '%s': failed to initialize bitstream filter", filename);
			return NULL;
		}
	}

	*width = cc->width;
	*height = cc->height;

	demux = calloc(1, sizeof(*demux));

	demux->afc = afc;
	demux->cc  = cc;
	demux->st  = st;
	demux->bsf = bsf;

	return demux;
}

struct demux * demux_init(const char * filename, int *width, int *height)
{
	av_register_all();
	avcodec_register_all();
	return open_stream(filename, width, height);
}

int demux_read(struct demux *demux, char *input, int size)
{
	AVPacket pk = {};

	while (!av_read_frame(demux->afc, &pk)) {
		if (pk.stream_index == demux->st->index) {
			uint8_t *buf;
			int bufsize;

			if (demux->bsf) {
				int ret;
				ret = av_bitstream_filter_filter(demux->bsf, demux->cc,
						NULL, &buf, &bufsize, pk.data, pk.size, 0);
				if (ret < 0) {
					ERROR("bsf error: %d", ret);
					return 0;
				}
			} else {
				buf     = pk.data;
				bufsize = pk.size;
			}

			if (bufsize > size)
				bufsize = size;

			memcpy(input, buf, bufsize);

			if (demux->bsf)
				av_free(buf);

			av_free_packet(&pk);

			return bufsize;
		}
		av_free_packet(&pk);
	}

	return 0;
}

void demux_deinit(struct demux *demux)
{
	av_close_input_file(demux->afc);
	if (demux->bsf)
		av_bitstream_filter_close(demux->bsf);
	free(demux);
}
