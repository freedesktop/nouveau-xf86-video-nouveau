/*
 * Copyright © 2007 Red Hat, Inc.
 * Copyright © 2008 Maarten Maathuis
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xorgVersion.h"

#include "nv_include.h"
#include "xf86drmMode.h"
#include "X11/Xatom.h"

#include <sys/ioctl.h>
#ifdef HAVE_LIBUDEV
#include "libudev.h"
#endif

typedef struct {
    int fd;
    uint32_t fb_id;
    drmModeResPtr mode_res;
    int cpp;
    drmEventContext event_context;
#ifdef HAVE_LIBUDEV
    struct udev_monitor *uevent_monitor;
#endif
} drmmode_rec, *drmmode_ptr;

typedef struct {
    drmmode_ptr drmmode;
    drmModeCrtcPtr mode_crtc;
    struct nouveau_bo *cursor;
    struct nouveau_bo *rotate_bo;
    int rotate_pitch;
    PixmapPtr rotate_pixmap;
    uint32_t rotate_fb_id;
    Bool cursor_visible;
} drmmode_crtc_private_rec, *drmmode_crtc_private_ptr;

typedef struct {
	drmModePropertyPtr mode_prop;
	int index; /* Index within the kernel-side property arrays for
		    * this connector. */
	int num_atoms; /* if range prop, num_atoms == 1; if enum prop,
			* num_atoms == num_enums + 1 */
	Atom *atoms;
} drmmode_prop_rec, *drmmode_prop_ptr;

typedef struct {
    drmmode_ptr drmmode;
    int output_id;
    drmModeConnectorPtr mode_output;
    drmModeEncoderPtr mode_encoder;
    drmModePropertyBlobPtr edid_blob;
    int num_props;
    drmmode_prop_ptr props;
} drmmode_output_private_rec, *drmmode_output_private_ptr;

static void drmmode_output_dpms(xf86OutputPtr output, int mode);

static drmmode_ptr
drmmode_from_scrn(ScrnInfoPtr scrn)
{
	if (scrn) {
		xf86CrtcConfigPtr conf = XF86_CRTC_CONFIG_PTR(scrn);
		drmmode_crtc_private_ptr crtc = conf->crtc[0]->driver_private;

		return crtc->drmmode;
	}

	return NULL;
}

static PixmapPtr
drmmode_pixmap_wrap(ScreenPtr pScreen, int width, int height, int depth,
		    int bpp, int pitch, struct nouveau_bo *bo, void *data)
{
	NVPtr pNv = NVPTR(xf86Screens[pScreen->myNum]);
	PixmapPtr ppix;

	if (!pNv->NoAccel)
		data = NULL;

	ppix = pScreen->CreatePixmap(pScreen, 0, 0, depth, 0);
	if (!ppix)
		return NULL;

	pScreen->ModifyPixmapHeader(ppix, width, height, depth, bpp,
				    pitch, data);
	if (!pNv->NoAccel)
		nouveau_bo_ref(bo, &nouveau_pixmap(ppix)->bo);

	return ppix;
}

static void
drmmode_ConvertFromKMode(ScrnInfoPtr scrn, drmModeModeInfo *kmode,
			 DisplayModePtr	mode)
{
	memset(mode, 0, sizeof(DisplayModeRec));
	mode->status = MODE_OK;

	mode->Clock = kmode->clock;

	mode->HDisplay = kmode->hdisplay;
	mode->HSyncStart = kmode->hsync_start;
	mode->HSyncEnd = kmode->hsync_end;
	mode->HTotal = kmode->htotal;
	mode->HSkew = kmode->hskew;

	mode->VDisplay = kmode->vdisplay;
	mode->VSyncStart = kmode->vsync_start;
	mode->VSyncEnd = kmode->vsync_end;
	mode->VTotal = kmode->vtotal;
	mode->VScan = kmode->vscan;

	mode->Flags = kmode->flags; //& FLAG_BITS;
	mode->name = strdup(kmode->name);

	if (kmode->type & DRM_MODE_TYPE_DRIVER)
		mode->type = M_T_DRIVER;
	if (kmode->type & DRM_MODE_TYPE_PREFERRED)
		mode->type |= M_T_PREFERRED;
	xf86SetModeCrtc (mode, scrn->adjustFlags);
}

static void
drmmode_ConvertToKMode(ScrnInfoPtr scrn, drmModeModeInfo *kmode,
		       DisplayModePtr mode)
{
	memset(kmode, 0, sizeof(*kmode));

	kmode->clock = mode->Clock;
	kmode->hdisplay = mode->HDisplay;
	kmode->hsync_start = mode->HSyncStart;
	kmode->hsync_end = mode->HSyncEnd;
	kmode->htotal = mode->HTotal;
	kmode->hskew = mode->HSkew;

	kmode->vdisplay = mode->VDisplay;
	kmode->vsync_start = mode->VSyncStart;
	kmode->vsync_end = mode->VSyncEnd;
	kmode->vtotal = mode->VTotal;
	kmode->vscan = mode->VScan;

	kmode->flags = mode->Flags; //& FLAG_BITS;
	if (mode->name)
		strncpy(kmode->name, mode->name, DRM_DISPLAY_MODE_LEN);
	kmode->name[DRM_DISPLAY_MODE_LEN-1] = 0;

}

static void
drmmode_crtc_dpms(xf86CrtcPtr drmmode_crtc, int mode)
{

}

void
drmmode_fbcon_copy(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
#if XORG_VERSION_CURRENT >= 10999001
	ExaDriverPtr exa = pNv->EXADriverPtr;
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	struct nouveau_bo *bo = NULL;
	PixmapPtr pspix, pdpix;
	drmModeFBPtr fb;
	unsigned w = pScrn->virtualX, h = pScrn->virtualY;
	int i, ret, fbcon_id = 0;

	if (pNv->NoAccel)
		goto fallback;

	for (i = 0; i < xf86_config->num_crtc; i++) {
		drmmode_crtc_private_ptr drmmode_crtc =
			xf86_config->crtc[i]->driver_private;

		if (drmmode_crtc->mode_crtc->buffer_id)
			fbcon_id = drmmode_crtc->mode_crtc->buffer_id;
	}

	if (!fbcon_id)
		goto fallback;

	fb = drmModeGetFB(nouveau_device(pNv->dev)->fd, fbcon_id);
	if (!fb) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Failed to retrieve fbcon fb: id %d\n", fbcon_id);
		goto fallback;
	}

	if (fb->depth != pScrn->depth || fb->width != w || fb->height != h) {
		drmFree(fb);
		goto fallback;
	}

	ret = nouveau_bo_wrap(pNv->dev, fb->handle, &bo);
	if (ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Failed to retrieve fbcon buffer: handle=0x%08x\n",
			   fb->handle);
		drmFree(fb);
		goto fallback;
	}

	pspix = drmmode_pixmap_wrap(pScreen, fb->width, fb->height,
				    fb->depth, fb->bpp, fb->pitch, bo, NULL);
	nouveau_bo_ref(NULL, &bo);
	drmFree(fb);
	if (!pspix) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Failed to create pixmap for fbcon contents\n");
		goto fallback;
	}

	pdpix = drmmode_pixmap_wrap(pScreen, pScrn->virtualX,
				    pScrn->virtualY, pScrn->depth,
				    pScrn->bitsPerPixel, pScrn->displayWidth *
				    pScrn->bitsPerPixel / 8, pNv->scanout,
				    NULL);
	if (!pdpix) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Failed to init scanout pixmap for fbcon mirror\n");
		pScreen->DestroyPixmap(pspix);
		goto fallback;
	}

	exa->PrepareCopy(pspix, pdpix, 0, 0, GXcopy, ~0);
	exa->Copy(pdpix, 0, 0, 0, 0, w, h);
	exa->DoneCopy(pdpix);
	FIRE_RING (pNv->chan);

	/* wait for completion before continuing, avoids seeing a momentary
	 * flash of "corruption" on occasion
	 */
	nouveau_bo_map(pNv->scanout, NOUVEAU_BO_RDWR);
	nouveau_bo_unmap(pNv->scanout);

	pScreen->DestroyPixmap(pdpix);
	pScreen->DestroyPixmap(pspix);
	pScreen->canDoBGNoneRoot = TRUE;
	return;

fallback:
#endif
	if (nouveau_bo_map(pNv->scanout, NOUVEAU_BO_WR))
		return;
	memset(pNv->scanout->map, 0x00, pNv->scanout->size);
	nouveau_bo_unmap(pNv->scanout);
}

static Bool
drmmode_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
		       Rotation rotation, int x, int y)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	uint32_t *output_ids;
	int output_count = 0;
	int ret = TRUE;
	int i;
	int fb_id;
	drmModeModeInfo kmode;

	if (drmmode->fb_id == 0) {
		unsigned int pitch =
			pScrn->displayWidth * (pScrn->bitsPerPixel / 8);

		ret = drmModeAddFB(drmmode->fd,
				   pScrn->virtualX, pScrn->virtualY,
				   pScrn->depth, pScrn->bitsPerPixel,
				   pitch, pNv->scanout->handle,
				   &drmmode->fb_id);
		if (ret < 0) {
			ErrorF("failed to add fb\n");
			return FALSE;
		}
	}

	if (!xf86CrtcRotate(crtc))
		return FALSE;

	output_ids = calloc(sizeof(uint32_t), xf86_config->num_output);
	if (!output_ids)
		return FALSE;

	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		drmmode_output_private_ptr drmmode_output;

		if (output->crtc != crtc)
			continue;

		drmmode_output = output->driver_private;
		output_ids[output_count] =
			drmmode_output->mode_output->connector_id;
		output_count++;
	}

	drmmode_ConvertToKMode(crtc->scrn, &kmode, mode);

	fb_id = drmmode->fb_id;
	if (drmmode_crtc->rotate_fb_id) {
		fb_id = drmmode_crtc->rotate_fb_id;
		x = 0;
		y = 0;
	}

	ret = drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
			     fb_id, x, y, output_ids, output_count, &kmode);
	free(output_ids);

	if (ret) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
			   "failed to set mode: %s", strerror(-ret));
		return FALSE;
	}

	/* Work around some xserver stupidity */
	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];

		if (output->crtc != crtc)
			continue;

		drmmode_output_dpms(output, DPMSModeOn);
	}

	crtc->funcs->gamma_set(crtc, crtc->gamma_red, crtc->gamma_green,
			       crtc->gamma_blue, crtc->gamma_size);

	xf86_reload_cursors(crtc->scrn->pScreen);

	return TRUE;
}

static void
drmmode_set_cursor_position (xf86CrtcPtr crtc, int x, int y)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;

	drmModeMoveCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, x, y);
}

static void
convert_cursor(CARD32 *dst, CARD32 *src, int dw, int sw)
{
	int i, j;

	for (j = 0;  j < sw; j++) {
		for (i = 0; i < sw; i++) {
			dst[j * dw + i] = src[j * sw + i];
		}
	}
}

static void
drmmode_load_cursor_argb (xf86CrtcPtr crtc, CARD32 *image)
{
	NVPtr pNv = NVPTR(crtc->scrn);
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	struct nouveau_bo *cursor = drmmode_crtc->cursor;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;

	nouveau_bo_map(cursor, NOUVEAU_BO_WR);
	convert_cursor(cursor->map, image, 64, nv_cursor_width(pNv));
	nouveau_bo_unmap(cursor);

	if (drmmode_crtc->cursor_visible) {
		drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
				 cursor->handle, 64, 64);
	}
}

static void
drmmode_hide_cursor (xf86CrtcPtr crtc)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;

	drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
			 0, 64, 64);
	drmmode_crtc->cursor_visible = FALSE;
}

static void
drmmode_show_cursor (xf86CrtcPtr crtc)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;

	drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
			 drmmode_crtc->cursor->handle, 64, 64);
	drmmode_crtc->cursor_visible = TRUE;
}

static void *
drmmode_crtc_shadow_allocate(xf86CrtcPtr crtc, int width, int height)
{
	ScrnInfoPtr scrn = crtc->scrn;
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	void *virtual;
	int ret;

	ret = nouveau_allocate_surface(scrn, width, height,
				       scrn->bitsPerPixel,
				       NOUVEAU_CREATE_PIXMAP_SCANOUT,
				       &drmmode_crtc->rotate_pitch,
				       &drmmode_crtc->rotate_bo);
	if (!ret) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
			   "Couldn't allocate shadow memory for rotated CRTC\n");
		return NULL;
	}

	ret = nouveau_bo_map(drmmode_crtc->rotate_bo, NOUVEAU_BO_RDWR);
	if (ret) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
			   "Couldn't get virtual address of shadow scanout\n");
		nouveau_bo_ref(NULL, &drmmode_crtc->rotate_bo);
		return NULL;
	}
	virtual = drmmode_crtc->rotate_bo->map;
	nouveau_bo_unmap(drmmode_crtc->rotate_bo);

	ret = drmModeAddFB(drmmode->fd, width, height, crtc->scrn->depth,
			   crtc->scrn->bitsPerPixel, drmmode_crtc->rotate_pitch,
			   drmmode_crtc->rotate_bo->handle,
			   &drmmode_crtc->rotate_fb_id);
	if (ret) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
			   "Error adding FB for shadow scanout: %s\n",
			   strerror(-ret));
		nouveau_bo_ref(NULL, &drmmode_crtc->rotate_bo);
		return NULL;
	}

	return virtual;
}

static PixmapPtr
drmmode_crtc_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	PixmapPtr rotate_pixmap;

	if (!data)
		data = drmmode_crtc_shadow_allocate (crtc, width, height);

	rotate_pixmap = drmmode_pixmap_wrap(pScrn->pScreen, width, height,
					    pScrn->depth, pScrn->bitsPerPixel,
					    drmmode_crtc->rotate_pitch,
					    drmmode_crtc->rotate_bo, data);

	drmmode_crtc->rotate_pixmap = rotate_pixmap;
	return drmmode_crtc->rotate_pixmap;
}

static void
drmmode_crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	NVPtr pNv = NVPTR(crtc->scrn);

	if (rotate_pixmap)
		FreeScratchPixmapHeader(rotate_pixmap);

	if (data) {
		drmModeRmFB(drmmode->fd, drmmode_crtc->rotate_fb_id);
		drmmode_crtc->rotate_fb_id = 0;
		if (!pNv->NoAccel)
			nouveau_bo_ref(NULL, &drmmode_crtc->rotate_bo);
		drmmode_crtc->rotate_pixmap = NULL;
	}
}

static void
drmmode_gamma_set(xf86CrtcPtr crtc, CARD16 *red, CARD16 *green, CARD16 *blue,
		  int size)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	int ret;

	ret = drmModeCrtcSetGamma(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
				  size, red, green, blue);
	if (ret != 0) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
			   "failed to set gamma: %s", strerror(-ret));
	}
}

static const xf86CrtcFuncsRec drmmode_crtc_funcs = {
	.dpms = drmmode_crtc_dpms,
	.set_mode_major = drmmode_set_mode_major,
	.set_cursor_position = drmmode_set_cursor_position,
	.show_cursor = drmmode_show_cursor,
	.hide_cursor = drmmode_hide_cursor,
	.load_cursor_argb = drmmode_load_cursor_argb,
	.shadow_create = drmmode_crtc_shadow_create,
	.shadow_allocate = drmmode_crtc_shadow_allocate,
	.shadow_destroy = drmmode_crtc_shadow_destroy,
	.gamma_set = drmmode_gamma_set,
};


static void
drmmode_crtc_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int num)
{
	NVPtr pNv = NVPTR(pScrn);
	xf86CrtcPtr crtc;
	drmmode_crtc_private_ptr drmmode_crtc;
	int ret;

	crtc = xf86CrtcCreate(pScrn, &drmmode_crtc_funcs);
	if (crtc == NULL)
		return;

	drmmode_crtc = xnfcalloc(sizeof(drmmode_crtc_private_rec), 1);
	drmmode_crtc->mode_crtc = drmModeGetCrtc(drmmode->fd,
						 drmmode->mode_res->crtcs[num]);
	drmmode_crtc->drmmode = drmmode;

	ret = nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_MAP, 0,
			     64*64*4, &drmmode_crtc->cursor);
	assert(ret == 0);

	crtc->driver_private = drmmode_crtc;

	return;
}

static xf86OutputStatus
drmmode_output_detect(xf86OutputPtr output)
{
	/* go to the hw and retrieve a new output struct */
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	xf86OutputStatus status;
	drmModeFreeConnector(drmmode_output->mode_output);

	drmmode_output->mode_output =
		drmModeGetConnector(drmmode->fd, drmmode_output->output_id);

	switch (drmmode_output->mode_output->connection) {
	case DRM_MODE_CONNECTED:
		status = XF86OutputStatusConnected;
		break;
	case DRM_MODE_DISCONNECTED:
		status = XF86OutputStatusDisconnected;
		break;
	default:
	case DRM_MODE_UNKNOWNCONNECTION:
		status = XF86OutputStatusUnknown;
		break;
	}
	return status;
}

static Bool
drmmode_output_mode_valid(xf86OutputPtr output, DisplayModePtr mode)
{
	if (mode->type & M_T_DEFAULT)
		/* Default modes are harmful here. */
		return MODE_BAD;

	return MODE_OK;
}

static DisplayModePtr
drmmode_output_get_modes(xf86OutputPtr output)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmModeConnectorPtr koutput = drmmode_output->mode_output;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	int i;
	DisplayModePtr Modes = NULL, Mode;
	drmModePropertyPtr props;
	xf86MonPtr ddc_mon = NULL;

	/* look for an EDID property */
	for (i = 0; i < koutput->count_props; i++) {
		props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
		if (!props || !(props->flags & DRM_MODE_PROP_BLOB))
			continue;

		if (!strcmp(props->name, "EDID")) {
			if (drmmode_output->edid_blob)
				drmModeFreePropertyBlob(drmmode_output->edid_blob);
			drmmode_output->edid_blob =
				drmModeGetPropertyBlob(drmmode->fd,
						       koutput->prop_values[i]);
		}
		drmModeFreeProperty(props);
	}

	if (drmmode_output->edid_blob)
		ddc_mon = xf86InterpretEDID(output->scrn->scrnIndex,
					    drmmode_output->edid_blob->data);
	xf86OutputSetEDID(output, ddc_mon);

	/* modes should already be available */
	for (i = 0; i < koutput->count_modes; i++) {
		Mode = xnfalloc(sizeof(DisplayModeRec));

		drmmode_ConvertFromKMode(output->scrn, &koutput->modes[i],
					 Mode);
		Modes = xf86ModesAdd(Modes, Mode);

	}
	return Modes;
}

static void
drmmode_output_destroy(xf86OutputPtr output)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	int i;

	if (drmmode_output->edid_blob)
		drmModeFreePropertyBlob(drmmode_output->edid_blob);
	for (i = 0; i < drmmode_output->num_props; i++) {
		drmModeFreeProperty(drmmode_output->props[i].mode_prop);
		free(drmmode_output->props[i].atoms);
	}
	drmModeFreeConnector(drmmode_output->mode_output);
	free(drmmode_output);
	output->driver_private = NULL;
}

static void
drmmode_output_dpms(xf86OutputPtr output, int mode)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmModeConnectorPtr koutput = drmmode_output->mode_output;
	drmModePropertyPtr props;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	int mode_id = -1, i;

	for (i = 0; i < koutput->count_props; i++) {
		props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
		if (props && (props->flags && DRM_MODE_PROP_ENUM)) {
			if (!strcmp(props->name, "DPMS")) {
				mode_id = koutput->props[i];
				drmModeFreeProperty(props);
				break;
			}
			drmModeFreeProperty(props);
		}
	}

	if (mode_id < 0)
		return;

	drmModeConnectorSetProperty(drmmode->fd, koutput->connector_id,
				    mode_id, mode);
}

static Bool
drmmode_property_ignore(drmModePropertyPtr prop)
{
	if (!prop)
	    return TRUE;
	/* ignore blob prop */
	if (prop->flags & DRM_MODE_PROP_BLOB)
		return TRUE;
	/* ignore standard property */
	if (!strcmp(prop->name, "EDID") ||
	    !strcmp(prop->name, "DPMS"))
		return TRUE;

	return FALSE;
}

static void
drmmode_output_create_resources(xf86OutputPtr output)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmModeConnectorPtr mode_output = drmmode_output->mode_output;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	drmModePropertyPtr drmmode_prop;
	uint32_t value;
	int i, j, err;

	drmmode_output->props = calloc(mode_output->count_props, sizeof(drmmode_prop_rec));
	if (!drmmode_output->props)
		return;

	drmmode_output->num_props = 0;
	for (i = 0, j = 0; i < mode_output->count_props; i++) {
		drmmode_prop = drmModeGetProperty(drmmode->fd, mode_output->props[i]);
		if (drmmode_property_ignore(drmmode_prop)) {
			drmModeFreeProperty(drmmode_prop);
			continue;
		}
		drmmode_output->props[j].mode_prop = drmmode_prop;
		drmmode_output->props[j].index = i;
		drmmode_output->num_props++;
		j++;
	}

	for (i = 0; i < drmmode_output->num_props; i++) {
		drmmode_prop_ptr p = &drmmode_output->props[i];
		drmmode_prop = p->mode_prop;

		value = drmmode_output->mode_output->prop_values[p->index];

		if (drmmode_prop->flags & DRM_MODE_PROP_RANGE) {
			INT32 range[2];

			p->num_atoms = 1;
			p->atoms = calloc(p->num_atoms, sizeof(Atom));
			if (!p->atoms)
				continue;
			p->atoms[0] = MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
			range[0] = drmmode_prop->values[0];
			range[1] = drmmode_prop->values[1];
			err = RRConfigureOutputProperty(output->randr_output, p->atoms[0],
							FALSE, TRUE,
							drmmode_prop->flags & DRM_MODE_PROP_IMMUTABLE ? TRUE : FALSE,
							2, range);
			if (err != 0) {
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRConfigureOutputProperty error, %d\n", err);
			}
			err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
						     XA_INTEGER, 32, PropModeReplace, 1,
						     &value, FALSE, FALSE);
			if (err != 0) {
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRChangeOutputProperty error, %d\n", err);
			}
		} else if (drmmode_prop->flags & DRM_MODE_PROP_ENUM) {
			p->num_atoms = drmmode_prop->count_enums + 1;
			p->atoms = calloc(p->num_atoms, sizeof(Atom));
			if (!p->atoms)
				continue;
			p->atoms[0] = MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
			for (j = 1; j <= drmmode_prop->count_enums; j++) {
				struct drm_mode_property_enum *e = &drmmode_prop->enums[j-1];
				p->atoms[j] = MakeAtom(e->name, strlen(e->name), TRUE);
			}
			err = RRConfigureOutputProperty(output->randr_output, p->atoms[0],
							FALSE, FALSE,
							drmmode_prop->flags & DRM_MODE_PROP_IMMUTABLE ? TRUE : FALSE,
							p->num_atoms - 1, (INT32 *)&p->atoms[1]);
			if (err != 0) {
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRConfigureOutputProperty error, %d\n", err);
			}
			for (j = 0; j < drmmode_prop->count_enums; j++)
				if (drmmode_prop->enums[j].value == value)
					break;
			/* there's always a matching value */
			err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
						     XA_ATOM, 32, PropModeReplace, 1, &p->atoms[j+1], FALSE, FALSE);
			if (err != 0) {
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRChangeOutputProperty error, %d\n", err);
			}
		}
	}
}

static Bool
drmmode_output_set_property(xf86OutputPtr output, Atom property,
			    RRPropertyValuePtr value)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	int i, ret;

	for (i = 0; i < drmmode_output->num_props; i++) {
		drmmode_prop_ptr p = &drmmode_output->props[i];

		if (p->atoms[0] != property)
			continue;

		if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
			uint32_t val;

			if (value->type != XA_INTEGER || value->format != 32 ||
			    value->size != 1)
				return FALSE;
			val = *(uint32_t *)value->data;

			ret = drmModeConnectorSetProperty(drmmode->fd, drmmode_output->output_id,
							  p->mode_prop->prop_id, (uint64_t)val);

			if (ret)
				return FALSE;

			return TRUE;

		} else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
			Atom	atom;
			const char	*name;
			int		j;

			if (value->type != XA_ATOM || value->format != 32 || value->size != 1)
				return FALSE;
			memcpy(&atom, value->data, 4);
			name = NameForAtom(atom);

			/* search for matching name string, then set its value down */
			for (j = 0; j < p->mode_prop->count_enums; j++) {
				if (!strcmp(p->mode_prop->enums[j].name, name)) {
					ret = drmModeConnectorSetProperty(drmmode->fd,
									  drmmode_output->output_id,
									  p->mode_prop->prop_id,
									  p->mode_prop->enums[j].value);

					if (ret)
						return FALSE;

					return TRUE;
				}
			}

			return FALSE;
		}
	}

	return TRUE;
}

static Bool
drmmode_output_get_property(xf86OutputPtr output, Atom property)
{

	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	uint32_t value;
	int err, i;

	if (output->scrn->vtSema) {
		drmModeFreeConnector(drmmode_output->mode_output);
		drmmode_output->mode_output =
			drmModeGetConnector(drmmode->fd, drmmode_output->output_id);
	}

	for (i = 0; i < drmmode_output->num_props; i++) {
		drmmode_prop_ptr p = &drmmode_output->props[i];
		if (p->atoms[0] != property)
			continue;

		value = drmmode_output->mode_output->prop_values[p->index];

		if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
			err = RRChangeOutputProperty(output->randr_output,
						     property, XA_INTEGER, 32,
						     PropModeReplace, 1, &value,
						     FALSE, FALSE);

			return !err;
		} else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
			int		j;

			/* search for matching name string, then set its value down */
			for (j = 0; j < p->mode_prop->count_enums; j++) {
				if (p->mode_prop->enums[j].value == value)
					break;
			}

			err = RRChangeOutputProperty(output->randr_output, property,
						     XA_ATOM, 32, PropModeReplace, 1,
						     &p->atoms[j+1], FALSE, FALSE);

			return !err;
		}
	}

	return FALSE;
}

static const xf86OutputFuncsRec drmmode_output_funcs = {
	.create_resources = drmmode_output_create_resources,
	.dpms = drmmode_output_dpms,
	.detect = drmmode_output_detect,
	.mode_valid = drmmode_output_mode_valid,
	.get_modes = drmmode_output_get_modes,
	.set_property = drmmode_output_set_property,
	.get_property = drmmode_output_get_property,
	.destroy = drmmode_output_destroy
};

static int subpixel_conv_table[7] = { 0, SubPixelUnknown,
				      SubPixelHorizontalRGB,
				      SubPixelHorizontalBGR,
				      SubPixelVerticalRGB,
				      SubPixelVerticalBGR,
				      SubPixelNone };

const char *output_names[] = { "None",
			       "VGA",
			       "DVI-I",
			       "DVI-D",
			       "DVI-A",
			       "Composite",
			       "SVIDEO",
			       "LVDS",
			       "CTV",
			       "DIN",
			       "DP",
			       "HDMI",
			       "HDMI",
			       "TV",
			       "eDP",
};
#define NUM_OUTPUT_NAMES (sizeof(output_names) / sizeof(output_names[0]))

static Bool
drmmode_zaphod_match(ScrnInfoPtr pScrn, const char *s, char *output_name)
{
    int i = 0;
    char s1[20];

    do {
	switch(*s) {
	case ',':
	    s1[i] = '\0';
	    i = 0;
	    if (strcmp(s1, output_name) == 0)
		return TRUE;
	    break;
	case ' ':
	case '\t':
	case '\n':
	case '\r':
	    break;
	default:
	    s1[i] = *s;
	    i++;
	    break;
	}
    } while(*s++);

    s1[i] = '\0';
    if (strcmp(s1, output_name) == 0)
	return TRUE;

    return FALSE;
}

static void
drmmode_output_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int num)
{
	NVPtr pNv = NVPTR(pScrn);
	xf86OutputPtr output;
	drmModeConnectorPtr koutput;
	drmModeEncoderPtr kencoder;
	drmmode_output_private_ptr drmmode_output;
	const char *s;
	char name[32];

	koutput = drmModeGetConnector(drmmode->fd,
				      drmmode->mode_res->connectors[num]);
	if (!koutput)
		return;

	kencoder = drmModeGetEncoder(drmmode->fd, koutput->encoders[0]);
	if (!kencoder) {
		drmModeFreeConnector(koutput);
		return;
	}

	if (koutput->connector_type >= NUM_OUTPUT_NAMES)
		snprintf(name, 32, "Unknown%d-%d", koutput->connector_type,
			 koutput->connector_type_id);
	else
		snprintf(name, 32, "%s-%d",
			 output_names[koutput->connector_type],
			 koutput->connector_type_id);

	if (xf86IsEntityShared(pScrn->entityList[0])) {
		s = xf86GetOptValString(pNv->Options, OPTION_ZAPHOD_HEADS);
		if (s) {
			if (!drmmode_zaphod_match(pScrn, s, name)) {
				drmModeFreeEncoder(kencoder);
				drmModeFreeConnector(koutput);
				return;
			}
		} else {
			if (pNv->Primary && (num != 0)) {
				drmModeFreeEncoder(kencoder);
				drmModeFreeConnector(koutput);
				return;
			} else
			if (pNv->Secondary && (num != 1)) {
				drmModeFreeEncoder(kencoder);
				drmModeFreeConnector(koutput);
				return;
			}
		}
	}

	output = xf86OutputCreate (pScrn, &drmmode_output_funcs, name);
	if (!output) {
		drmModeFreeEncoder(kencoder);
		drmModeFreeConnector(koutput);
		return;
	}

	drmmode_output = calloc(sizeof(drmmode_output_private_rec), 1);
	if (!drmmode_output) {
		xf86OutputDestroy(output);
		drmModeFreeConnector(koutput);
		drmModeFreeEncoder(kencoder);
		return;
	}

	drmmode_output->output_id = drmmode->mode_res->connectors[num];
	drmmode_output->mode_output = koutput;
	drmmode_output->mode_encoder = kencoder;
	drmmode_output->drmmode = drmmode;
	output->mm_width = koutput->mmWidth;
	output->mm_height = koutput->mmHeight;

	output->subpixel_order = subpixel_conv_table[koutput->subpixel];
	output->driver_private = drmmode_output;

	output->possible_crtcs = kencoder->possible_crtcs;
	output->possible_clones = kencoder->possible_clones;

	output->interlaceAllowed = true;
	output->doubleScanAllowed = true;
}

static Bool
drmmode_xf86crtc_resize(ScrnInfoPtr scrn, int width, int height)
{
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
	ScreenPtr screen = screenInfo.screens[scrn->scrnIndex];
	NVPtr pNv = NVPTR(scrn);
	drmmode_crtc_private_ptr
		    drmmode_crtc = xf86_config->crtc[0]->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	uint32_t old_width, old_height, old_pitch, old_fb_id;
	struct nouveau_bo *old_bo = NULL;
	int ret, i, pitch;
	PixmapPtr ppix;

	ErrorF("resize called %d %d\n", width, height);

	if (scrn->virtualX == width && scrn->virtualY == height)
		return TRUE;

	old_width = scrn->virtualX;
	old_height = scrn->virtualY;
	old_pitch = scrn->displayWidth;
	old_fb_id = drmmode->fb_id;
	nouveau_bo_ref(pNv->scanout, &old_bo);
	nouveau_bo_ref(NULL, &pNv->scanout);

	ret = nouveau_allocate_surface(scrn, width, height,
				       scrn->bitsPerPixel,
				       NOUVEAU_CREATE_PIXMAP_SCANOUT,
				       &pitch, &pNv->scanout);
	if (!ret)
		goto fail;

	scrn->virtualX = width;
	scrn->virtualY = height;
	scrn->displayWidth = pitch / (scrn->bitsPerPixel >> 3);

	nouveau_bo_map(pNv->scanout, NOUVEAU_BO_RDWR);

	ret = drmModeAddFB(drmmode->fd, width, height, scrn->depth,
			   scrn->bitsPerPixel, pitch, pNv->scanout->handle,
			   &drmmode->fb_id);
	if (ret) {
		nouveau_bo_unmap(pNv->scanout);
		goto fail;
	}

	if (pNv->ShadowPtr) {
		free(pNv->ShadowPtr);
		pNv->ShadowPitch = pitch;
		pNv->ShadowPtr = malloc(pNv->ShadowPitch * height);
	}

	ppix = screen->GetScreenPixmap(screen);
	if (!pNv->NoAccel)
		nouveau_bo_ref(pNv->scanout, &nouveau_pixmap(ppix)->bo);
	screen->ModifyPixmapHeader(ppix, width, height, -1, -1, pitch,
				   (!pNv->NoAccel || pNv->ShadowPtr) ?
				   pNv->ShadowPtr : pNv->scanout->map);
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 9
	scrn->pixmapPrivate.ptr = ppix->devPrivate.ptr;
#endif
	nouveau_bo_unmap(pNv->scanout);

	for (i = 0; i < xf86_config->num_crtc; i++) {
		xf86CrtcPtr crtc = xf86_config->crtc[i];

		if (!crtc->enabled)
			continue;

		drmmode_set_mode_major(crtc, &crtc->mode,
				       crtc->rotation, crtc->x, crtc->y);
	}

	if (old_fb_id)
		drmModeRmFB(drmmode->fd, old_fb_id);
	nouveau_bo_ref(NULL, &old_bo);

	return TRUE;

 fail:
	nouveau_bo_ref(old_bo, &pNv->scanout);
	scrn->virtualX = old_width;
	scrn->virtualY = old_height;
	scrn->displayWidth = old_pitch;
	drmmode->fb_id = old_fb_id;

	return FALSE;
}

static const xf86CrtcConfigFuncsRec drmmode_xf86crtc_config_funcs = {
	drmmode_xf86crtc_resize
};

Bool drmmode_pre_init(ScrnInfoPtr pScrn, int fd, int cpp)
{
	xf86CrtcConfigPtr   xf86_config;
	drmmode_ptr drmmode;
	int i;

	drmmode = xnfalloc(sizeof *drmmode);
	drmmode->fd = fd;
	drmmode->fb_id = 0;

	xf86CrtcConfigInit(pScrn, &drmmode_xf86crtc_config_funcs);
	xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

	drmmode->cpp = cpp;
	drmmode->mode_res = drmModeGetResources(drmmode->fd);
	if (!drmmode->mode_res)
		return FALSE;

	xf86CrtcSetSizeRange(pScrn, 320, 200, drmmode->mode_res->max_width,
			     drmmode->mode_res->max_height);
	for (i = 0; i < drmmode->mode_res->count_crtcs; i++) {
		if (!xf86IsEntityShared(pScrn->entityList[0] ||
		     pScrn->confScreen->device->screen == i))
			drmmode_crtc_init(pScrn, drmmode, i);
	}

	for (i = 0; i < drmmode->mode_res->count_connectors; i++)
		drmmode_output_init(pScrn, drmmode, i);

	xf86InitialConfiguration(pScrn, TRUE);

	return TRUE;
}

void
drmmode_adjust_frame(ScrnInfoPtr scrn, int x, int y, int flags)
{
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
	xf86OutputPtr output = config->output[config->compat_output];
	xf86CrtcPtr crtc = output->crtc;

	if (!crtc || !crtc->enabled)
		return;

	drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation, x, y);
}

void
drmmode_remove_fb(ScrnInfoPtr pScrn)
{
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
	xf86CrtcPtr crtc = NULL;
	drmmode_crtc_private_ptr drmmode_crtc;
	drmmode_ptr drmmode;

	if (config)
		crtc = config->crtc[0];
	if (!crtc)
		return;

	drmmode_crtc = crtc->driver_private;
	drmmode = drmmode_crtc->drmmode;

	if (drmmode->fb_id)
		drmModeRmFB(drmmode->fd, drmmode->fb_id);
	drmmode->fb_id = 0;
}

int
drmmode_cursor_init(ScreenPtr pScreen)
{
	NVPtr pNv = NVPTR(xf86Screens[pScreen->myNum]);
	int size = nv_cursor_width(pNv);
	int flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
		    HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_32 |
		    (pNv->dev->chipset >= 0x11 ? HARDWARE_CURSOR_ARGB : 0) |
		    HARDWARE_CURSOR_UPDATE_UNHIDDEN;

	return xf86_cursors_init(pScreen, size, size, flags);
}

Bool
drmmode_page_flip(DrawablePtr draw, PixmapPtr back, void *priv)
{
	ScrnInfoPtr scrn = xf86Screens[draw->pScreen->myNum];
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
	drmmode_crtc_private_ptr crtc = config->crtc[0]->driver_private;
	drmmode_ptr mode = crtc->drmmode;
	int ret, i, old_fb_id;

	old_fb_id = mode->fb_id;
	ret = drmModeAddFB(mode->fd, scrn->virtualX, scrn->virtualY,
			   scrn->depth, scrn->bitsPerPixel,
			   scrn->displayWidth * scrn->bitsPerPixel / 8,
			   nouveau_pixmap_bo(back)->handle, &mode->fb_id);
	if (ret) {
		xf86DrvMsg(scrn->scrnIndex, X_WARNING,
			   "add fb failed: %s\n", strerror(errno));
		return FALSE;
	}

	for (i = 0; i < config->num_crtc; i++) {
		crtc = config->crtc[i]->driver_private;

		if (!config->crtc[i]->enabled)
			continue;

		ret = drmModePageFlip(mode->fd, crtc->mode_crtc->crtc_id,
				      mode->fb_id, 0, priv);
		if (ret) {
			xf86DrvMsg(scrn->scrnIndex, X_WARNING,
				   "flip queue failed: %s\n", strerror(errno));
			return FALSE;
		}
	}

	drmModeRmFB(mode->fd, old_fb_id);

	return TRUE;
}

#ifdef HAVE_LIBUDEV
static void
drmmode_handle_uevents(ScrnInfoPtr scrn)
{
	drmmode_ptr drmmode = drmmode_from_scrn(scrn);
	struct udev_device *dev;

	dev = udev_monitor_receive_device(drmmode->uevent_monitor);
	if (!dev)
		return;

	RRGetInfo(screenInfo.screens[scrn->scrnIndex], TRUE);
	udev_device_unref(dev);
}
#endif

static void
drmmode_uevent_init(ScrnInfoPtr scrn)
{
#ifdef HAVE_LIBUDEV
	drmmode_ptr drmmode = drmmode_from_scrn(scrn);
	struct udev *u;
	struct udev_monitor *mon;

	u = udev_new();
	if (!u)
		return;
	mon = udev_monitor_new_from_netlink(u, "udev");
	if (!mon) {
		udev_unref(u);
		return;
	}

	if (udev_monitor_filter_add_match_subsystem_devtype(mon,
							    "drm",
							    "drm_minor") < 0 ||
	    udev_monitor_enable_receiving(mon) < 0) {
		udev_monitor_unref(mon);
		udev_unref(u);
		return;
	}

	AddGeneralSocket(udev_monitor_get_fd(mon));
	drmmode->uevent_monitor = mon;
#endif
}

static void
drmmode_uevent_fini(ScrnInfoPtr scrn)
{
#ifdef HAVE_LIBUDEV
	drmmode_ptr drmmode = drmmode_from_scrn(scrn);

	if (drmmode->uevent_monitor) {
		struct udev *u = udev_monitor_get_udev(drmmode->uevent_monitor);

		udev_monitor_unref(drmmode->uevent_monitor);
		udev_unref(u);
	}
#endif
}

static void
drmmode_wakeup_handler(pointer data, int err, pointer p)
{
	ScrnInfoPtr scrn = data;
	drmmode_ptr drmmode = drmmode_from_scrn(scrn);
	fd_set *read_mask = p;

	if (scrn == NULL || err < 0)
		return;

	if (FD_ISSET(drmmode->fd, read_mask))
		drmHandleEvent(drmmode->fd, &drmmode->event_context);

#ifdef HAVE_LIBUDEV
	if (FD_ISSET(udev_monitor_get_fd(drmmode->uevent_monitor), read_mask))
		drmmode_handle_uevents(scrn);
#endif
}

void
drmmode_screen_init(ScreenPtr pScreen)
{
	ScrnInfoPtr scrn = xf86Screens[pScreen->myNum];
	drmmode_ptr drmmode = drmmode_from_scrn(scrn);

	drmmode_uevent_init(scrn);

	/* Plug in a vblank event handler */
	drmmode->event_context.version = DRM_EVENT_CONTEXT_VERSION;
	drmmode->event_context.vblank_handler = nouveau_dri2_vblank_handler;
	AddGeneralSocket(drmmode->fd);

	/* Register a wakeup handler to get informed on DRM events */
	RegisterBlockAndWakeupHandlers((BlockHandlerProcPtr)NoopDDA,
				       drmmode_wakeup_handler, scrn);
}

void
drmmode_screen_fini(ScreenPtr pScreen)
{
	ScrnInfoPtr scrn = xf86Screens[pScreen->myNum];

	drmmode_uevent_fini(scrn);
}
