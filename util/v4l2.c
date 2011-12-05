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

#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "util.h"

struct v4l2 {
	int fd;
	int nbufs;
	struct v4l2_buffer *v4l2bufs;
	struct buffer **bufs;
};

void
v4l2_usage(void)
{
	MSG("V4L2 Capture Options:");
	MSG("\t-c WxH@fourcc\tset capture dimensions/format");
}

/* Open v4l2 (and media0??) XXX */
struct v4l2 *
v4l2_open(int argc, char **argv)
{
	struct v4l2_format format = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};
	struct v4l2 *v4l2;
	int i, ret;

	v4l2 = calloc(1, sizeof(*v4l2));
	v4l2->fd = open("/dev/video0", O_RDWR);

	ret = ioctl(v4l2->fd, VIDIOC_G_FMT, &format);
	if (ret < 0) {
		ERROR("VIDIOC_G_FMT failed: %s (%d)", strerror(errno), ret);
		goto fail;
	}

	/* note: set args to NULL after we've parsed them so other modules know
	 * that it is already parsed (since the arg parsing is decentralized)
	 */
	for (i = 1; i < argc; i++) {
		if (!argv[i]) {
			continue;
		}
		if (!strcmp("-c", argv[i])) {
			char fourccstr[5];
			argv[i++] = NULL;
			if (sscanf(argv[i], "%ux%u@%4s",
					&format.fmt.pix.width,
					&format.fmt.pix.height,
					fourccstr) != 3) {
				ERROR("invalid arg: %s", argv[i]);
				goto fail;
			}
			format.fmt.pix.pixelformat = FOURCC_STR(fourccstr);
		} else {
			continue;
		}
		argv[i] = NULL;
	}

	if ((format.fmt.pix.width == 0) ||
			(format.fmt.pix.height == 0) ||
			(format.fmt.pix.pixelformat == 0)) {
		ERROR("invalid capture settings '%dx%d@%4s' (did you not use '-c'?)",
				format.fmt.pix.width, format.fmt.pix.height,
				(char *)&format.fmt.pix.pixelformat);
		goto fail;
	}

	ret = ioctl(v4l2->fd, VIDIOC_S_FMT, &format);
	if (ret < 0) {
		ERROR("VIDIOC_S_FMT failed: %s (%d)", strerror(errno), ret);
		goto fail;
	}

	return v4l2;

fail:
	// XXX cleanup
	return NULL;
}

int
v4l2_reqbufs(struct v4l2 *v4l2, struct buffer **bufs, uint32_t n)
{
	struct v4l2_requestbuffers reqbuf = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_DMABUF,
			.count = n,
	};
	uint32_t i;
	int ret;

	if (v4l2->v4l2bufs) {
		// maybe eventually need to support this?
		ERROR("already reqbuf'd");
		return -1;
	}

	ret = ioctl(v4l2->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret < 0) {
		ERROR("VIDIOC_REQBUFS failed: %s (%d)", strerror(errno), ret);
		return ret;
	}

	if ((reqbuf.count != n) ||
			(reqbuf.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
			(reqbuf.memory != V4L2_MEMORY_DMABUF)) {
		ERROR("unsupported..");
		return -1;
	}

	v4l2->nbufs = reqbuf.count;
	v4l2->v4l2bufs = calloc(v4l2->nbufs, sizeof(*v4l2->v4l2bufs));
	if (!v4l2->v4l2bufs) {
		ERROR("allocation failed");
		return -1;
	}

	for (i = 0; i < reqbuf.count; i++) {
		v4l2->v4l2bufs[i] = (struct v4l2_buffer){
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
					.memory = V4L2_MEMORY_DMABUF,
					.index = i,
					.m.fd = omap_bo_dmabuf(bufs[i]->bo),
		};
		ret = ioctl(v4l2->fd, VIDIOC_QUERYBUF, &v4l2->v4l2bufs[i]);
		v4l2->v4l2bufs[i].m.fd = omap_bo_dmabuf(bufs[i]->bo);
		if (ret) {
			ERROR("VIDIOC_QUERYBUF failed: %s (%d)", strerror(errno), ret);
			return ret;
		}
	}

	return 0;
}

int
v4l2_streamon(struct v4l2 *v4l2)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

    ret = ioctl(v4l2->fd, VIDIOC_STREAMON, &type);

    if (ret) {
		ERROR("VIDIOC_STREAMON failed: %s (%d)", strerror(errno), ret);
    }

    return ret;
}

int
v4l2_streamoff(struct v4l2 *v4l2)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

    ret = ioctl(v4l2->fd, VIDIOC_STREAMOFF, &type);

    if (ret) {
		ERROR("VIDIOC_STREAMOFF failed: %s (%d)", strerror(errno), ret);
    }

    return ret;
}

int
v4l2_qbuf(struct v4l2 *v4l2, struct buffer *buf)
{
	struct v4l2_buffer *v4l2buf = NULL;
	int i, ret, fd;

	fd = omap_bo_dmabuf(buf->bo);

	for (i = 0; i < v4l2->nbufs; i++) {
		if (v4l2->v4l2bufs[i].m.fd == fd) {
			v4l2buf = &v4l2->v4l2bufs[i];
		}
	}

	if (!v4l2buf) {
		ERROR("invalid buffer");
		return -1;
	}

	MSG("QBUF: idx=%d, fd=%d", v4l2buf->index, v4l2buf->m.fd);

	ret = ioctl(v4l2->fd, VIDIOC_QBUF, v4l2buf);
	if (ret) {
		ERROR("VIDIOC_QBUF failed: %s (%d)", strerror(errno), ret);
	}

	return ret;
}

struct buffer *
v4l2_dqbuf(struct v4l2 *v4l2)
{
	struct buffer *buf;
	struct v4l2_buffer v4l2buf = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_DMABUF,
	};
	int ret;

	ret = ioctl(v4l2->fd, VIDIOC_DQBUF, &v4l2buf);
	if (ret) {
		ERROR("VIDIOC_DQBUF failed: %s (%d)", strerror(errno), ret);
	}

	MSG("DQBUF: idx=%d, fd=%d", v4l2buf.index, v4l2buf.m.fd);

	buf = v4l2->bufs[v4l2buf.index];

	if (omap_bo_dmabuf(buf->bo) != v4l2buf.m.fd) {
		MSG("WARNING: camera gave us incorrect buffer: %d vs %d",
				omap_bo_dmabuf(buf->bo), v4l2buf.m.fd);
	}

	return buf;
}
