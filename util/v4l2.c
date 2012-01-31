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
	MSG("\t-m\t\tdo MCF setup");
}

/* media_* helpers to do the MCF dance to get things configured properly
 * so that we can set the specified format on the output device.  For
 * non-MCF cameras this can just be skipped.
 */
#include <linux/media.h>
#include <linux/v4l2-subdev.h>

static int
media_open_entity(struct media_entity_desc *entity)
{
	struct stat devstat;
	char devname[32];
	char sysname[32];
	char target[1024];
	char *p;
	int ret;

	sprintf(sysname, "/sys/dev/char/%u:%u", entity->v4l.major,
			entity->v4l.minor);
	ret = readlink(sysname, target, sizeof(target));
	if (ret < 0)
		return -errno;


	target[ret] = '\0';
	p = strrchr(target, '/');
	if (p == NULL)
		return -EINVAL;

	sprintf(devname, "/dev/%s", p + 1);
MSG("\t%s -> %s -> %s", sysname, target, devname);
	ret = stat(devname, &devstat);
	if (ret < 0)
		return -errno;

	/* Sanity check: udev might have reordered the device nodes.
	 * Make sure the major/minor match. We should really use
	 * libudev.
	 */
	if (major(devstat.st_rdev) == entity->v4l.major &&
	    minor(devstat.st_rdev) == entity->v4l.minor) {
		return open(devname, O_RDWR);
	}

	return -1;
}

static int
media_find_entity(int fd, struct media_entity_desc *entity,
		uint32_t type, uint32_t entity_id)
{
	int id, ret = 0;

	for (id = 0; ; id = entity->id) {
		memset(entity, 0, sizeof(*entity));
		entity->id = id | MEDIA_ENT_ID_FLAG_NEXT;

		ret = ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, entity);
		if (ret) {
			ERROR("MEDIA_IOC_ENUM_ENTITIES failed: %s (%d)",
					strerror(errno), ret);
			break;
		}

MSG("\tfound entity: %s type=%08x, flags=%08x, group_id=%d, pads=%d, links=%d",
entity->name, entity->type, entity->flags, entity->group_id,
entity->pads, entity->links);

		if ((entity->type == type) || (entity->id == entity_id)) {
			return 0;
		}
	}

	return ret;
}

static void
media_configure(struct media_entity_desc *entity,
		struct v4l2_format *format, int pad)
{
	struct v4l2_subdev_format ent_format = {
			.pad = pad,
			.which = V4L2_SUBDEV_FORMAT_ACTIVE,
			.format = {
					.width  = format->fmt.pix.width,
					.height = format->fmt.pix.height,
					.code   = V4L2_MBUS_FMT_UYVY8_1X16,
					.field  = V4L2_FIELD_NONE,
					.colorspace = V4L2_COLORSPACE_JPEG,
			},
	};
	int fd, ret;

	fd = media_open_entity(entity);
	if (fd < 0) {
		ERROR("could not open media device: \"%s\" (%d:%d)", entity->name,
				entity->v4l.major, entity->v4l.minor);
		return;
	}

	MSG("Setting format for: \"%s\" (%d)", entity->name, pad);
	ret = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &ent_format);
	if (ret) {
		MSG("Could not configure: %s (%d)", strerror(errno), ret);
	}
}

/* walk the graph and attempt to configure all the nodes to the same settings.
 * This works for smart-sensor with no element in between that can convert/
 * scale..  If the sensor can't handle the settings, then S_FMT just fails
 * and hopefully some element in between can pick up the slack.
 */
static int
media_setup(struct v4l2_format *format)
{
	struct media_entity_desc entity;
	int fd, ret;

	fd = open("/dev/media0", O_RDWR);
	if (fd < 0) {
		ERROR("could not open MCF: %s (%d)", strerror(errno), ret);
		return fd;
	}

	ret = media_find_entity(fd, &entity, MEDIA_ENT_T_V4L2_SUBDEV_SENSOR, ~0);
	if (ret) {
		return ret;
	}

	/* now walk the graph to the output, configure everything on the way: */
	do {
		struct media_link_desc links[10];
		struct media_links_enum link_enum = {
				.entity = entity.id,
				.links = links,
		};
		int i;

		ret = ioctl(fd, MEDIA_IOC_ENUM_LINKS, &link_enum);
		if (ret) {
			ERROR("MEDIA_IOC_ENUM_LINKS failed: %s (%d)",
					strerror(errno), ret);
			return ret;
		}

		for (i = 0; i < entity.links; i++) {
			if (links[i].source.entity == entity.id) {
				// XXX maybe if there are multiple links, we should prefer
				// an enabled link, otherwise just pick one..

				media_configure(&entity, format, links[i].source.index);
				media_configure(&entity, format, links[i].sink.index);

				/* lets take this link.. */
				if (!(links[i].flags & MEDIA_LNK_FL_ENABLED)) {
					links[i].flags |= MEDIA_LNK_FL_ENABLED;
					ret = ioctl(fd, MEDIA_IOC_SETUP_LINK, &links[i]);
					if (ret) {
						ERROR("MEDIA_IOC_SETUP_LINK failed: %s (%d)",
								strerror(errno), errno);
//						return ret;
					}
				}

				ret = media_find_entity(fd, &entity, ~0, links[i].sink.entity);
				if (ret) {
					return ret;
				}

				break;
			}
		}
	} while (entity.type != MEDIA_ENT_T_DEVNODE_V4L);

	return 0;
}

/* Open v4l2 (and media0??) XXX */
struct v4l2 *
v4l2_open(int argc, char **argv, uint32_t *fourcc,
		uint32_t *width, uint32_t *height)
{
	struct v4l2_format format = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};
	struct v4l2 *v4l2;
	int i, ret;
	bool mcf = false;

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
		} else if (!strcmp(argv[i], "-m")) {
			mcf = true;
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

	*fourcc = format.fmt.pix.pixelformat;
	*width  = format.fmt.pix.width;
	*height = format.fmt.pix.height;

	if (mcf) {
		ret = media_setup(&format);
		if (ret < 0) {
			goto fail;
		}
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
	v4l2->bufs = bufs;
	v4l2->v4l2bufs = calloc(v4l2->nbufs, sizeof(*v4l2->v4l2bufs));
	if (!v4l2->v4l2bufs) {
		ERROR("allocation failed");
		return -1;
	}

	for (i = 0; i < reqbuf.count; i++) {
		assert(bufs[i]->nbo == 1); /* TODO add multi-planar support */
		v4l2->v4l2bufs[i] = (struct v4l2_buffer){
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
					.memory = V4L2_MEMORY_DMABUF,
					.index = i,
					.m.fd = omap_bo_dmabuf(bufs[i]->bo[0]),
		};
		ret = ioctl(v4l2->fd, VIDIOC_QUERYBUF, &v4l2->v4l2bufs[i]);
		v4l2->v4l2bufs[i].m.fd = omap_bo_dmabuf(bufs[i]->bo[0]);
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

	assert(buf->nbo == 1); /* TODO add multi-planar support */

	fd = omap_bo_dmabuf(buf->bo[0]);

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
	v4l2buf->m.fd = omap_bo_dmabuf(buf->bo[0]);
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

	buf = v4l2->bufs[v4l2buf.index];

	assert(buf->nbo == 1); /* TODO add multi-planar support */

	MSG("DQBUF: idx=%d, fd=%d", v4l2buf.index, omap_bo_dmabuf(buf->bo[0]));

	return buf;
}
