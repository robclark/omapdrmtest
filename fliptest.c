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
	MSG("Simple page-flip test, similar to 'modetest' but with option to use tiled buffers.");
	MSG("");
	disp_usage();
}

int
main(int argc, char **argv)
{
	struct display *disp;
	struct buffer **buffers;
	int ret, i;

	MSG("Opening Display..");
	disp = disp_open(argc, argv);
	if (!disp) {
		usage(argv[0]);
		return 1;
	}

	if (check_args(argc, argv)) {
		/* remaining args.. print usage msg */
		usage(argv[0]);
		return 0;
	}

	buffers = disp_get_buffers(disp, NBUF);
	if (!buffers) {
		return 1;
	}

	for (i = 0; i < CNT; i++) {
		struct buffer *buf = buffers[i % NBUF];
		fill(buf, i * 2);
		ret = disp_post_buffer(disp, buf);
		if (ret) {
			return ret;
		}
	}

	MSG("Ok!");

	return 0;
}
