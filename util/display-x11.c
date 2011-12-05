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


void
disp_x11_usage(void)
{
	MSG("X11 Display Options:");
//	MSG("\t-t <tiled-mode>\t8, 16, or 32");
//	MSG("\t-s <connector_id>:<mode>\tset a mode");
//	MSG("\t-s <connector_id>@<crtc_id>:<mode>\tset a mode");
}

struct display *
disp_x11_open(int argc, char **argv)
{
	ERROR("unimplemented");
	return NULL;
}
