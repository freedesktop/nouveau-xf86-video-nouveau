/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "nv_include.h"
#include "dri3.h"

static int
nouveau_dri3_open(ScreenPtr screen, RRProviderPtr provider, int *fd)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	NVPtr pNv = NVPTR(scrn);

	*fd = open(pNv->drm_device_name, O_RDWR | O_CLOEXEC, 0);
	if ((*fd) < 0)
		return BadAlloc;

	return Success;
}

static PixmapPtr
nouveau_dri3_import(ScreenPtr screen, int fd, CARD16 width, CARD16 height,
		    CARD16 stride, CARD8 depth, CARD8 bpp)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	NVPtr pNv = NVPTR(scrn);
	PixmapPtr ppix;
	struct nouveau_pixmap *priv;
	int ret;

	ppix = screen->CreatePixmap(screen, 0, 0, depth, 0);
	priv = ppix ? nouveau_pixmap(ppix) : NULL;
	if (!priv)
		return NULL;

	ret = nouveau_bo_prime_handle_ref(pNv->dev, fd, &priv->bo);
	if (ret) {
		screen->DestroyPixmap(ppix);
		return NULL;
	}

	screen->ModifyPixmapHeader(ppix, width, height, 0, 0, stride, NULL);
	return ppix;
}

static int
nouveau_dri3_export(ScreenPtr screen, PixmapPtr pixmap,
		    CARD16 *stride, CARD32 *size)
{
	struct nouveau_pixmap *priv = nouveau_pixmap(pixmap);
	if (priv && priv->bo) {
		int fd, ret = nouveau_bo_set_prime(priv->bo, &fd);
		if (ret == 0) {
			*stride = exaGetPixmapPitch(pixmap);
			*size = priv->bo->size;
		}
		return ret ? ret : fd;
	}
	return -EINVAL;
}

static struct dri3_screen_info
nouveau_dri3 = {
	.version = DRI3_SCREEN_INFO_VERSION,
	.open = nouveau_dri3_open,
	.pixmap_from_fd = nouveau_dri3_import,
	.fd_from_pixmap = nouveau_dri3_export,
};

void
nouveau_dri3_fini(ScreenPtr screen)
{
}

Bool
nouveau_dri3_init(ScreenPtr screen)
{
	return dri3_screen_init(screen, &nouveau_dri3);
}
