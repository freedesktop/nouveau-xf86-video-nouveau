/*
 * Copyright 2007-2008 Maarten Maathuis
 * Copyright 2008 Stephane Marchesin
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#include "exa.h"
#include "damage.h"
#include "dixstruct.h"
#include "fourcc.h"

#include "nv_include.h"
#include "nv_dma.h"

#include "nv30_shaders.h"

extern Atom xvSyncToVBlank, xvSetDefaults;

/*
 * The filtering function used for video scaling. We use a cubic filter as defined in 
 * "Reconstruction Filters in Computer Graphics"
 * Mitchell & Netravali in SIGGRAPH '88 
 */
static float filter_func(float x)
{
	const double B=0.75;
	const double C=(1.0-B)/2.0;
	double x1=fabs(x);
	double x2=fabs(x)*x1;
	double x3=fabs(x)*x2;

	if (fabs(x)<1.0) 
		return ( (12.0-9.0*B-6.0*C)*x3+(-18.0+12.0*B+6.0*C)*x2+(6.0-2.0*B) )/6.0; 
	else 
		return ( (-B-6.0*C)*x3+(6.0*B+30.0*C)*x2+(-12.0*B-48.0*C)*x1+(8.0*B+24.0*C) )/6.0;
}

static int8_t f32tosb8(float v)
{
	return (int8_t)(v*127.0);
}

/*
 * 512 means 2048 bytes of VRAM
 */
#define TABLE_SIZE 512
static void compute_filter_table(int8_t *t) {
	int i;
	float x;
	for(i=0;i<TABLE_SIZE;i++) {
		x=(i+0.5)/TABLE_SIZE;

		float w0=filter_func(x+1.0);
		float w1=filter_func(x);
		float w2=filter_func(x-1.0);
		float w3=filter_func(x-2.0);

		t[4*i+2]=f32tosb8(1.0+x-w1/(w0+w1));
		t[4*i+1]=f32tosb8(1.0-x+w3/(w2+w3));
		t[4*i+0]=f32tosb8(w0+w1);
		t[4*i+3]=f32tosb8(0.0);
	}
}

static void
NV30_LoadFilterTable(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	if (!pNv->xv_filtertable_mem) {
		if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_GART |
				   NOUVEAU_BO_MAP, 0,
				   TABLE_SIZE * sizeof(float) * 4,
				   &pNv->xv_filtertable_mem)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"Couldn't alloc filter table!\n");
			return;
		}

		if (nouveau_bo_map(pNv->xv_filtertable_mem, NOUVEAU_BO_RDWR)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "Couldn't map filter table!\n");
			return;
		}

		int8_t *t=pNv->xv_filtertable_mem->map;
		compute_filter_table(t);
		nouveau_bo_unmap(pNv->xv_filtertable_mem);
	}
}

#define SWIZZLE(ts0x,ts0y,ts0z,ts0w,ts1x,ts1y,ts1z,ts1w)			\
	(									\
	NV34TCL_TX_SWIZZLE_S0_X_##ts0x | NV34TCL_TX_SWIZZLE_S0_Y_##ts0y	|	\
	NV34TCL_TX_SWIZZLE_S0_Z_##ts0z | NV34TCL_TX_SWIZZLE_S0_W_##ts0w	|	\
	NV34TCL_TX_SWIZZLE_S1_X_##ts1x | NV34TCL_TX_SWIZZLE_S1_Y_##ts1y |	\
	NV34TCL_TX_SWIZZLE_S1_Z_##ts1z | NV34TCL_TX_SWIZZLE_S1_W_##ts1w		\
	)

/*
 * Texture 0 : filter table
 * Texture 1 : Y data
 * Texture 2 : UV data
 */
static Bool
NV30VideoTexture(ScrnInfoPtr pScrn, struct nouveau_bo *src, int offset,
		 uint16_t width, uint16_t height, uint16_t src_pitch, int unit)
{
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *rankine = pNv->Nv3D;
	uint32_t tex_reloc = NOUVEAU_BO_VRAM | NOUVEAU_BO_GART | NOUVEAU_BO_RD;
	uint32_t card_fmt = 0;
	uint32_t card_swz = 0;

	switch(unit) {
		case 0:
		card_fmt = NV34TCL_TX_FORMAT_FORMAT_A8R8G8B8;
		card_swz = SWIZZLE(S1, S1, S1, S1, X, Y, Z, W);
		break;
		case 1:
		card_fmt = NV34TCL_TX_FORMAT_FORMAT_A8_RECT;
		card_swz = SWIZZLE(S1, S1, S1, S1, X, X, X, X);
		break;
		case 2:
		card_fmt = NV34TCL_TX_FORMAT_FORMAT_A8L8_RECT;
#if X_BYTE_ORDER == X_BIG_ENDIAN
		card_swz = SWIZZLE(S1, S1, S1, S1, Z, W, X, Y); /* x = V, y = U */
#else
		card_swz = SWIZZLE(S1, S1, S1, S1, W, Z, Y, X); /* x = V, y = U */
#endif
		break;
	}

	BEGIN_RING(chan, rankine, NV34TCL_TX_OFFSET(unit), 8);
	if (OUT_RELOCl(chan, src, offset, tex_reloc))
		return FALSE;

	if (unit == 0) {
		if (OUT_RELOCd(chan, src, NV34TCL_TX_FORMAT_DIMS_1D |
			       card_fmt | (1 << 16) |
			       (log2i(width) <<
				NV34TCL_TX_FORMAT_BASE_SIZE_U_SHIFT) |
			       (log2i(height) <<
				NV34TCL_TX_FORMAT_BASE_SIZE_V_SHIFT) |
			       8 /* no idea */, tex_reloc | NOUVEAU_BO_OR,
			       NV34TCL_TX_FORMAT_DMA0, NV34TCL_TX_FORMAT_DMA1))
			return FALSE;
		OUT_RING  (chan, NV34TCL_TX_WRAP_S_REPEAT |
				 NV34TCL_TX_WRAP_T_CLAMP_TO_EDGE |
				 NV34TCL_TX_WRAP_R_CLAMP_TO_EDGE);
	} else {
		if (OUT_RELOCd(chan, src, NV34TCL_TX_FORMAT_DIMS_2D |
			       card_fmt | (1 << 16) |
			       (log2i(width) <<
				NV34TCL_TX_FORMAT_BASE_SIZE_U_SHIFT) |
			       (log2i(height) <<
				NV34TCL_TX_FORMAT_BASE_SIZE_V_SHIFT) |
			       8 /* no idea */, tex_reloc | NOUVEAU_BO_OR,
			       NV34TCL_TX_FORMAT_DMA0, NV34TCL_TX_FORMAT_DMA1))
			return FALSE;
		OUT_RING  (chan, NV34TCL_TX_WRAP_S_CLAMP_TO_EDGE |
				 NV34TCL_TX_WRAP_T_CLAMP_TO_EDGE |
				 NV34TCL_TX_WRAP_R_CLAMP_TO_EDGE);
	}

	OUT_RING  (chan, NV34TCL_TX_ENABLE_ENABLE);
	OUT_RING  (chan, (src_pitch << NV34TCL_TX_SWIZZLE_RECT_PITCH_SHIFT) |
			 card_swz);
	if (unit==0)
		OUT_RING  (chan, NV34TCL_TX_FILTER_SIGNED_ALPHA |
				 NV34TCL_TX_FILTER_SIGNED_RED |
				 NV34TCL_TX_FILTER_SIGNED_GREEN |
				 NV34TCL_TX_FILTER_SIGNED_BLUE |
				 NV34TCL_TX_FILTER_MINIFY_LINEAR |
				 NV34TCL_TX_FILTER_MAGNIFY_LINEAR | 0x2000);
	else
		OUT_RING  (chan, NV34TCL_TX_FILTER_MINIFY_LINEAR |
				 NV34TCL_TX_FILTER_MAGNIFY_LINEAR | 0x2000);
	OUT_RING  (chan, (width << NV34TCL_TX_NPOT_SIZE_W_SHIFT) | height);
	OUT_RING  (chan, 0); /* border ARGB */

	return TRUE;
}

Bool
NV30GetSurfaceFormat(PixmapPtr ppix, int *fmt_ret)
{
	switch (ppix->drawable.bitsPerPixel) {
		case 32:
			*fmt_ret = NV34TCL_RT_FORMAT_COLOR_A8R8G8B8;
			break;
		case 24:
			*fmt_ret = NV34TCL_RT_FORMAT_COLOR_X8R8G8B8;
			break;
		case 16:
			*fmt_ret = NV34TCL_RT_FORMAT_COLOR_R5G6B5;
			break;
		case 8:
			*fmt_ret = NV34TCL_RT_FORMAT_COLOR_B8;
			break;
		default:
			return FALSE;
	}

	return TRUE;
}

void
NV30StopTexturedVideo(ScrnInfoPtr pScrn, pointer data, Bool Exit)
{
}

#define VERTEX_OUT(sx,sy,dx,dy) do {                                           \
	BEGIN_RING(chan, rankine, NV34TCL_VTX_ATTR_2F_X(8), 4);                \
	OUT_RINGf (chan, (sx)); OUT_RINGf (chan, (sy));                        \
	OUT_RINGf (chan, (sx)/2.0); OUT_RINGf (chan, (sy)/2.0);                \
	BEGIN_RING(chan, rankine, NV34TCL_VTX_ATTR_2I(0), 1);                  \
 	OUT_RING  (chan, ((dy)<<16)|(dx));                                     \
} while(0)

int
NV30PutTextureImage(ScrnInfoPtr pScrn, struct nouveau_bo *src, int src_offset,
		    int src_offset2, int id, int src_pitch, BoxPtr dstBox,
		    int x1, int y1, int x2, int y2,
		    uint16_t width, uint16_t height,
		    uint16_t src_w, uint16_t src_h,
		    uint16_t drw_w, uint16_t drw_h,
		    RegionPtr clipBoxes,
		    PixmapPtr ppix,
		    NVPortPrivPtr pPriv)
{
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *rankine = pNv->Nv3D;
	struct nouveau_bo *bo = nouveau_pixmap_bo(ppix);
	Bool bicubic = pPriv->bicubic;
	float X1, X2, Y1, Y2;
	BoxPtr pbox;
	int nbox;
	int dst_format = 0;

	if (drw_w > 4096 || drw_h > 4096) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"XV: Draw size too large.\n");
		return BadAlloc;
	}

	if (!NV30GetSurfaceFormat(ppix, &dst_format)) {
		ErrorF("No surface format, bad.\n");
		return BadImplementation;
	}

	pbox = REGION_RECTS(clipBoxes);
	nbox = REGION_NUM_RECTS(clipBoxes);

	if (MARK_RING(chan, 128, 1 + 1 + 3))
		return BadImplementation;

	/* Disable blending */
	BEGIN_RING(chan, rankine, NV34TCL_BLEND_FUNC_ENABLE, 1);
	OUT_RING  (chan, 0);

	/* Setup surface */
	BEGIN_RING(chan, rankine, NV34TCL_RT_FORMAT, 3);
	OUT_RING  (chan, NV34TCL_RT_FORMAT_TYPE_LINEAR |
			 NV34TCL_RT_FORMAT_ZETA_Z24S8 |
			 dst_format);
	OUT_RING  (chan, (exaGetPixmapPitch(ppix) << 16) |
			  exaGetPixmapPitch(ppix));
	if (OUT_RELOCl(chan, bo, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR)) {
		MARK_UNDO(chan);
		return BadImplementation;
	}

	if (pNv->dev->chipset == 0x30) {
		int x = 0;
		int y = 0;
		int w = ppix->drawable.x + ppix->drawable.width;
		int h = ppix->drawable.y + ppix->drawable.height;

		BEGIN_RING(chan, rankine, NV34TCL_VIEWPORT_HORIZ, 2);
		OUT_RING  (chan, (w<<16)|x);
		OUT_RING  (chan, (h<<16)|y);
		BEGIN_RING(chan, rankine, NV34TCL_VIEWPORT_CLIP_HORIZ(0), 2);
		OUT_RING  (chan, (w-1+x)<<16);
		OUT_RING  (chan, (h-1+y)<<16);
		BEGIN_RING(chan, rankine, NV34TCL_VIEWPORT_TX_ORIGIN, 1);
		OUT_RING  (chan, (y<<16)|x);
	}

	NV30_LoadFilterTable(pScrn);

	BEGIN_RING(chan, rankine, NV34TCL_TX_UNITS_ENABLE, 1);
	OUT_RING  (chan, NV34TCL_TX_UNITS_ENABLE_TX0 |
			 NV34TCL_TX_UNITS_ENABLE_TX1);

	if (!NV30VideoTexture(pScrn, pNv->xv_filtertable_mem, 0, TABLE_SIZE,
			      1, 0 , 0) ||
	    !NV30VideoTexture(pScrn, src, src_offset, src_w, src_h,
		    	      src_pitch, 1)) {
		MARK_UNDO(chan);
		return BadImplementation;
	}

	/* We've got NV12 format, which means half width and half height texture of chroma channels. */
	if (!NV30VideoTexture(pScrn, src, src_offset2, src_w/2, src_h/2,
			      src_pitch, 2)) {
		MARK_UNDO(chan);
		return BadImplementation;
	}

	BEGIN_RING(chan, rankine, NV34TCL_TX_ENABLE(3), 1);
	OUT_RING  (chan, 0x0);

	if (drw_w / 2 < src_w || drw_h / 2 < src_h)
		bicubic = FALSE;

	if (!NV30_LoadFragProg(pScrn, bicubic ?
			       &nv30_fp_yv12_bicubic :
			       &nv30_fp_yv12_bilinear)) {
		MARK_UNDO(chan);
		return BadImplementation;
	}

	/* Just before rendering we wait for vblank in the non-composited case. */
	if (pPriv->SyncToVBlank) {
		FIRE_RING(chan);
		NV11SyncToVBlank(ppix, dstBox);
	}

	/* These are fixed point values in the 16.16 format. */
	X1 = (float)(x1>>16)+(float)(x1&0xFFFF)/(float)0x10000;
	Y1 = (float)(y1>>16)+(float)(y1&0xFFFF)/(float)0x10000;
	X2 = (float)(x2>>16)+(float)(x2&0xFFFF)/(float)0x10000;
	Y2 = (float)(y2>>16)+(float)(y2&0xFFFF)/(float)0x10000;

	BEGIN_RING(chan, rankine, NV34TCL_VERTEX_BEGIN_END, 1);
	OUT_RING  (chan, NV34TCL_VERTEX_BEGIN_END_TRIANGLES);

	while(nbox--) {
		float tx1=X1+(float)(pbox->x1 - dstBox->x1)*(X2-X1)/(float)(drw_w);
		float tx2=X1+(float)(pbox->x2 - dstBox->x1)*(src_w)/(float)(drw_w);
		float ty1=Y1+(float)(pbox->y1 - dstBox->y1)*(Y2-Y1)/(float)(drw_h);
		float ty2=Y1+(float)(pbox->y2 - dstBox->y1)*(src_h)/(float)(drw_h);
		int sx1=pbox->x1;
		int sx2=pbox->x2;
		int sy1=pbox->y1;
		int sy2=pbox->y2;

		BEGIN_RING(chan, rankine, NV34TCL_SCISSOR_HORIZ, 2);
		OUT_RING  (chan, (sx2 << 16) | 0);
		OUT_RING  (chan, (sy2 << 16) | 0);

		VERTEX_OUT(tx1, ty1, sx1, sy1);
		VERTEX_OUT(tx2+(tx2-tx1), ty1, sx2+(sx2-sx1), sy1);
		VERTEX_OUT(tx1, ty2+(ty2-ty1), sx1, sy2+(sy2-sy1));

		pbox++;
	}

	BEGIN_RING(chan, rankine, NV34TCL_VERTEX_BEGIN_END, 1);
	OUT_RING  (chan, NV34TCL_VERTEX_BEGIN_END_STOP);

	if (pNv->dev->chipset == 0x30) {
		BEGIN_RING(chan, rankine, NV34TCL_VIEWPORT_HORIZ, 2);
		OUT_RING  (chan, 4096 << 16);
		OUT_RING  (chan, 4096 << 16);
		BEGIN_RING(chan, rankine, NV34TCL_VIEWPORT_CLIP_HORIZ(0), 2);
		OUT_RING  (chan, 4095 << 16);
		OUT_RING  (chan, 4095 << 16);
		BEGIN_RING(chan, rankine, NV34TCL_VIEWPORT_TX_ORIGIN, 1);
		OUT_RING  (chan, 0);
	}

	FIRE_RING (chan);

	return Success;
}

/**
 * NV30SetTexturePortAttribute
 * sets the attribute "attribute" of port "data" to value "value"
 * supported attributes:
 * Sync to vblank.
 * 
 * @param pScrenInfo
 * @param attribute attribute to set
 * @param value value to which attribute is to be set
 * @param data port from which the attribute is to be set
 * 
 * @return Success, if setting is successful
 * BadValue/BadMatch, if value/attribute are invalid
 */
int
NV30SetTexturePortAttribute(ScrnInfoPtr pScrn, Atom attribute,
                       INT32 value, pointer data)
{
        NVPortPrivPtr pPriv = (NVPortPrivPtr)data;

        if (attribute == xvSyncToVBlank) {
                if ((value < 0) || (value > 1))
                        return BadValue;
                pPriv->SyncToVBlank = value;
        } else
        if (attribute == xvSetDefaults) {
                pPriv->SyncToVBlank = TRUE;
        } else
                return BadMatch;

        return Success;
}

/**
 * NV30GetTexturePortAttribute
 * reads the value of attribute "attribute" from port "data" into INT32 "*value"
 * Sync to vblank.
 * 
 * @param pScrn unused
 * @param attribute attribute to be read
 * @param value value of attribute will be stored here
 * @param data port from which attribute will be read
 * @return Success, if queried attribute exists
 */
int
NV30GetTexturePortAttribute(ScrnInfoPtr pScrn, Atom attribute,
                       INT32 *value, pointer data)
{
        NVPortPrivPtr pPriv = (NVPortPrivPtr)data;

        if(attribute == xvSyncToVBlank)
                *value = (pPriv->SyncToVBlank) ? 1 : 0;
        else
                return BadMatch;

        return Success;
}

