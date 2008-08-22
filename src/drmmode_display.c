/*
 * Copyright © 2007 Red Hat, Inc.
 * Copyright © 2008 Maarten Maathuis
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

#ifdef XF86DRM_MODE

#include "drmmode_display.h"
#include "sarea.h"
#include "X11/Xatom.h"

#if 0
/* not using for the moment */
uint32_t drmmode_create_new_fb(ScrnInfoPtr pScrn, int width, int height, int *pitch)
{
	NVPtr pNv = NVPTR(pScrn);
	int ret = 0;

	/* temporary hack, allow old framebuffers to continue to exist and idle. */
	if (pNv->FB_old) {
		nouveau_bo_del(pNv->FB_old);
		pNv->FB_old = NULL;
	}

	pNv->FB_old = pNv->FB;
	pNv->FB = NULL;

	*pitch = NOUVEAU_ALIGN(*pitch, 64) * (pScrn->bitsPerPixel >> 3);

	ret = nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN,
			0, *pitch * NOUVEAU_ALIGN(height, 64), &pNv->FB)

	if (ret)
		return 0;

	ret = nouveau_bo_map(pNv->FB, NOUVEAU_BO_RDWR);

	if (ret)
		return 0;

	return pNv->FB.handle;
}
#endif

#if 0
static Bool drmmode_resize_fb(ScrnInfoPtr scrn, drmmode_ptr drmmode, int width, int height);
#endif

static Bool
drmmode_xf86crtc_resize (ScrnInfoPtr scrn, int width, int height)
{
	//xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
	//drmmode_crtc_private_ptr drmmode_crtc = xf86_config->crtc[0]->driver_private;
	//drmmode_ptr drmmode = drmmode_crtc->drmmode;
	//Bool ret;

	ErrorF("resize called %d %d\n", width, height);
	//ret = drmmode_resize_fb(scrn, drmmode, width, height);
	scrn->virtualX = width;
	scrn->virtualY = height;

	return TRUE; //ret;
}

static void
drmmode_ConvertFromKMode(ScrnInfoPtr	scrn,
			struct drm_mode_modeinfo *kmode,
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
drmmode_ConvertToKMode(ScrnInfoPtr	scrn,
		struct drm_mode_modeinfo *kmode,
		DisplayModePtr	mode)
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

static const xf86CrtcConfigFuncsRec drmmode_xf86crtc_config_funcs = {
	drmmode_xf86crtc_resize
};

static void
drmmode_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
	return;
}

static Bool
drmmode_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
			Rotation rotation, int x, int y)
{
	xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	int saved_x, saved_y;
	Rotation saved_rotation;
	DisplayModeRec saved_mode;
	uint32_t *output_ids;
	int output_count = 0;
	int ret = TRUE;
	int i;
	int fb_id;
	struct drm_mode_modeinfo kmode;

	ErrorF("drmmode_set_mode_major called\n");

	saved_mode = crtc->mode;
	saved_x = crtc->x;
	saved_y = crtc->y;
	saved_rotation = crtc->rotation;

	crtc->mode = *mode;
	crtc->x = x;
	crtc->y = y;
	crtc->rotation = rotation;

	output_ids = xcalloc(sizeof(uint32_t), xf86_config->num_output);
	if (!output_ids) {
		ret = FALSE;
		goto done;
	}

	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		drmmode_output_private_ptr drmmode_output;

		if (output->crtc != crtc)
			continue;

		drmmode_output = output->driver_private;
		output_ids[output_count] = drmmode_output->mode_output->connector_id;
		output_count++;
	}

	if (!xf86CrtcRotate(crtc, mode, rotation)) {
		goto done;
	}

	drmmode_ConvertToKMode(crtc->scrn, &kmode, mode);

	if (drmmode_crtc->shadow_id && crtc->rotatedData)
		fb_id = drmmode_crtc->shadow_id;
	else
		fb_id = drmmode->fb_id;
	ErrorF("fb id is %d\n", fb_id);
	drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
			fb_id, x, y, output_ids, output_count, &kmode);

done:
	if (!ret) {
		crtc->x = saved_x;
		crtc->y = saved_y;
		crtc->rotation = saved_rotation;
		crtc->mode = saved_mode;
	}

	if (output_ids)
		xfree(output_ids);
	return ret;
}

static void
drmmode_set_cursor_colors (xf86CrtcPtr crtc, int bg, int fg)
{
  
}

static void
drmmode_set_cursor_position (xf86CrtcPtr crtc, int x, int y)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;

	drmModeMoveCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, x, y);
}

static void
drmmode_load_cursor_argb (xf86CrtcPtr crtc, CARD32 *image)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

	NVPtr pNv = NVPTR(crtc->scrn);
	uint32_t *dst = NULL;

	if (drmmode_crtc->index == 1)
		dst = (uint32_t *) pNv->Cursor2->map;
	else
		dst = (uint32_t *) pNv->Cursor->map;

	/* Assume cursor is 64x64 */
	memcpy(dst, (uint32_t *)image, 64 * 64 * 4);
}


static void
drmmode_hide_cursor (xf86CrtcPtr crtc)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;

	drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, 0, 64, 64);

}

static void
drmmode_show_cursor (xf86CrtcPtr crtc)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	NVPtr pNv = NVPTR(crtc->scrn);

	drm_handle_t handle = 0;

	if (drmmode_crtc->index == 1)
		handle = nouveau_bo_get_drm_map(pNv->Cursor2);
	else
		handle = nouveau_bo_get_drm_map(pNv->Cursor);

	drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, handle, 64, 64);
}

/* This stuff isn't ready for NOUVEAU_EXA_PIXMAPS, but can be easily ported. */
static void *
drmmode_shadow_allocate (xf86CrtcPtr crtc, int width, int height)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	int size, pitch, ret;

	ErrorF("drmmode_shadow_allocate\n");

	pitch = width * (pScrn->bitsPerPixel/8);
	size = pitch * height;

	if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN,
			64, size, &drmmode_crtc->shadow)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to allocate memory for shadow buffer!\n");
		return NULL;
	}

	if (drmmode_crtc->shadow && nouveau_bo_map(drmmode_crtc->shadow, NOUVEAU_BO_RDWR)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"Failed to map shadow buffer.\n");
		return NULL;
	}

	ret = drmModeAddFB(drmmode->fd, width, height, pScrn->depth, pScrn->bitsPerPixel, pitch, nouveau_bo_get_drm_map(drmmode_crtc->shadow), &drmmode_crtc->shadow_id);
	if (ret)
		return NULL;

	/* for easy acces by exa */
	pNv->shadow[drmmode_crtc->index] = drmmode_crtc->shadow;

	return drmmode_crtc->shadow->map;
}

static PixmapPtr
drmmode_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	uint32_t pitch;
	PixmapPtr rotate_pixmap;

	ErrorF("drmmode_shadow_create\n");

	if (!data)
		data = crtc->funcs->shadow_allocate (crtc, width, height);

	pitch = width * (pScrn->bitsPerPixel/8);

	rotate_pixmap = GetScratchPixmapHeader(pScrn->pScreen,
						width, height,
						pScrn->depth,
						pScrn->bitsPerPixel,
						pitch,
						data);

	if (rotate_pixmap == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Couldn't allocate shadow pixmap for rotated CRTC\n");
	}

	return rotate_pixmap;
}

static void
drmmode_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	ScreenPtr pScreen = pScrn->pScreen;

	ErrorF("drmmode_shadow_destroy\n");

	if (rotate_pixmap)
		pScreen->DestroyPixmap(rotate_pixmap);

	if (drmmode_crtc->shadow_id) {
		drmModeRmFB(drmmode->fd, drmmode_crtc->shadow_id);
		drmmode_crtc->shadow_id = 0;
	}

	if (drmmode_crtc->shadow)
		nouveau_bo_del(&drmmode_crtc->shadow);

	drmmode_crtc->shadow = NULL;

	/* for easy acces by exa */
	pNv->shadow[drmmode_crtc->index] = NULL;
}

static void
drmmode_gamma_set(xf86CrtcPtr crtc, CARD16 *red, CARD16 *green, CARD16 *blue, int size)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
	drmmode_ptr drmmode = drmmode_crtc->drmmode;
	int ret;

	ErrorF("drmmode_gamma_set\n");

	ret = drmModeCrtcSetGamma(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, size, (uint16_t *)red, (uint16_t *)green, (uint16_t *)blue);
	if (ret)
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "drmModeCrtcSetGamma failed\n");
}

static const xf86CrtcFuncsRec drmmode_crtc_funcs = {
	.dpms = drmmode_crtc_dpms,
	.set_mode_major = drmmode_set_mode_major,
	.set_cursor_colors = drmmode_set_cursor_colors,
	.set_cursor_position = drmmode_set_cursor_position,
	.show_cursor = drmmode_show_cursor,
	.hide_cursor = drmmode_hide_cursor,
	.load_cursor_argb = drmmode_load_cursor_argb,
	.shadow_create = drmmode_shadow_create,
	.shadow_allocate = drmmode_shadow_allocate,
	.shadow_destroy = drmmode_shadow_destroy,
	.gamma_set = drmmode_gamma_set,
	.destroy = NULL, /* XXX */
};


static void
drmmode_crtc_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int num)
{
	xf86CrtcPtr crtc;
	drmmode_crtc_private_ptr drmmode_crtc;

	crtc = xf86CrtcCreate(pScrn, &drmmode_crtc_funcs);
	if (crtc == NULL)
		return;

	drmmode_crtc = xnfcalloc(sizeof(drmmode_crtc_private_rec), 1);
	drmmode_crtc->mode_crtc = drmModeGetCrtc(drmmode->fd, drmmode->mode_res->crtcs[num]);
	drmmode_crtc->drmmode = drmmode;
	drmmode_crtc->index = num;
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

	ErrorF("drmmode_output_detect called\n");

	drmmode_output->mode_output = drmModeGetConnector(drmmode->fd, drmmode_output->output_id);

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
drmmode_output_mode_valid(xf86OutputPtr output, DisplayModePtr pModes)
{
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

	ErrorF("drmmode_output_get_modes called\n");

	/* look for an EDID property */
	for (i = 0; i < koutput->count_props; i++) {
		props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
		if (props && (props->flags & DRM_MODE_PROP_BLOB)) {
			if (!strcmp(props->name, "EDID")) {
				ErrorF("EDID property found\n");
				if (drmmode_output->edid_blob)
					drmModeFreePropertyBlob(drmmode_output->edid_blob);
				drmmode_output->edid_blob = drmModeGetPropertyBlob(drmmode->fd, koutput->prop_values[i]);

				if (!drmmode_output->edid_blob)
					ErrorF("No EDID blob\n");
			}
			drmModeFreeProperty(props);
		}
	}

	if (drmmode_output->edid_blob)
		xf86OutputSetEDID(output, xf86InterpretEDID(output->scrn->scrnIndex, drmmode_output->edid_blob->data));
	else
		xf86OutputSetEDID(output, xf86InterpretEDID(output->scrn->scrnIndex, NULL));

	/* modes should already be available */
	for (i = 0; i < koutput->count_modes; i++) {
		Mode = xnfalloc(sizeof(DisplayModeRec));

		drmmode_ConvertFromKMode(output->scrn, &koutput->modes[i], Mode);
		Modes = xf86ModesAdd(Modes, Mode);

	}
	return Modes;
}

static void
drmmode_output_destroy(xf86OutputPtr output)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;

	if (drmmode_output->edid_blob)
		drmModeFreePropertyBlob(drmmode_output->edid_blob);
	drmModeFreeConnector(drmmode_output->mode_output);
	xfree(drmmode_output);
	output->driver_private = NULL;
}

static void
drmmode_output_dpms(xf86OutputPtr output, int mode)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmModeConnectorPtr koutput = drmmode_output->mode_output;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	drmModePropertyPtr props;
	int i;

	ErrorF("drmmode_output_dpms called with mode %d\n", mode);

	for (i = 0; i < koutput->count_props; i++) {
		props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
		if (props && (props->flags & DRM_MODE_PROP_ENUM)) {
			if (!strcmp(props->name, "DPMS")) {
				ErrorF("DPMS property found\n");
				drmModeConnectorSetProperty(drmmode->fd, drmmode_output->output_id, props->prop_id, mode); 
			}
			drmModeFreeProperty(props);
		}
	}
}

/*
 * Output properties.
 */

/*
 * Several scaling modes exist, let the user choose.
 */
#define SCALING_MODE_NAME "SCALING_MODE"
static const struct {
	char *name;
	int mode;
} scaling_mode[] = {
	{ "non-gpu", DRM_MODE_SCALE_NON_GPU },
	{ "fullscreen", DRM_MODE_SCALE_FULLSCREEN },
	{ "aspect", DRM_MODE_SCALE_ASPECT },
	{ "noscale", DRM_MODE_SCALE_NO_SCALE },
	{ NULL, 0xFFFF /* invalid */ }
};
static Atom scaling_mode_atom;

#define DITHERING_MODE_NAME "DITHERING"
static const struct {
	char *name;
	int mode;
} dithering_mode[] = {
	{ "off", DRM_MODE_DITHERING_OFF },
	{ "on", DRM_MODE_DITHERING_ON },
	{ NULL, 0xFFFF /* invalid */ }
};
static Atom dithering_atom;

int
drmmode_scaling_mode_lookup(char *name, int size)
{
	int i;

	/* for when name is zero terminated */
	if (size < 0)
		size = strlen(name);

	for (i = 0; scaling_mode[i].name; i++)
		/* We're getting non-terminated strings */
		if (strlen(scaling_mode[i].name) >= size &&
				!strncasecmp(name, scaling_mode[i].name, size))
			break;

	return scaling_mode[i].mode;
}

int
drmmode_dithering_lookup(char *name, int size)
{
	int i;

	/* for when name is zero terminated */
	if (size < 0)
		size = strlen(name);

	for (i = 0; dithering_mode[i].name; i++)
		/* We're getting non-terminated strings */
		if (strlen(dithering_mode[i].name) >= size &&
				!strncasecmp(name, dithering_mode[i].name, size))
			break;

	return dithering_mode[i].mode;
}


void
drmmode_output_create_resources(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	int error;

	/*
	 * Setup scaling mode property.
	 */
	scaling_mode_atom = MakeAtom(SCALING_MODE_NAME, sizeof(SCALING_MODE_NAME) - 1, TRUE);

	error = RRConfigureOutputProperty(output->randr_output,
					scaling_mode_atom, FALSE, FALSE, FALSE,
					0, NULL);

	if (error != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"RRConfigureOutputProperty error, %d\n", error);
	}

	/* property has unknown initial value (for the moment) */

	/*
	 * Setup dithering property.
	 */
	dithering_atom = MakeAtom(DITHERING_MODE_NAME, sizeof(DITHERING_MODE_NAME) - 1, TRUE);

	error = RRConfigureOutputProperty(output->randr_output,
					dithering_atom, FALSE, FALSE, FALSE,
					0, NULL);

	if (error != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"RRConfigureOutputProperty error, %d\n", error);
	}

	/* property has unknown initial value (for the moment) */
}

Bool
drmmode_output_set_property(xf86OutputPtr output, Atom property,
				RRPropertyValuePtr value)
{
	drmmode_output_private_ptr drmmode_output = output->driver_private;
	drmModeConnectorPtr koutput = drmmode_output->mode_output;
	drmmode_ptr drmmode = drmmode_output->drmmode;
	drmModePropertyPtr props;
	int i, mode, rval = 0;

	if (property == scaling_mode_atom) {
		char *name = NULL;

		if (value->type != XA_STRING || value->format != 8)
			return FALSE;

		name = (char *) value->data;

		/* Match a string to a scaling mode */
		mode = drmmode_scaling_mode_lookup(name, value->size);
		if (mode == 0xFFFF)
			return FALSE;

		for (i = 0; i < koutput->count_props; i++) {
			props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
			if (props && (props->flags & DRM_MODE_PROP_ENUM)) {
				if (!strcmp(props->name, "scaling mode")) {
					ErrorF("scaling mode property found\n");
					rval = drmModeConnectorSetProperty(drmmode->fd, drmmode_output->output_id, props->prop_id, mode); 
				}
				drmModeFreeProperty(props);
			}
		}

		if (rval)
			return FALSE;

		return TRUE;
	} else if (property == dithering_atom) {
		char *name = NULL;

		if (value->type != XA_STRING || value->format != 8)
			return FALSE;

		name = (char *) value->data;

		/* Match a string to a scaling mode */
		mode = drmmode_dithering_lookup(name, value->size);
		if (mode == 0xFFFF)
			return FALSE;

		for (i = 0; i < koutput->count_props; i++) {
			props = drmModeGetProperty(drmmode->fd, koutput->props[i]);
			if (props && (props->flags & DRM_MODE_PROP_ENUM)) {
				if (!strcmp(props->name, "dithering")) {
					ErrorF("dithering property found\n");
					rval = drmModeConnectorSetProperty(drmmode->fd, drmmode_output->output_id, props->prop_id, mode); 
				}
				drmModeFreeProperty(props);
			}
		}

		if (rval)
			return FALSE;

		return TRUE;
	}

	return FALSE;
}

static const xf86OutputFuncsRec drmmode_output_funcs = {
	.dpms = drmmode_output_dpms,
	.detect = drmmode_output_detect,
	.mode_valid = drmmode_output_mode_valid,
	.get_modes = drmmode_output_get_modes,
	.destroy = drmmode_output_destroy,
	.create_resources = drmmode_output_create_resources,
	.set_property = drmmode_output_set_property,
};

static int subpixel_conv_table[7] = { 0, SubPixelUnknown,
				      SubPixelHorizontalRGB,
				      SubPixelHorizontalBGR,
				      SubPixelVerticalRGB,
				      SubPixelVerticalBGR,
				      SubPixelNone };

struct output_name {
	const char *name;
	int count;
};

struct output_name output_names[] = {
	{ "None", 0 },
	{ "VGA", 0 },
	{ "DVI-I", 0 },
	{ "DVI-D", 0 },
};

static void
drmmode_output_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int num)
{
	xf86OutputPtr output;
	drmModeConnectorPtr koutput;
	drmModeEncoderPtr kencoder;
	drmmode_output_private_ptr drmmode_output;
	char name[32];

	ErrorF("drmmode_output_init\n");

	koutput = drmModeGetConnector(drmmode->fd, drmmode->mode_res->connectors[num]);
	if (!koutput) {
		ErrorF("No connector\n");
		return;
	}

	kencoder = drmModeGetEncoder(drmmode->fd, koutput->encoders[0]);
	if (!kencoder) {
		ErrorF("No encoder\n");
		drmModeFreeConnector(koutput);
		return;
	}

	snprintf(name, 32, "%s-%d", output_names[koutput->connector_type].name, output_names[koutput->connector_type].count++);

	output = xf86OutputCreate (pScrn, &drmmode_output_funcs, name);
	if (!output) {
		drmModeFreeEncoder(kencoder);
		drmModeFreeConnector(koutput);
		return;
	}

	drmmode_output = xcalloc(sizeof(drmmode_output_private_rec), 1);
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

	return;
}

void drmmode_set_fb(ScrnInfoPtr scrn, drmmode_ptr drmmode, int width, int height, int pitch, struct nouveau_bo *bo)
{
	int ret;

	ErrorF("drmmode_set_fb is called\n");

	ret = drmModeAddFB(drmmode->fd, width, height, scrn->depth,
			   scrn->bitsPerPixel, pitch, nouveau_bo_get_drm_map(bo), &drmmode->fb_id);

	if (ret) {
		ErrorF("Failed to add fb\n");
	}

	drmmode->mode_fb = drmModeGetFB(drmmode->fd, drmmode->fb_id);
	if (!drmmode->mode_fb)
		return;

	ErrorF("Add fb id %d %d %d\n", drmmode->fb_id, width, height);
}

#if 0
static Bool drmmode_resize_fb(ScrnInfoPtr scrn, drmmode_ptr drmmode, int width, int height)
{
	uint32_t handle;
	int pitch;
	int ret;

	ErrorF("current width %d height %d\n", drmmode->mode_fb->width, drmmode->mode_fb->height);

	if (drmmode->mode_fb->width == width && drmmode->mode_fb->height == height)
		return TRUE;

	if (!drmmode->create_new_fb)
		return FALSE;

	handle = drmmode->create_new_fb(scrn, width, height, &pitch);
	if (handle == 0)
		return FALSE;

	ErrorF("pitch is %d\n", pitch);
	ret = drmModeReplaceFB(drmmode->fd, drmmode->fb_id, 
			       width, height,
			       scrn->depth, scrn->depth, pitch,
			       handle);

	if (ret)
		return FALSE;

	drmModeFreeFB(drmmode->mode_fb);
	drmmode->mode_fb = drmModeGetFB(drmmode->fd, drmmode->fb_id);

	if (!drmmode->mode_fb)
		return FALSE;

	return TRUE;
}
#endif

Bool drmmode_pre_init(ScrnInfoPtr pScrn, char *busId, drmmode_ptr drmmode, int cpp)
{
	xf86CrtcConfigPtr xf86_config = NULL;
	int i, ret;

	int drm_page_size;
	drm_page_size = getpagesize();

	/* Create a bus Id */
	/* Low level DRM open */
	ret = DRIOpenDRMMaster(pScrn, (drm_page_size > SAREA_MAX) ? drm_page_size : SAREA_MAX, busId, "nouveau");
	if (!ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"[dri] DRIGetVersion failed to open the DRM\n");
		return FALSE;
	}

	drmmode->fd = DRIMasterFD(pScrn);

	xf86CrtcConfigInit(pScrn, &drmmode_xf86crtc_config_funcs);
	xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

	drmmode->cpp = cpp;
	drmmode->mode_res = drmModeGetResources(drmmode->fd);
	if (!drmmode->mode_res)
		return FALSE;

	xf86CrtcSetSizeRange(pScrn, 320, 200, drmmode->mode_res->max_width, drmmode->mode_res->max_height);
	for (i = 0; i < drmmode->mode_res->count_crtcs; i++)
		drmmode_crtc_init(pScrn, drmmode, i);

	for (i = 0; i < drmmode->mode_res->count_connectors; i++)
		drmmode_output_init(pScrn, drmmode, i);

	if (!xf86InitialConfiguration(pScrn, FALSE))
		return FALSE;

	return TRUE;
}

#endif /* XF86DRM_MODE */
