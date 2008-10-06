/*
 * Copyright 2008 Ben Skeggs
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
#include "nv50_accel.h"
#include "nv50_texture.h"

static Bool
nv50_xv_check_image_put(PixmapPtr ppix)
{
	struct nouveau_pixmap *rt = nouveau_pixmap(ppix);

	if (!rt || !rt->bo || !rt->bo->tiled)
		return FALSE;

	switch (ppix->drawable.depth) {
	case 32:
	case 24:
	case 16:
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

int
nv50_xv_image_put(ScrnInfoPtr pScrn,
		  struct nouveau_bo *src, int src_offset, int src_offset2,
		  int id, int src_pitch, BoxPtr dstBox,
		  int x1, int y1, int x2, int y2,
		  uint16_t width, uint16_t height,
		  uint16_t src_w, uint16_t src_h,
		  uint16_t drw_w, uint16_t drw_h,
		  RegionPtr clipBoxes, PixmapPtr ppix,
		  NVPortPrivPtr pPriv)
{
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *tesla = pNv->Nv3D;
	struct nouveau_pixmap *rt = nouveau_pixmap(ppix);
	float X1, X2, Y1, Y2;
	BoxPtr pbox;
	int nbox;

	if (!nv50_xv_check_image_put(ppix))
		return BadMatch;

	BEGIN_RING(chan, tesla, NV50TCL_RT_ADDRESS_HIGH(0), 5);
	OUT_RELOCh(chan, rt->bo, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR);
	OUT_RELOCl(chan, rt->bo, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR);
	switch (ppix->drawable.depth) {
	case 32: OUT_RING  (chan, NV50TCL_RT_FORMAT_32BPP); break;
	case 24: OUT_RING  (chan, NV50TCL_RT_FORMAT_24BPP); break;
	case 16: OUT_RING  (chan, NV50TCL_RT_FORMAT_16BPP); break;
	}
	OUT_RING  (chan, 0);
	OUT_RING  (chan, 0);
	BEGIN_RING(chan, tesla, NV50TCL_RT_HORIZ(0), 2);
	OUT_RING  (chan, ppix->drawable.width);
	OUT_RING  (chan, ppix->drawable.height);
	BEGIN_RING(chan, tesla, 0x1224, 1);
	OUT_RING  (chan, 1);

	BEGIN_RING(chan, tesla, NV50TCL_BLEND_ENABLE(0), 1);
	OUT_RING  (chan, 0);

	BEGIN_RING(chan, tesla, NV50TCL_CB_ADDR, 1);
	OUT_RING  (chan, CB_TIC);
	BEGIN_RING(chan, tesla, NV50TCL_CB_DATA(0) | 0x40000000, 16);
	if (id == FOURCC_YV12 || id == FOURCC_I420) {
	OUT_RING  (chan, NV50TIC_0_0_MAPA_C0 | NV50TIC_0_0_TYPEA_UNORM |
			 NV50TIC_0_0_MAPR_ZERO | NV50TIC_0_0_TYPER_UNORM |
			 NV50TIC_0_0_MAPG_ZERO | NV50TIC_0_0_TYPEG_UNORM |
			 NV50TIC_0_0_MAPB_ZERO | NV50TIC_0_0_TYPEB_UNORM |
			 NV50TIC_0_0_FMT_8);
	OUT_RELOCl(chan, src,
			 src_offset, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_RING  (chan, 0xd0005000);
	OUT_RING  (chan, 0x00300000);
	OUT_RING  (chan, src_w);
	OUT_RING  (chan, (1 << NV50TIC_0_5_DEPTH_SHIFT) | src_h);
	OUT_RING  (chan, 0x03000000);
	OUT_RELOCh(chan, src,
			 src_offset, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_RING  (chan, NV50TIC_0_0_MAPA_C1 | NV50TIC_0_0_TYPEA_UNORM |
			 NV50TIC_0_0_MAPR_C0 | NV50TIC_0_0_TYPER_UNORM |
			 NV50TIC_0_0_MAPG_ZERO | NV50TIC_0_0_TYPEG_UNORM |
			 NV50TIC_0_0_MAPB_ZERO | NV50TIC_0_0_TYPEB_UNORM |
			 NV50TIC_0_0_FMT_8_8);
	OUT_RELOCl(chan, src,
			 src_offset2, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_RING  (chan, 0xd0005000);
	OUT_RING  (chan, 0x00300000);
	OUT_RING  (chan, src_w >> 1);
	OUT_RING  (chan, (1 << NV50TIC_0_5_DEPTH_SHIFT) | (src_h >> 1));
	OUT_RING  (chan, 0x03000000);
	OUT_RELOCh(chan, src,
			 src_offset2, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	} else {
	OUT_RING  (chan, NV50TIC_0_0_MAPA_C0 | NV50TIC_0_0_TYPEA_UNORM |
			 NV50TIC_0_0_MAPR_ZERO | NV50TIC_0_0_TYPER_UNORM |
			 NV50TIC_0_0_MAPG_ZERO | NV50TIC_0_0_TYPEG_UNORM |
			 NV50TIC_0_0_MAPB_ZERO | NV50TIC_0_0_TYPEB_UNORM |
			 NV50TIC_0_0_FMT_8_8);
	OUT_RELOCl(chan, src,
			 src_offset, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_RING  (chan, 0xd0005000);
	OUT_RING  (chan, 0x00300000);
	OUT_RING  (chan, src_w);
	OUT_RING  (chan, (1 << NV50TIC_0_5_DEPTH_SHIFT) | src_h);
	OUT_RING  (chan, 0x03000000);
	OUT_RELOCh(chan, src,
			 src_offset, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_RING  (chan, NV50TIC_0_0_MAPA_C3 | NV50TIC_0_0_TYPEA_UNORM |
			 NV50TIC_0_0_MAPR_C1 | NV50TIC_0_0_TYPER_UNORM |
			 NV50TIC_0_0_MAPG_ZERO | NV50TIC_0_0_TYPEG_UNORM |
			 NV50TIC_0_0_MAPB_ZERO | NV50TIC_0_0_TYPEB_UNORM |
			 NV50TIC_0_0_FMT_8_8_8_8);
	OUT_RELOCl(chan, src,
			 src_offset, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_RING  (chan, 0xd0005000);
	OUT_RING  (chan, 0x00300000);
	OUT_RING  (chan, (src_w >> 1));
	OUT_RING  (chan, (1 << NV50TIC_0_5_DEPTH_SHIFT) | src_h);
	OUT_RING  (chan, 0x03000000);
	OUT_RELOCh(chan, src,
			 src_offset, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	}

	BEGIN_RING(chan, tesla, NV50TCL_CB_ADDR, 1);
	OUT_RING  (chan, CB_TSC);
	BEGIN_RING(chan, tesla, NV50TCL_CB_DATA(0) | 0x40000000, 16);
	OUT_RING  (chan, NV50TSC_1_0_WRAPS_CLAMP_TO_EDGE |
			 NV50TSC_1_0_WRAPT_CLAMP_TO_EDGE |
			 NV50TSC_1_0_WRAPR_CLAMP_TO_EDGE);
	OUT_RING  (chan, NV50TSC_1_1_MAGF_LINEAR |
			 NV50TSC_1_1_MINF_LINEAR |
			 NV50TSC_1_1_MIPF_NONE);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, NV50TSC_1_0_WRAPS_CLAMP_TO_EDGE |
			 NV50TSC_1_0_WRAPT_CLAMP_TO_EDGE |
			 NV50TSC_1_0_WRAPR_CLAMP_TO_EDGE);
	OUT_RING  (chan, NV50TSC_1_1_MAGF_LINEAR |
			 NV50TSC_1_1_MINF_LINEAR |
			 NV50TSC_1_1_MIPF_NONE);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);
	OUT_RING  (chan, 0x00000000);

	BEGIN_RING(chan, tesla, NV50TCL_FP_START_ID, 1);
	OUT_RING  (chan, PFP_NV12);

	BEGIN_RING(chan, tesla, 0x1334, 1);
	OUT_RING  (chan, 0);

	BEGIN_RING(chan, tesla, 0x1458, 1);
	OUT_RING  (chan, 1);
	BEGIN_RING(chan, tesla, 0x1458, 1);
	OUT_RING  (chan, 0x203);

	/* These are fixed point values in the 16.16 format. */
	X1 = (float)(x1>>16)+(float)(x1&0xFFFF)/(float)0x10000;
	Y1 = (float)(y1>>16)+(float)(y1&0xFFFF)/(float)0x10000;
	X2 = (float)(x2>>16)+(float)(x2&0xFFFF)/(float)0x10000;
	Y2 = (float)(y2>>16)+(float)(y2&0xFFFF)/(float)0x10000;

	BEGIN_RING(chan, tesla, NV50TCL_VERTEX_BEGIN, 1);
	OUT_RING  (chan, NV50TCL_VERTEX_BEGIN_QUADS);

	pbox = REGION_RECTS(clipBoxes);
	nbox = REGION_NUM_RECTS(clipBoxes);
	while(nbox--) {
		float tx1=X1+(float)(pbox->x1 - dstBox->x1)*(X2-X1)/(float)(drw_w);
		float tx2=X1+(float)(pbox->x2 - dstBox->x1)*(src_w)/(float)(drw_w);
		float ty1=Y1+(float)(pbox->y1 - dstBox->y1)*(Y2-Y1)/(float)(drw_h);
		float ty2=Y1+(float)(pbox->y2 - dstBox->y1)*(src_h)/(float)(drw_h);
		int sx1=pbox->x1;
		int sx2=pbox->x2;
		int sy1=pbox->y1;
		int sy2=pbox->y2;

		tx1 = tx1 / src_w;
		tx2 = tx2 / src_w;
		ty1 = ty1 / src_h;
		ty2 = ty2 / src_h;

		VTX2s(pNv, tx1, ty1, tx1, ty1, sx1, sy1);
		VTX2s(pNv, tx2, ty1, tx2, ty1, sx2, sy1);
		VTX2s(pNv, tx2, ty2, tx2, ty2, sx2, sy2);
		VTX2s(pNv, tx1, ty2, tx1, ty2, sx1, sy2);

		pbox++;
	}

	BEGIN_RING(chan, tesla, NV50TCL_VERTEX_END, 1);
	OUT_RING  (chan, 0);

	FIRE_RING (chan);

	return Success;
}

void
nv50_xv_video_stop(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
}

int
nv50_xv_port_attribute_set(ScrnInfoPtr pScrn, Atom attribute,
			   INT32 value, pointer data)
{
	return BadMatch;
}

int
nv50_xv_port_attribute_get(ScrnInfoPtr pScrn, Atom attribute,
			   INT32 *value, pointer data)
{
	return BadMatch;
}

