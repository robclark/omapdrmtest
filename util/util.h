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

#ifndef UTIL_H_
#define UTIL_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <omap_drm.h>
#include <omap_drmif.h>

/* align x to next highest multiple of 2^n */
#define ALIGN2(x,n)   (((x) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))

typedef enum {
	false = 0,
	true = 1
} bool;

#include "list.h"

/* Display Interface:
 *
 * Could be either KMS or X11 depending on build and
 * environment.  Some of details of opening/connecting, allocating buffers,
 * etc, differ.  The intention is just to provide as simple as possible
 * abstraction to avoid lots of duplication in each test app to handle
 * both cases.
 */

struct buffer {
	uint32_t fourcc, width, height;
	int nbo;
	struct omap_bo *bo[4];
	uint32_t pitches[4];
	struct list unlocked;
};

struct display {
	int fd;
	uint32_t width, height;
	struct omap_device *dev;
	struct list unlocked;

	struct buffer ** (*get_buffers)(struct display *disp, uint32_t n);
	struct buffer ** (*get_vid_buffers)(struct display *disp,
			uint32_t n, uint32_t fourcc, uint32_t w, uint32_t h);
	int (*post_buffer)(struct display *disp, struct buffer *buf);
	int (*post_vid_buffer)(struct display *disp, struct buffer *buf,
			uint32_t x, uint32_t y, uint32_t w, uint32_t h);
};

/* Print display related help */
void disp_usage(void);

/* Open display.. X11 or KMS depending on cmdline args, environment,
 * and build args
 */
struct display * disp_open(int argc, char **argv);

/* Get normal RGB/UI buffers (ie. not scaled, not YUV) */
static inline struct buffer **
disp_get_buffers(struct display *disp, uint32_t n)
{
	return disp->get_buffers(disp, n);
}

/* Get video/overlay buffers (ie. can be YUV, scaled, etc) */
struct buffer ** disp_get_vid_buffers(struct display *disp, uint32_t n,
		uint32_t fourcc, uint32_t w, uint32_t h);

/* flip to / post the specified buffer */
static inline int
disp_post_buffer(struct display *disp, struct buffer *buf)
{
	return disp->post_buffer(disp, buf);
}

/* flip to / post the specified video buffer */
static inline int
disp_post_vid_buffer(struct display *disp, struct buffer *buf,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	return disp->post_vid_buffer(disp, buf, x, y, w, h);
}

/* allocate a buffer from pool created by disp_get_vid_buffers() */
struct buffer * disp_get_vid_buffer(struct display *disp);
/* free to video buffer pool */
void disp_put_vid_buffer(struct display *disp, struct buffer *buf);

/* helper to setup the display for apps that just need video with
 * no flipchain on the GUI layer
 */
struct buffer * disp_get_fb(struct display *disp);


/* V4L2 utilities:
 */

struct v4l2;

/* Print v4l2 related help */
void v4l2_usage(void);

/* Open v4l2 (and media0??) XXX */
struct v4l2 * v4l2_open(int argc, char **argv, uint32_t *fourcc,
		uint32_t *width, uint32_t *height);

/* Share the buffers w/ v4l2 via dmabuf */
int v4l2_reqbufs(struct v4l2 *v4l2, struct buffer **bufs, uint32_t n);

int v4l2_streamon(struct v4l2 *v4l2);
int v4l2_streamoff(struct v4l2 *v4l2);

/* Queue a buffer to the camera */
int v4l2_qbuf(struct v4l2 *v4l2, struct buffer *buf);

/* Dequeue buffer from camera */
struct buffer * v4l2_dqbuf(struct v4l2 *v4l2);

/* Other utilities..
 */

int check_args(int argc, char **argv);

void fill(struct buffer *buf, int i);

#define FOURCC(a, b, c, d) ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24 ))
#define FOURCC_STR(str)    FOURCC(str[0], str[1], str[2], str[3])

#define MSG(fmt, ...) \
		do { fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while (0)
#define ERROR(fmt, ...) \
		do { fprintf(stderr, "ERROR:%s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); } while (0)

#ifndef container_of
#define container_of(ptr, type, member) \
    (type *)((char *)(ptr) - (char *) &((type *)0)->member)
#endif

#ifndef MIN
#  define MIN(a,b)     (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#  define MAX(a,b)     (((a) > (b)) ? (a) : (b))
#endif

#ifndef PAGE_SHIFT
#  define PAGE_SHIFT 12
#endif

#ifndef PAGE_SIZE
#  define PAGE_SIZE (1 << PAGE_SHIFT)
#endif

/* align x to next highest multiple of 2^n */
#define ALIGN2(x,n)   (((x) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))

#include <sys/time.h>
static inline long
mark(long *last)
{
	struct timeval t;
	gettimeofday(&t, NULL);
	if (last) {
		long delta = t.tv_usec - *last;
		*last = t.tv_usec;
		return delta;
	}
	return t.tv_usec;
}

#endif /* UTIL_H_ */
