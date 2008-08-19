/*
 * Copyright 2007 Nouveau Project
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include "nouveau_drmif.h"
#include "nouveau_local.h"

static void
nouveau_mem_free(struct nouveau_device *dev, unsigned *handle, void **map,
		 unsigned size)
{
	struct nouveau_device_priv *nvdev = nouveau_device(dev);

	if (map && *map) {
		munmap(*map, size);
		*map = NULL;
	}

	if (handle && *handle) {
		struct drm_gem_close req;

		req.handle = *handle;
		*handle = 0;

		ioctl(nvdev->fd, DRM_IOCTL_GEM_CLOSE, &req);
	}
}

static int
nouveau_mem_alloc(struct nouveau_device *dev, unsigned size, unsigned align,
		  uint32_t flags, unsigned *handle, void **map)
{
	struct nouveau_device_priv *nvdev = nouveau_device(dev);
	struct drm_nouveau_gem_new req;
	int ret;

	req.size = size;
	req.align = align;
	req.domain = flags;
	ret = drmCommandWriteRead(nvdev->fd, DRM_NOUVEAU_GEM_NEW, &req,
				  sizeof(struct drm_nouveau_gem_new));
	if (ret)
		return ret;
	*handle = req.handle;

	if (map) {
		struct drm_nouveau_gem_mmap m_req;

		m_req.handle = req.handle;
		ret = drmCommandWriteRead(nvdev->fd, DRM_NOUVEAU_GEM_MMAP,
					  &m_req, sizeof(m_req));
		if (ret) {
			nouveau_mem_free(dev, handle, map, size);
			return ret;
		}

		*map = (void *)m_req.vaddr;
	}

	return 0;
}

int
nouveau_bo_pin(struct nouveau_bo *bo, uint32_t flags)
{
	struct nouveau_device_priv *nvdev = nouveau_device(bo->device);
	struct nouveau_bo_priv *nvbo = nouveau_bo(bo);
	struct drm_nouveau_gem_pin req;
	int ret;

	if (nvbo->pinned)
		return 0;

	req.handle = nvbo->handle;
	req.domain = 0;
	if (flags & NOUVEAU_BO_VRAM)
		req.domain |= NOUVEAU_GEM_DOMAIN_VRAM;
	if (flags & NOUVEAU_BO_GART)
		req.domain |= NOUVEAU_GEM_DOMAIN_GART;

	ret = drmCommandWriteRead(nvdev->fd, DRM_NOUVEAU_GEM_PIN, &req,
				  sizeof(struct drm_nouveau_gem_pin));
	if (ret)
		return ret;
	nvbo->offset = req.offset;
	nvbo->domain = req.domain;

	if (req.domain & NOUVEAU_GEM_DOMAIN_VRAM)
		bo->flags = NOUVEAU_BO_VRAM;
	if (req.domain & NOUVEAU_GEM_DOMAIN_GART)
		bo->flags = NOUVEAU_BO_GART;
	bo->offset = req.offset;
	nvbo->pinned = 1;

	return 0;
}

void
nouveau_bo_unpin(struct nouveau_bo *bo)
{
	struct nouveau_device_priv *nvdev = nouveau_device(bo->device);
	struct nouveau_bo_priv *nvbo = nouveau_bo(bo);
	struct drm_nouveau_gem_unpin req;

	if (!nvbo->pinned)
		return;

	req.handle = nvbo->handle;
	drmCommandWrite(nvdev->fd, DRM_NOUVEAU_GEM_UNPIN, &req, sizeof(req));

	nvbo->pinned = bo->offset = bo->flags = 0;
}

int
nouveau_bo_init(struct nouveau_device *dev)
{
	return 0;
}

void
nouveau_bo_takedown(struct nouveau_device *dev)
{
}

int
nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, int align,
	       int size, struct nouveau_bo **bo)
{
	struct nouveau_bo_priv *nvbo;
	int ret;

	if (!dev || !bo || *bo)
		return -EINVAL;

	nvbo = calloc(1, sizeof(struct nouveau_bo_priv));
	if (!nvbo)
		return -ENOMEM;
	nvbo->base.device = dev;
	nvbo->base.size = size;
	nvbo->base.handle = bo_to_ptr(nvbo);
	nvbo->size = size;
	nvbo->align = align;
	nvbo->refcount = 1;

	if (flags & NOUVEAU_BO_TILED) {
		nvbo->base.tiled = 1;
		if (flags & NOUVEAU_BO_ZTILE)
			nvbo->base.tiled |= 2;
		flags &= ~NOUVEAU_BO_TILED;
	}

	ret = nouveau_bo_set_status(&nvbo->base, flags);
	if (ret) {
		free(nvbo);
		return ret;
	}

	if (flags & NOUVEAU_BO_PIN) {
		ret = nouveau_bo_pin(&nvbo->base, flags);
		if (ret) {
			nouveau_bo_del((void *)&nvbo);
			return ret;
		}
	}

	*bo = &nvbo->base;
	return 0;
}

int
nouveau_bo_user(struct nouveau_device *dev, void *ptr, int size,
		struct nouveau_bo **bo)
{
	struct nouveau_bo_priv *nvbo;

	if (!dev || !bo || *bo)
		return -EINVAL;

	nvbo = calloc(1, sizeof(*nvbo));
	if (!nvbo)
		return -ENOMEM;
	nvbo->base.device = dev;
	
	nvbo->sysmem = ptr;
	nvbo->user = 1;

	nvbo->base.size = size;
	nvbo->base.handle = bo_to_ptr(nvbo);
	nvbo->refcount = 1;
	*bo = &nvbo->base;
	return 0;
}

int
nouveau_bo_ref(struct nouveau_device *dev, uint64_t handle,
	       struct nouveau_bo **bo)
{
	struct nouveau_bo_priv *nvbo = ptr_to_bo(handle);

	if (!dev || !bo || *bo)
		return -EINVAL;

	nvbo->refcount++;
	*bo = &nvbo->base;
	return 0;
}

static void
nouveau_bo_del_cb(void *priv)
{
	struct nouveau_bo_priv *nvbo = priv;

	nouveau_fence_ref(NULL, &nvbo->fence);
	nouveau_mem_free(nvbo->base.device, &nvbo->handle, &nvbo->map,
			 nvbo->size);
	if (nvbo->sysmem && !nvbo->user)
		free(nvbo->sysmem);
	free(nvbo);
}

void
nouveau_bo_del(struct nouveau_bo **bo)
{
	struct nouveau_bo_priv *nvbo;

	if (!bo || !*bo)
		return;
	nvbo = nouveau_bo(*bo);
	*bo = NULL;

	if (--nvbo->refcount)
		return;

	if (nvbo->pending)
		nouveau_pushbuf_flush(nvbo->pending_channel, 0);

	if (nvbo->fence)
		nouveau_fence_signal_cb(nvbo->fence, nouveau_bo_del_cb, nvbo);
	else
		nouveau_bo_del_cb(nvbo);
}

int
nouveau_bo_map(struct nouveau_bo *bo, uint32_t flags)
{
	struct nouveau_bo_priv *nvbo = nouveau_bo(bo);

	if (!nvbo)
		return -EINVAL;

	if (nvbo->pending &&
	    (nvbo->pending->access & NOUVEAU_GEM_ACCESS_WR ||
	     flags & NOUVEAU_BO_WR)) {
		nouveau_pushbuf_flush(nvbo->pending_channel, 0);
	}

	if (flags & NOUVEAU_BO_WR)
		nouveau_fence_wait(&nvbo->fence);
	else
		nouveau_fence_wait(&nvbo->wr_fence);

	if (nvbo->sysmem)
		bo->map = nvbo->sysmem;
	else
		bo->map = nvbo->map;
	return 0;
}

void
nouveau_bo_unmap(struct nouveau_bo *bo)
{
	bo->map = NULL;
}

uint64_t
nouveau_bo_get_drm_map(struct nouveau_bo *bo)
{
	NOUVEAU_ERR("-EINVAL :)\n");
	return 0;
}

int
nouveau_bo_set_status(struct nouveau_bo *bo, uint32_t flags)
{
	struct nouveau_bo_priv *nvbo = nouveau_bo(bo);
	unsigned new_handle = 0;
	void *new_map = NULL, *new_sysmem = NULL;
	unsigned new_domain = 0, ret;

	assert(!bo->map);

	/* Check current memtype vs requested, if they match do nothing */
	if ((nvbo->domain & NOUVEAU_GEM_DOMAIN_VRAM) &&
	    (flags & NOUVEAU_BO_VRAM))
		return 0;
	if ((nvbo->domain & (NOUVEAU_GEM_DOMAIN_GART)) &&
	    (flags & NOUVEAU_BO_GART))
		return 0;
	if (!nvbo->handle && nvbo->sysmem && (flags & NOUVEAU_BO_LOCAL))
		return 0;

	/* Allocate new memory */
	if (flags & NOUVEAU_BO_VRAM)
		new_domain |= NOUVEAU_GEM_DOMAIN_VRAM;
	else
	if (flags & NOUVEAU_BO_GART)
		new_domain |= NOUVEAU_GEM_DOMAIN_GART;
	
	if (nvbo->base.tiled && flags) {
		new_domain |= NOUVEAU_GEM_DOMAIN_TILE;
		if (nvbo->base.tiled & 2)
			new_domain |= NOUVEAU_GEM_DOMAIN_TILE_ZETA;
	}

	if (new_domain) {
		ret = nouveau_mem_alloc(bo->device, nvbo->size, nvbo->align,
					new_domain, &new_handle, &new_map);
		if (ret)
			return ret;
	} else
	if (!nvbo->user) {
		new_sysmem = malloc(bo->size);
	}

	/* Copy old -> new */
	/*XXX: use M2MF */
	if (nvbo->sysmem || nvbo->map) {
		struct drm_nouveau_gem_pushbuf_bo *pbo = nvbo->pending;
		nvbo->pending = NULL;
		nouveau_bo_map(bo, NOUVEAU_BO_RD);
		memcpy(new_map, bo->map, bo->size);
		nouveau_bo_unmap(bo);
		nvbo->pending = pbo;
	}

	/* Free old memory */
	if (nvbo->fence)
		nouveau_fence_wait(&nvbo->fence);
	nouveau_mem_free(bo->device, &nvbo->handle, &nvbo->map, nvbo->size);
	if (nvbo->sysmem && !nvbo->user)
		free(nvbo->sysmem);

	nvbo->handle = new_handle;
	nvbo->map = new_map;
	if (!nvbo->user)
		nvbo->sysmem = new_sysmem;

	return 0;
}


