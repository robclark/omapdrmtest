/*
 * Copyright (C) 2011 Texas Instruments
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "util.h"

#include <drm.h>

void disp_kms_usage(void);
struct display * disp_kms_open(int argc, char **argv);

#ifdef HAVE_X11
void disp_x11_usage(void);
struct display * disp_x11_open(int argc, char **argv);
void disp_x11_close(struct display *disp);
#endif

void
disp_usage(void)
{
	MSG("Generic Display options:");
	MSG("\t--fps <fps>\tforce playback rate (0 means \"do not force\")");

#ifdef HAVE_X11
	disp_x11_usage();
#endif
	disp_kms_usage();
}

struct display *
disp_open(int argc, char **argv)
{
	struct display *disp;
	int i, fps = 0;

	for (i = 1; i < argc; i++) {
		if (!argv[i]) {
			continue;
		}
		if (!strcmp("--fps", argv[i])) {
			argv[i++] = NULL;

			if (sscanf(argv[i], "%d", &fps) != 1) {
				ERROR("invalid arg: %s", argv[i]);
				return NULL;
			}

			MSG("Forcing playback rate at %d fps.", fps);
			argv[i] = NULL;
		}
	}

#ifdef HAVE_X11
	disp = disp_x11_open(argc, argv);
	if (disp)
		goto out;
#endif

	disp = disp_kms_open(argc, argv);

	if (!disp) {
		ERROR("unable to create display");
	}

out:
	disp->rtctl.fps = fps;
	return disp;
}

void disp_close(struct display *disp)
{
#ifdef HAVE_X11
	disp_x11_close(disp);
#endif
}

struct buffer **
disp_get_vid_buffers(struct display *disp, uint32_t n,
		uint32_t fourcc, uint32_t w, uint32_t h)
{
	struct buffer **buffers;
	unsigned int i;

	buffers = disp->get_vid_buffers(disp, n, fourcc, w, h);
	if (buffers) {
		/* if allocation succeeded, store in the unlocked
		 * video buffer list
		 */
		list_init(&disp->unlocked);
		for (i = 0; i < n; i++)
			list_add(&buffers[i]->unlocked, &disp->unlocked);
	}

	return buffers;
}

struct buffer *
disp_get_vid_buffer(struct display *disp)
{
	struct buffer *buf = NULL;
	if (!list_is_empty(&disp->unlocked)) {
		buf = list_last_entry(&disp->unlocked, struct buffer, unlocked);
		list_del(&buf->unlocked);

		/* barrier.. if we are using GPU blitting, we need to make sure
		 * that the GPU is finished:
		 */
		omap_bo_cpu_prep(buf->bo[0], OMAP_GEM_WRITE);
		omap_bo_cpu_fini(buf->bo[0], OMAP_GEM_WRITE);
	}
	return buf;
}

void
disp_put_vid_buffer(struct display *disp, struct buffer *buf)
{
	list_add(&buf->unlocked, &disp->unlocked);
}

/* Maintain playback rate if fps > 0. */
static void maintain_playback_rate(struct rate_control *p)
{
	long usecs_since_last_frame;
	int usecs_between_frames, usecs_to_sleep;

	if (p->fps <= 0)
		return;

	usecs_between_frames = 1000000 / p->fps;
	usecs_since_last_frame = mark(&p->last_frame_mark);
	MSG("fps: %.02f", 1000000.0 / usecs_since_last_frame);
	usecs_to_sleep = usecs_between_frames - usecs_since_last_frame + p->usecs_to_sleep;

	if (usecs_to_sleep < 0)
		usecs_to_sleep = 0;

	/* mark() has a limitation that >1s time deltas will make the whole
	 * loop diverge. Workaround that limitation by clamping our desired sleep time
	 * to a maximum. TODO: Remove when mark() is in better shape. */
	if (usecs_to_sleep >= 1000000)
		usecs_to_sleep = 999999;

	/* We filter a bit our rate adaptation, to avoid being too "choppy".
	 * Adjust the "alpha" value as needed. */
	p->usecs_to_sleep = ((67 * p->usecs_to_sleep) + (33 * usecs_to_sleep)) / 100;

	if (p->usecs_to_sleep >= 1) {
		MSG("sleeping %dus", p->usecs_to_sleep);
		usleep(p->usecs_to_sleep);
	}
}

/* flip to / post the specified buffer */
int
disp_post_buffer(struct display *disp, struct buffer *buf)
{
	maintain_playback_rate(&disp->rtctl);
	return disp->post_buffer(disp, buf);
}

/* flip to / post the specified video buffer */
int
disp_post_vid_buffer(struct display *disp, struct buffer *buf,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	maintain_playback_rate(&disp->rtctl);
	return disp->post_vid_buffer(disp, buf, x, y, w, h);
}

struct buffer *
disp_get_fb(struct display *disp)
{
	struct buffer **bufs = disp_get_buffers(disp, 1);
	if (!bufs)
		return NULL;
	fill(bufs[0], 42);
	disp_post_buffer(disp, bufs[0]);
	return bufs[0];
}


int
check_args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		if (argv[i]) {
			ERROR("invalid arg: %s", argv[i]);
			return -1;
		}
	}
	return 0;
}

/* stolen from modetest.c */
static void
fillRGB4(char *virtual, int n, int width, int height, int stride)
{
	int i, j;
	/* paint the buffer with colored tiles */
	for (j = 0; j < height; j++) {
		uint32_t *fb_ptr = (uint32_t*)((char*)virtual + j * stride);
		for (i = 0; i < width; i++) {
			div_t d = div(n+i+j, width);
			fb_ptr[i] =
					0x00130502 * (d.quot >> 6) +
					0x000a1120 * (d.rem >> 6);
		}
	}
}


/* swap these for big endian.. */
#define RED   2
#define GREEN 1
#define BLUE  0

static void
fill420(unsigned char *y, unsigned char *u, unsigned char *v,
		int cs /*chroma pixel stride */,
		int n, int width, int height, int stride)
{
	int i, j;

	/* paint the buffer with colored tiles, in blocks of 2x2 */
	for (j = 0; j < height; j+=2) {
		unsigned char *y1p = y + j * stride;
		unsigned char *y2p = y1p + stride;
		unsigned char *up = u + (j/2) * stride * cs / 2;
		unsigned char *vp = v + (j/2) * stride * cs / 2;

		for (i = 0; i < width; i+=2) {
			div_t d = div(n+i+j, width);
			uint32_t rgb = 0x00130502 * (d.quot >> 6) + 0x000a1120 * (d.rem >> 6);
			unsigned char *rgbp = (unsigned char *)&rgb;
			unsigned char y = (0.299 * rgbp[RED]) + (0.587 * rgbp[GREEN]) + (0.114 * rgbp[BLUE]);

			*(y2p++) = *(y1p++) = y;
			*(y2p++) = *(y1p++) = y;

			*up = (rgbp[BLUE] - y) * 0.565 + 128;
			*vp = (rgbp[RED] - y) * 0.713 + 128;
			up += cs;
			vp += cs;
		}
	}
}

static void
fill422(unsigned char *virtual, int n, int width, int height, int stride)
{
	int i, j;
	/* paint the buffer with colored tiles */
	for (j = 0; j < height; j++) {
		uint8_t *ptr = (uint8_t*)((char*)virtual + j * stride);
		for (i = 0; i < width; i++) {
			div_t d = div(n+i+j, width);
			uint32_t rgb = 0x00130502 * (d.quot >> 6) + 0x000a1120 * (d.rem >> 6);
			unsigned char *rgbp = (unsigned char *)&rgb;
			unsigned char y = (0.299 * rgbp[RED]) + (0.587 * rgbp[GREEN]) + (0.114 * rgbp[BLUE]);

			*(ptr++) = y;
			*(ptr++) = (rgbp[BLUE] - y) * 0.565 + 128;
			*(ptr++) = y;
			*(ptr++) = (rgbp[RED] - y) * 0.713 + 128;
		}
	}
}


void
fill(struct buffer *buf, int n)
{
	int i;

	for (i = 0; i < buf->nbo; i++)
		omap_bo_cpu_prep(buf->bo[i], OMAP_GEM_WRITE);

	switch(buf->fourcc) {
	case 0: {
		assert(buf->nbo == 1);
		fillRGB4(omap_bo_map(buf->bo[0]), n,
				buf->width, buf->height, buf->pitches[0]);
		break;
	}
	case FOURCC('Y','U','Y','V'): {
		assert(buf->nbo == 1);
		fill422(omap_bo_map(buf->bo[0]), n,
				buf->width, buf->height, buf->pitches[0]);
		break;
	}
	case FOURCC('N','V','1','2'): {
		unsigned char *y, *u, *v;
		assert(buf->nbo == 2);
		y = omap_bo_map(buf->bo[0]);
		u = omap_bo_map(buf->bo[1]);
		v = u + 1;
		fill420(y, u, v, 2, n, buf->width, buf->height, buf->pitches[0]);
		break;
	}
	case FOURCC('I','4','2','0'): {
		unsigned char *y, *u, *v;
		assert(buf->nbo == 3);
		y = omap_bo_map(buf->bo[0]);
		u = omap_bo_map(buf->bo[1]);
		v = omap_bo_map(buf->bo[2]);
		fill420(y, u, v, 1, n, buf->width, buf->height, buf->pitches[0]);
		break;
	}
	default:
		ERROR("invalid format: 0x%08x", buf->fourcc);
		break;
	}

	for (i = 0; i < buf->nbo; i++)
		omap_bo_cpu_fini(buf->bo[i], OMAP_GEM_WRITE);
}
