#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "xorg-server.h"
#include "nv_include.h"
#ifdef DRI2
#include "dri2.h"
#endif

#if defined(DRI2) && DRI2INFOREC_VERSION >= 3
struct nouveau_dri2_buffer {
	DRI2BufferRec base;
	PixmapPtr ppix;
};

static inline struct nouveau_dri2_buffer *
nouveau_dri2_buffer(DRI2BufferPtr buf)
{
	return (struct nouveau_dri2_buffer *)buf;
}

DRI2BufferPtr
nouveau_dri2_create_buffer(DrawablePtr pDraw, unsigned int attachment,
			   unsigned int format)
{
	ScreenPtr pScreen = pDraw->pScreen;
	NVPtr pNv = NVPTR(xf86Screens[pScreen->myNum]);
	struct nouveau_dri2_buffer *nvbuf;
	PixmapPtr ppix;

	nvbuf = calloc(1, sizeof(*nvbuf));
	if (!nvbuf)
		return NULL;

	if (attachment == DRI2BufferFrontLeft) {
		if (pDraw->type == DRAWABLE_PIXMAP) {
			ppix = (PixmapPtr)pDraw;
		} else {
			WindowPtr pwin = (WindowPtr)pDraw;
			ppix = pScreen->GetWindowPixmap(pwin);
		}

		ppix->refcnt++;
	} else {
		unsigned int usage_hint = NOUVEAU_CREATE_PIXMAP_TILED;

		if (attachment == DRI2BufferDepth ||
		    attachment == DRI2BufferDepthStencil)
			usage_hint |= NOUVEAU_CREATE_PIXMAP_ZETA;
		else
			usage_hint |= NOUVEAU_CREATE_PIXMAP_SCANOUT;

		ppix = pScreen->CreatePixmap(pScreen, pDraw->width,
					     pDraw->height, pDraw->depth,
					     usage_hint);
	}

	pNv->exa_force_cp = TRUE;
	exaMoveInPixmap(ppix);
	pNv->exa_force_cp = FALSE;

	nvbuf->base.attachment = attachment;
	nvbuf->base.pitch = ppix->devKind;
	nvbuf->base.cpp = ppix->drawable.bitsPerPixel / 8;
	nvbuf->base.driverPrivate = nvbuf;
	nvbuf->base.format = format;
	nvbuf->base.flags = 0;
	nvbuf->ppix = ppix;

	nouveau_bo_handle_get(nouveau_pixmap(ppix)->bo, &nvbuf->base.name);
	return &nvbuf->base;
}

void
nouveau_dri2_destroy_buffer(DrawablePtr pDraw, DRI2BufferPtr buf)
{
	struct nouveau_dri2_buffer *nvbuf;

	nvbuf = nouveau_dri2_buffer(buf);
	if (!nvbuf)
		return;

	pDraw->pScreen->DestroyPixmap(nvbuf->ppix);
	free(nvbuf);
}

void
nouveau_dri2_copy_region(DrawablePtr pDraw, RegionPtr pRegion,
			 DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer)
{
	struct nouveau_dri2_buffer *src = nouveau_dri2_buffer(pSrcBuffer);
	struct nouveau_dri2_buffer *dst = nouveau_dri2_buffer(pDstBuffer);
	PixmapPtr pspix = src->ppix, pdpix = dst->ppix;
	ScreenPtr pScreen = pDraw->pScreen;
	RegionPtr pCopyClip;
	GCPtr pGC;

	if (src->base.attachment == DRI2BufferFrontLeft)
		pspix = (PixmapPtr)pDraw;
	if (dst->base.attachment == DRI2BufferFrontLeft)
		pdpix = (PixmapPtr)pDraw;

	pGC = GetScratchGC(pDraw->depth, pScreen);
	pCopyClip = REGION_CREATE(pScreen, NULL, 0);
	REGION_COPY(pScreen, pCopyClip, pRegion);
	pGC->funcs->ChangeClip(pGC, CT_REGION, pCopyClip, 0);
	ValidateGC(&pdpix->drawable, pGC);

	pGC->ops->CopyArea(&pspix->drawable, &pdpix->drawable, pGC, 0, 0,
			   pDraw->width, pDraw->height, 0, 0);

	FreeScratchGC(pGC);
}

struct nouveau_dri2_vblank_state {
	enum {
		SWAP,
		WAIT
	} action;

	ClientPtr client;
	XID draw;

	DRI2BufferPtr dst;
	DRI2BufferPtr src;
	DRI2SwapEventPtr func;
	void *data;
};

static Bool
can_exchange(DrawablePtr draw, PixmapPtr dst_pix, PixmapPtr src_pix)
{
	ScrnInfoPtr scrn = xf86Screens[draw->pScreen->myNum];
	NVPtr pNv = NVPTR(scrn);

	return (!nouveau_exa_pixmap_is_onscreen(dst_pix) ||
		(DRI2CanFlip(draw) && pNv->has_pageflip)) &&
		dst_pix->drawable.width == src_pix->drawable.width &&
		dst_pix->drawable.height == src_pix->drawable.height &&
		dst_pix->drawable.depth == src_pix->drawable.depth &&
		dst_pix->devKind == src_pix->devKind;
}

static Bool
can_sync_to_vblank(DrawablePtr draw)
{
	ScrnInfoPtr scrn = xf86Screens[draw->pScreen->myNum];
	NVPtr pNv = NVPTR(scrn);
	PixmapPtr pix = NVGetDrawablePixmap(draw);

	return pNv->glx_vblank &&
		nouveau_exa_pixmap_is_onscreen(pix) &&
		nv_window_belongs_to_crtc(scrn, draw->x, draw->y,
					  draw->width, draw->height);
}

static int
nouveau_wait_vblank(DrawablePtr draw, int type, CARD64 msc,
		    CARD64 *pmsc, CARD64 *pust, void *data)
{
	ScrnInfoPtr scrn = xf86Screens[draw->pScreen->myNum];
	NVPtr pNv = NVPTR(scrn);
	int crtcs = nv_window_belongs_to_crtc(scrn, draw->x, draw->y,
					      draw->width, draw->height);
	drmVBlank vbl;
	int ret;

	vbl.request.type = type | (crtcs == 2 ? DRM_VBLANK_SECONDARY : 0);
	vbl.request.sequence = msc;
	vbl.request.signal = (unsigned long)data;

	ret = drmWaitVBlank(nouveau_device(pNv->dev)->fd, &vbl);
	if (ret) {
		xf86DrvMsg(scrn->scrnIndex, X_WARNING,
			   "Wait for VBlank failed: %s\n", strerror(errno));
		return ret;
	}

	if (pmsc)
		*pmsc = vbl.reply.sequence;
	if (pust)
		*pust = (CARD64)vbl.reply.tval_sec * 1000000 +
			vbl.reply.tval_usec;
	return 0;
}

static void
nouveau_dri2_finish_swap(DrawablePtr draw, unsigned int frame,
			 unsigned int tv_sec, unsigned int tv_usec,
			 struct nouveau_dri2_vblank_state *s)
{
	ScrnInfoPtr scrn = xf86Screens[draw->pScreen->myNum];
	NVPtr pNv = NVPTR(scrn);
	PixmapPtr dst_pix = nouveau_dri2_buffer(s->dst)->ppix;
	PixmapPtr src_pix = nouveau_dri2_buffer(s->src)->ppix;
	struct nouveau_bo *dst_bo = nouveau_pixmap_bo(dst_pix);
	struct nouveau_bo *src_bo = nouveau_pixmap_bo(src_pix);
	struct nouveau_channel *chan = pNv->chan;
	RegionRec reg;
	int type, ret;

	REGION_INIT(0, &reg, (&(BoxRec){ 0, 0, draw->width, draw->height }), 0);
	REGION_TRANSLATE(0, &reg, draw->x, draw->y);

	/* Throttle on the previous frame before swapping */
	nouveau_bo_map(dst_bo, NOUVEAU_BO_RD);
	nouveau_bo_unmap(dst_bo);

	if (can_sync_to_vblank(draw)) {
		/* Reference the back buffer to sync it to vblank */
		WAIT_RING(chan, 1);
		OUT_RELOC(chan, src_bo, 0,
			  NOUVEAU_BO_VRAM | NOUVEAU_BO_RD, 0, 0);

		if (pNv->Architecture >= NV_ARCH_50)
			NV50SyncToVBlank(dst_pix, REGION_EXTENTS(0, &reg));
		else
			NV11SyncToVBlank(dst_pix, REGION_EXTENTS(0, &reg));

		FIRE_RING(chan);
	}

	if (can_exchange(draw, dst_pix, src_pix)) {
		type = DRI2_EXCHANGE_COMPLETE;
		DamageRegionAppend(draw, &reg);

		if (DRI2CanFlip(draw)) {
			type = DRI2_FLIP_COMPLETE;
			ret = drmmode_page_flip(draw, src_pix, s);
			if (!ret)
				goto out;
		}

		SWAP(s->dst->name, s->src->name);
		SWAP(nouveau_pixmap(dst_pix)->bo, nouveau_pixmap(src_pix)->bo);

		DamageRegionProcessPending(draw);
	} else {
		type = DRI2_BLIT_COMPLETE;

		/* Reference the front buffer to let throttling work
		 * on occluded drawables. */
		WAIT_RING(chan, 1);
		OUT_RELOC(chan, dst_bo, 0,
			  NOUVEAU_BO_VRAM | NOUVEAU_BO_RD, 0, 0);

		REGION_TRANSLATE(0, &reg, -draw->x, -draw->y);
		nouveau_dri2_copy_region(draw, &reg, s->dst, s->src);
	}

	/*
	 * Tell the X server buffers are already swapped even if they're
	 * not, to prevent it from blocking the client on the next
	 * GetBuffers request (and let the client do triple-buffering).
	 *
	 * XXX - The DRI2SwapLimit() API will allow us to move this to
	 *	 the flip handler with no FPS hit.
	 */
	DRI2SwapComplete(s->client, draw, frame, tv_sec, tv_usec,
			 type, s->func, s->data);
out:
	free(s);
}

static Bool
nouveau_dri2_schedule_swap(ClientPtr client, DrawablePtr draw,
			   DRI2BufferPtr dst, DRI2BufferPtr src,
			   CARD64 *target_msc, CARD64 divisor, CARD64 remainder,
			   DRI2SwapEventPtr func, void *data)
{
	struct nouveau_dri2_vblank_state *s;
	CARD64 current_msc;
	int ret;

	/* Initialize a swap structure */
	s = malloc(sizeof(*s));
	if (!s)
		return FALSE;

	*s = (struct nouveau_dri2_vblank_state)
		{ SWAP, client, draw->id, dst, src, func, data };

	if (can_sync_to_vblank(draw)) {
		/* Get current sequence */
		ret = nouveau_wait_vblank(draw, DRM_VBLANK_RELATIVE, 0,
					  &current_msc, NULL, NULL);
		if (ret)
			goto fail;

		/* Calculate a swap target if we don't have one */
		if (current_msc >= *target_msc && divisor)
			*target_msc = current_msc + divisor
				- (current_msc - remainder) % divisor;

		/* Request a vblank event one frame before the target */
		ret = nouveau_wait_vblank(draw, DRM_VBLANK_ABSOLUTE |
					  DRM_VBLANK_EVENT,
					  max(current_msc, *target_msc - 1),
					  NULL, NULL, s);
		if (ret)
			goto fail;

	} else {
		/* We can't/don't want to sync to vblank, just swap. */
		nouveau_dri2_finish_swap(draw, 0, 0, 0, s);
	}

	return TRUE;

fail:
	free(s);
	return FALSE;
}

static Bool
nouveau_dri2_schedule_wait(ClientPtr client, DrawablePtr draw,
			   CARD64 target_msc, CARD64 divisor, CARD64 remainder)
{
	struct nouveau_dri2_vblank_state *s;
	CARD64 current_msc;
	int ret;

	if (!can_sync_to_vblank(draw)) {
		DRI2WaitMSCComplete(client, draw, target_msc, 0, 0);
		return TRUE;
	}

	/* Initialize a vblank structure */
	s = malloc(sizeof(*s));
	if (!s)
		return FALSE;

	*s = (struct nouveau_dri2_vblank_state) { WAIT, client, draw->id };

	/* Get current sequence */
	ret = nouveau_wait_vblank(draw, DRM_VBLANK_RELATIVE, 0,
				  &current_msc, NULL, NULL);
	if (ret)
		goto fail;

	/* Calculate a wait target if we don't have one */
	if (current_msc > target_msc && divisor)
		target_msc = current_msc + divisor
			- (current_msc - remainder) % divisor;

	/* Request a vblank event */
	ret = nouveau_wait_vblank(draw, DRM_VBLANK_ABSOLUTE |
				  DRM_VBLANK_EVENT,
				  max(current_msc, target_msc),
				  NULL, NULL, s);
	if (ret)
		goto fail;

	DRI2BlockClient(client, draw);
	return TRUE;
fail:
	free(s);
	return FALSE;
}

static Bool
nouveau_dri2_get_msc(DrawablePtr draw, CARD64 *ust, CARD64 *msc)
{
	int ret;

	if (!can_sync_to_vblank(draw)) {
		*ust = 0;
		*msc = 0;
		return TRUE;
	}

	/* Get current sequence */
	ret = nouveau_wait_vblank(draw, DRM_VBLANK_RELATIVE, 0, msc, ust, NULL);
	if (ret)
		return FALSE;

	return TRUE;
}

void
nouveau_dri2_vblank_handler(int fd, unsigned int frame,
			    unsigned int tv_sec, unsigned int tv_usec,
			    void *event_data)
{
	struct nouveau_dri2_vblank_state *s = event_data;
	DrawablePtr draw;
	int ret;

	ret = dixLookupDrawable(&draw, s->draw, serverClient,
				M_ANY, DixWriteAccess);
	if (ret)
		return;

	switch (s->action) {
	case SWAP:
		nouveau_dri2_finish_swap(draw, frame, tv_sec, tv_usec, s);
		break;

	case WAIT:
		DRI2WaitMSCComplete(s->client, draw, frame, tv_sec, tv_usec);
		free(s);
		break;
	}
}

Bool
nouveau_dri2_init(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	DRI2InfoRec dri2 = { 0 };

	if (pNv->Architecture >= NV_ARCH_30)
		dri2.driverName = "nouveau";
	else
		dri2.driverName = "nouveau_vieux";

	dri2.fd = nouveau_device(pNv->dev)->fd;
	dri2.deviceName = pNv->drm_device_name;

	dri2.version = DRI2INFOREC_VERSION;
	dri2.CreateBuffer = nouveau_dri2_create_buffer;
	dri2.DestroyBuffer = nouveau_dri2_destroy_buffer;
	dri2.CopyRegion = nouveau_dri2_copy_region;
	dri2.ScheduleSwap = nouveau_dri2_schedule_swap;
	dri2.ScheduleWaitMSC = nouveau_dri2_schedule_wait;
	dri2.GetMSC = nouveau_dri2_get_msc;

	return DRI2ScreenInit(pScreen, &dri2);
}

void
nouveau_dri2_fini(ScreenPtr pScreen)
{
	DRI2CloseScreen(pScreen);
}
#else
Bool
nouveau_dri2_init(ScreenPtr pScreen)
{
	return TRUE;
}

void
nouveau_dri2_fini(ScreenPtr pScreen)
{
}
#endif

