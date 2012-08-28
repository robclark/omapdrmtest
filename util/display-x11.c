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

#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xregion.h>
#include <X11/extensions/dri2proto.h>
#include <X11/extensions/dri2.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static int global_fd = 0;

#define to_display_x11(x) container_of(x, struct display_x11, base)
struct display_x11 {
	struct display base;
	Display *dpy;
	Window win;
};

#define to_buffer_x11(x) container_of(x, struct buffer_x11, base)
struct buffer_x11 {
	struct buffer base;
	DRI2Buffer *dri2buf;
};


#define MAX_BUFFERS 32

static struct buffer **
get_buffers(struct display *disp, uint32_t n)
{
	ERROR("unimplemented");
	return NULL;
}

static struct buffer **
get_vid_buffers(struct display *disp, uint32_t n,
		uint32_t fourcc, uint32_t w, uint32_t h)
{
	struct display_x11 *disp_x11 = to_display_x11(disp);
	struct buffer **bufs;
	unsigned attachments[MAX_BUFFERS+1] = {
			DRI2BufferFrontLeft, 32, /* always requested, never returned */
	};
	DRI2Buffer *dri2bufs;
	uint32_t i, j, nbufs = 1;

	for (i = 0; i < n; i++) {
		attachments[ nbufs * 2     ] = i + 1;
		attachments[(nbufs * 2) + 1] = fourcc;
		nbufs++;
	}

	dri2bufs = DRI2GetBuffersVid(disp_x11->dpy, disp_x11->win,
			w, h, attachments, nbufs, &nbufs);
	if (!dri2bufs) {
		ERROR("DRI2GetBuffersVid failed");
		return NULL;
	}

	MSG("DRI2GetBuffers: nbufs=%d", nbufs);

	if (nbufs != n) {
		ERROR("wrong number of bufs: %d vs %d", nbufs, n);
		return NULL;
	}

	bufs = calloc(nbufs, sizeof(struct buffer *));

	for (i = 0; i < nbufs; i++) {
		struct buffer *buf;
		struct buffer_x11 *buf_x11;

		buf_x11 = calloc(1, sizeof(*buf_x11));
		if (!buf_x11) {
			ERROR("allocation failed");
			return NULL;
		}

		buf_x11->dri2buf = &dri2bufs[i];

		buf = &buf_x11->base;

		buf->fourcc = fourcc;
		buf->width = w;
		buf->height = h;
		buf->multiplanar = false;

		for (j = 0; dri2bufs[i].names[j]; j++) {
			buf->bo[j] = omap_bo_from_name(disp->dev, dri2bufs[i].names[j]);
			buf->pitches[j] = dri2bufs[i].pitch[j];
		}

		buf->nbo = j;

		bufs[i] = buf;
	}

	return bufs;
}

static int
post_buffer(struct display *disp, struct buffer *buf)
{
	MSG("unimplemented");
	return -1;
}

static int
post_vid_buffer(struct display *disp, struct buffer *buf,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	struct display_x11 *disp_x11 = to_display_x11(disp);
	struct buffer_x11 *buf_x11 = to_buffer_x11(buf);
	CARD64 count;
	BoxRec b = {
			.x1 = x,
			.y1 = y,
			.x2 = x + w,
			.y2 = y + h,
	};

	DRI2SwapBuffersVid(disp_x11->dpy, disp_x11->win, 0, 0, 0, &count,
			buf_x11->dri2buf->attachment, &b);
	DBG("DRI2SwapBuffersVid[%u]: count=%llu",
			buf_x11->dri2buf->attachment, count);

	return 0;
}

static void
close_x11(struct display *disp)
{
	struct display_x11 *disp_x11 = to_display_x11(disp);
	XCloseDisplay(disp_x11->dpy);
}

void
disp_x11_usage(void)
{
	MSG("X11 Display Options:");
	MSG("\t-w WxH\tset window dimensions");
}

/*** Move these somewhere common ***/
#include <ctype.h>
static Bool is_fourcc(unsigned int val)
{
	char *str = (char *)&val;
	return isalnum(str[0]) && isalnum(str[1]) && isalnum(str[2]) && isalnum(str[3]);
}
#define ATOM(name) XInternAtom(dpy, name, False)
static inline void print_hex(int len, const unsigned char *val)
{
	char buf[33];
	int i, j;
	for (i = 0; i < len; i += j) {
		for (j = 0; (j < 16) && ((i + j) < len); ++j)
			sprintf(&buf[j * 2], "%02x", val[i + j]);
		fprintf(stderr, "\t%s\n", buf);
	}
}
/***********************************/


static Bool WireToEvent(Display *dpy, XExtDisplayInfo *info,
		XEvent *event, xEvent *wire)
{
	switch ((wire->u.u.type & 0x7f) - info->codes->first_event) {

	case DRI2_BufferSwapComplete:
	{
//		xDRI2BufferSwapComplete *awire = (xDRI2BufferSwapComplete *)wire;
		DBG("BufferSwapComplete");
		return True;
	}
	default:
		/* client doesn't support server event */
		break;
	}

	return False;
}

static Status EventToWire(Display *dpy, XExtDisplayInfo *info,
		XEvent *event, xEvent *wire)
{
   switch (event->type) {
   default:
      /* client doesn't support server event */
      break;
   }

   return Success;
}

static const DRI2EventOps ops = {
		.WireToEvent = WireToEvent,
		.EventToWire = EventToWire,
};

struct display *
disp_x11_open(int argc, char **argv)
{
	struct display_x11 *disp_x11 = NULL;
	struct display *disp;
	Display *dpy;
	Window root, win;
	drm_magic_t magic;
	int eventBase, errorBase, major, minor;
	char *driver, *device;
	unsigned int nformats, *formats;
	unsigned int i, width = 500, height = 500;
	CARD32 *pval;

	MSG("attempting to open X11 connection");
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		ERROR("Could not open display");
		goto no_x11;
	}

	if (!DRI2InitDisplay(dpy, &ops)) {
		ERROR("DRI2InitDisplay failed");
		goto no_x11;
	}

	if (!DRI2QueryExtension(dpy, &eventBase, &errorBase)) {
		ERROR("DRI2QueryExtension failed");
		goto no_x11;
	}

	MSG("DRI2QueryExtension: eventBase=%d, errorBase=%d", eventBase, errorBase);

	if (!DRI2QueryVersion(dpy, &major, &minor)) {
		ERROR("DRI2QueryVersion failed");
		goto no_x11;
	}

	MSG("DRI2QueryVersion: major=%d, minor=%d", major, minor);

	root = RootWindow(dpy, DefaultScreen(dpy));

	if (!DRI2Connect(dpy, root, DRI2DriverDRI, &driver, &device)) {
		MSG("DRI2Connect failed");
		goto no_x11;
	}

	MSG("DRI2Connect: driver=%s, device=%s", driver, device);

	disp_x11 = calloc(1, sizeof(*disp_x11));
	if (!disp_x11) {
		ERROR("allocation failed");
		goto no_x11;
	}

	disp = &disp_x11->base;

	if (!global_fd) {
		MSG("opening device: %s", device);
		global_fd = open(device, O_RDWR);
		if (global_fd < 0) {
			ERROR("could not open drm device: %s (%d)",
					strerror(errno), errno);
			goto no_x11_free;
		}

		if (drmGetMagic(global_fd, &magic)) {
			ERROR("drmGetMagic failed");
			goto no_x11_free;
		}

		if (!DRI2Authenticate(dpy, root, magic)) {
			ERROR("DRI2Authenticate failed");
			goto no_x11_free;
		}
	}

	disp->fd = global_fd;

	disp->dev = omap_device_new(disp->fd);
	if (!disp->dev) {
		ERROR("couldn't create device");
		goto no_x11_free;
	}

	disp->get_buffers = get_buffers;
	disp->get_vid_buffers = get_vid_buffers;
	disp->post_buffer = post_buffer;
	disp->post_vid_buffer = post_vid_buffer;
	disp->close = close_x11;
	disp->multiplanar = false;

	/* note: set args to NULL after we've parsed them so other modules know
	 * that it is already parsed (since the arg parsing is decentralized)
	 */
	for (i = 1; i < argc; i++) {
		if (!argv[i]) {
			continue;
		}
		if (!strcmp("-w", argv[i])) {
			argv[i++] = NULL;
			if (sscanf(argv[i], "%dx%d", &width, &height) != 2) {
				ERROR("invalid arg: %s", argv[i]);
				goto no_x11_free;
			}
		} else {
			/* ignore */
			continue;
		}
		argv[i] = NULL;
	}

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), 1, 1,
			width, height, 0, BlackPixel (dpy, 0), BlackPixel(dpy, 0));
	XMapWindow(dpy, win);
	XFlush(dpy);

	disp_x11->dpy = dpy;
	disp_x11->win = win;

	if (!DRI2GetFormats(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			&nformats, &formats)) {
		ERROR("DRI2GetFormats failed");
		goto no_x11_free;
	}

	if (nformats == 0) {
		ERROR("no formats!");
		goto no_x11_free;
	}

	/* print out supported formats */
	MSG("Found %d supported formats:", nformats);
	for (i = 0; i < nformats; i++) {
		if (is_fourcc(formats[i])) {
			MSG("  %d: %08x (\"%.4s\")", i, formats[i], (char *)&formats[i]);
		} else {
			MSG("  %d: %08x (device dependent)", i, formats[i]);
		}
	}

	DRI2CreateDrawable(dpy, win);

	/* check some attribute.. just to exercise the code-path: */
	if (!DRI2GetAttribute(dpy, win, ATOM("XV_CSC_MATRIX"), &i, &pval)) {
		ERROR("DRI2GetAttribute failed");
		goto no_x11_free;
	}

	MSG("Got CSC matrix:");
	print_hex(i*4, (const unsigned char *)pval);

	// XXX

	return disp;

no_x11_free:
	XFree(driver);
	XFree(device);
no_x11:
	ERROR("unimplemented");
	return NULL;
}
