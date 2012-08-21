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


#include "util.h"

#define NBUF 3
#define CNT  500

static void
usage(char *name)
{
	MSG("Usage: %s [OPTION]...", name);
	MSG("Test of buffer passing between v4l2 camera and display.");
	MSG("");
	disp_usage();
	v4l2_usage();
}

int
main(int argc, char **argv)
{
	struct display *disp;
	struct v4l2 *v4l2;
	struct buffer *framebuf;
	struct buffer **buffers;
	uint32_t fourcc, width, height;
	int ret, i;

	MSG("Opening Display..");
	disp = disp_open(argc, argv);
	if (!disp) {
		usage(argv[0]);
		return 1;
	}

	MSG("Opening V4L2..");
	v4l2 = v4l2_open(argc, argv, &fourcc, &width, &height);
	if (!v4l2) {
		usage(argv[0]);
		return 1;
	}

	if (check_args(argc, argv)) {
		/* remaining args.. print usage msg */
		usage(argv[0]);
		return 0;
	}

	framebuf = disp_get_fb(disp);

	buffers = disp_get_vid_buffers(disp, NBUF, fourcc, width, height);
	if (!buffers) {
		return 1;
	}

	ret = v4l2_reqbufs(v4l2, buffers, NBUF);
	if (ret) {
		return 1;
	}

	v4l2_qbuf(v4l2, buffers[0]);
	v4l2_streamon(v4l2);
	for (i = 1; i < CNT; i++) {
		v4l2_qbuf(v4l2, buffers[i % NBUF]);
		ret = disp_post_vid_buffer(disp, v4l2_dqbuf(v4l2),
				0, 0, width, height);
		if (ret) {
			return ret;
		}
	}
	v4l2_streamoff(v4l2);
	v4l2_dqbuf(v4l2);

	MSG("Ok!");
	disp_close(disp);

	return ret;
}
