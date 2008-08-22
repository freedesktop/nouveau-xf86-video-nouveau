/*
 * Copyright 2003 NVIDIA, Corporation
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

#include "nv_include.h"
#include "exa.h"

#include "nv_dma.h"
#include "nv_local.h"

#include <sys/time.h>

const int NVCopyROP[16] =
{
   0x00,            /* GXclear */
   0x88,            /* GXand */
   0x44,            /* GXandReverse */
   0xCC,            /* GXcopy */
   0x22,            /* GXandInverted */
   0xAA,            /* GXnoop */
   0x66,            /* GXxor */
   0xEE,            /* GXor */
   0x11,            /* GXnor */
   0x99,            /* GXequiv */
   0x55,            /* GXinvert*/
   0xDD,            /* GXorReverse */
   0x33,            /* GXcopyInverted */
   0xBB,            /* GXorInverted */
   0x77,            /* GXnand */
   0xFF             /* GXset */
};

static void 
NVSetPattern(ScrnInfoPtr pScrn, CARD32 clr0, CARD32 clr1,
				CARD32 pat0, CARD32 pat1)
{
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *patt = pNv->NvImagePattern;

	BEGIN_RING(chan, patt, NV04_IMAGE_PATTERN_MONOCHROME_COLOR0, 4);
	OUT_RING  (chan, clr0);
	OUT_RING  (chan, clr1);
	OUT_RING  (chan, pat0);
	OUT_RING  (chan, pat1);
}

static void 
NVSetROP(ScrnInfoPtr pScrn, CARD32 alu, CARD32 planemask)
{
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *objrop = pNv->NvRop;
	int rop = NVCopyROP[alu] & 0xf0;

	if (planemask != ~0) {
		NVSetPattern(pScrn, 0, planemask, ~0, ~0);
		if (pNv->currentRop != (alu + 32)) {
			BEGIN_RING(chan, objrop, NV03_CONTEXT_ROP_ROP, 1);
			OUT_RING  (chan, rop | 0x0a);
			pNv->currentRop = alu + 32;
		}
	} else
	if (pNv->currentRop != alu) {
		if(pNv->currentRop >= 16)
			NVSetPattern(pScrn, ~0, ~0, ~0, ~0);
		BEGIN_RING(chan, objrop, NV03_CONTEXT_ROP_ROP, 1);
		OUT_RING  (chan, rop | (rop >> 4));
		pNv->currentRop = alu;
	}
}

/* EXA acceleration hooks */
static int
NVExaMarkSync(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);

	nouveau_fence_ref(pNv->chan->pushbuf->fence, &pNv->exa_sync);
	return 0;
}

static void
NVExaWaitMarker(ScreenPtr pScreen, int marker)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);

	/*XXX*/
	struct nouveau_fence_priv *nvfence = nouveau_fence(pNv->exa_sync);
	if (!nvfence->emitted)
		FIRE_RING(pNv->chan);

	nouveau_fence_wait(&pNv->exa_sync);
}

static Bool NVExaPrepareSolid(PixmapPtr pPixmap,
			      int   alu,
			      Pixel planemask,
			      Pixel fg)
{
	ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *surf2d = pNv->NvContextSurfaces;
	struct nouveau_grobj *rect = pNv->NvRectangle;
	unsigned int fmt, pitch, color;

	planemask |= ~0 << pPixmap->drawable.bitsPerPixel;
	if (planemask != ~0 || alu != GXcopy) {
		if (pPixmap->drawable.bitsPerPixel == 32)
			return FALSE;
		BEGIN_RING(chan, rect, NV04_GDI_RECTANGLE_TEXT_OPERATION, 1);
		OUT_RING  (chan, 1); /* ROP_AND */
		NVSetROP(pScrn, alu, planemask);
	} else {
		BEGIN_RING(chan, rect, NV04_GDI_RECTANGLE_TEXT_OPERATION, 1);
		OUT_RING  (chan, 3); /* SRCCOPY */
	}

	if (!NVAccelGetCtxSurf2DFormatFromPixmap(pPixmap, (int*)&fmt))
		return FALSE;
	pitch = exaGetPixmapPitch(pPixmap);

	if (pPixmap->drawable.bitsPerPixel == 16) {
		/* convert to 32bpp */
		uint32_t r =  (fg&0x1F)          * 255 / 31;
		uint32_t g = ((fg&0x7E0) >> 5)   * 255 / 63;
		uint32_t b = ((fg&0xF100) >> 11) * 255 / 31;
		color = b<<16 | g<<8 | r;
	} else 
		color = fg;

	/* When SURFACE_FORMAT_A8R8G8B8 is used with GDI_RECTANGLE_TEXT, the 
	 * alpha channel gets forced to 0xFF for some reason.  We're using 
	 * SURFACE_FORMAT_Y32 as a workaround
	 */
	if (fmt == NV04_CONTEXT_SURFACES_2D_FORMAT_A8R8G8B8)
		fmt = NV04_CONTEXT_SURFACES_2D_FORMAT_Y32;

	BEGIN_RING(chan, surf2d, NV04_CONTEXT_SURFACES_2D_FORMAT, 4);
	OUT_RING  (chan, fmt);
	OUT_RING  (chan, (pitch << 16) | pitch);
	OUT_PIXMAPl(chan, pPixmap, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR);
	OUT_PIXMAPl(chan, pPixmap, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR);

	BEGIN_RING(chan, rect, NV04_GDI_RECTANGLE_TEXT_COLOR_FORMAT, 1);
	OUT_RING  (chan, NV04_GDI_RECTANGLE_TEXT_COLOR_FORMAT_A8R8G8B8);
	BEGIN_RING(chan, rect, NV04_GDI_RECTANGLE_TEXT_COLOR1_A, 1);
	OUT_RING (chan, color);

	return TRUE;
}

static void NVExaSolid (PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
	ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *rect = pNv->NvRectangle;
	int width = x2-x1;
	int height = y2-y1;

	BEGIN_RING(chan, rect,
		   NV04_GDI_RECTANGLE_TEXT_UNCLIPPED_RECTANGLE_POINT(0), 2);
	OUT_RING  (chan, (x1 << 16) | y1);
	OUT_RING  (chan, (width << 16) | height);

	if((width * height) >= 512)
		FIRE_RING (chan);
}

static void NVExaDoneSolid (PixmapPtr pPixmap)
{
}

static Bool NVExaPrepareCopy(PixmapPtr pSrcPixmap,
			     PixmapPtr pDstPixmap,
			     int       dx,
			     int       dy,
			     int       alu,
			     Pixel     planemask)
{
	ScrnInfoPtr pScrn = xf86Screens[pSrcPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *surf2d = pNv->NvContextSurfaces;
	struct nouveau_grobj *blit = pNv->NvImageBlit;
	int fmt;

	if (pSrcPixmap->drawable.bitsPerPixel !=
			pDstPixmap->drawable.bitsPerPixel)
		return FALSE;

	planemask |= ~0 << pDstPixmap->drawable.bitsPerPixel;
	if (planemask != ~0 || alu != GXcopy) {
		if (pDstPixmap->drawable.bitsPerPixel == 32)
			return FALSE;
		BEGIN_RING(chan, blit, NV04_IMAGE_BLIT_OPERATION, 1);
		OUT_RING  (chan, 1); /* ROP_AND */
		NVSetROP(pScrn, alu, planemask);
	} else {
		BEGIN_RING(chan, blit, NV04_IMAGE_BLIT_OPERATION, 1);
		OUT_RING  (chan, 3); /* SRCCOPY */
	}

	if (!NVAccelGetCtxSurf2DFormatFromPixmap(pDstPixmap, &fmt))
		return FALSE;

	BEGIN_RING(chan, surf2d, NV04_CONTEXT_SURFACES_2D_FORMAT, 4);
	OUT_RING  (chan, fmt);
	OUT_RING  (chan, (exaGetPixmapPitch(pDstPixmap) << 16) |
		   (exaGetPixmapPitch(pSrcPixmap)));
	OUT_PIXMAPl(chan, pSrcPixmap, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_PIXMAPl(chan, pDstPixmap, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR);

	return TRUE;
}

static void NVExaCopy(PixmapPtr pDstPixmap,
		      int	srcX,
		      int	srcY,
		      int	dstX,
		      int	dstY,
		      int	width,
		      int	height)
{
	ScrnInfoPtr pScrn = xf86Screens[pDstPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *blit = pNv->NvImageBlit;

	BEGIN_RING(chan, blit, NV01_IMAGE_BLIT_POINT_IN, 3);
	OUT_RING  (chan, (srcY << 16) | srcX);
	OUT_RING  (chan, (dstY << 16) | dstX);
	OUT_RING  (chan, (height  << 16) | width);

	if((width * height) >= 512)
		FIRE_RING (chan);
}

static void NVExaDoneCopy (PixmapPtr pDstPixmap) {}

static inline Bool NVAccelMemcpyRect(char *dst, const char *src, int height,
		       int dst_pitch, int src_pitch, int line_len)
{
	if ((src_pitch == line_len) && (src_pitch == dst_pitch)) {
		memcpy(dst, src, line_len*height);
	} else {
		while (height--) {
			memcpy(dst, src, line_len);
			src += src_pitch;
			dst += dst_pitch;
		}
	}

	return TRUE;
}

static inline Bool
NVAccelDownloadM2MF(PixmapPtr pspix, int x, int y, int w, int h,
		    char *dst, unsigned dst_pitch)
{
	ScrnInfoPtr pScrn = xf86Screens[pspix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *m2mf = pNv->NvMemFormat;
	unsigned cpp = pspix->drawable.bitsPerPixel / 8;
	unsigned line_len = w * cpp;
	unsigned src_pitch = 0, src_offset = 0, linear = 0;
	struct nouveau_pixmap *nvpix;

	nvpix = exaGetPixmapDriverPrivate(pspix);
	if (!nvpix || !nvpix->bo)
		return FALSE;

	BEGIN_RING(chan, m2mf, 0x184, 2);
	OUT_PIXMAPo(chan, pspix,
		    NOUVEAU_BO_GART | NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
	OUT_RELOCo(chan, pNv->GART, NOUVEAU_BO_GART | NOUVEAU_BO_WR);

	if (!nvpix->bo->tiled) {
		linear     = 1;
		src_pitch  = exaGetPixmapPitch(pspix);
		src_offset = (y * src_pitch) + (x * cpp);
	}

	if (pNv->Architecture >= NV_ARCH_50) {
		if (linear) {
			BEGIN_RING(chan, m2mf, 0x0200, 1);
			OUT_RING  (chan, 1);
		} else {
			BEGIN_RING(chan, m2mf, 0x0200, 6);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, exaGetPixmapPitch(pspix));
			OUT_RING  (chan, pspix->drawable.height);
			OUT_RING  (chan, 1);
			OUT_RING  (chan, 0);
		}

		BEGIN_RING(chan, m2mf, 0x021c, 1);
		OUT_RING  (chan, 1);
	}

	while (h) {
		int line_count, i;

		if (h * line_len <= pNv->GART->size) {
			line_count = h;
		} else {
			line_count = pNv->GART->size / line_len;
			if (line_count > h)
				line_count = h;
		}

		/* HW limitations */
		if (line_count > 2047)
			line_count = 2047;

		if (pNv->Architecture >= NV_ARCH_50) {
			if (!linear) {
				BEGIN_RING(chan, m2mf, 0x0218, 1);
				OUT_RING  (chan, (y << 16) | (x * cpp));
			}

			BEGIN_RING(chan, m2mf, 0x238, 2);
			OUT_PIXMAPh(chan, pspix, src_offset, NOUVEAU_BO_GART |
				    NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
			OUT_RELOCh(chan, pNv->GART, 0, NOUVEAU_BO_GART |
				   NOUVEAU_BO_WR);
		}

		BEGIN_RING(chan, m2mf,
			   NV04_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
		OUT_PIXMAPl(chan, pspix, src_offset, NOUVEAU_BO_GART |
			    NOUVEAU_BO_VRAM | NOUVEAU_BO_RD);
		OUT_RELOCl(chan, pNv->GART, 0, NOUVEAU_BO_GART | NOUVEAU_BO_WR);
		OUT_RING  (chan, src_pitch);
		OUT_RING  (chan, line_len);
		OUT_RING  (chan, line_len);
		OUT_RING  (chan, line_count);
		OUT_RING  (chan, (1<<8)|1);
		OUT_RING  (chan, 0);

		nouveau_bo_map(pNv->GART, NOUVEAU_BO_RD);
		if (dst_pitch == line_len) {
			memcpy(dst, pNv->GART->map, dst_pitch * line_count);
			dst += dst_pitch * line_count;
		} else {
			const void *src = pNv->GART->map;

			for (i = 0; i < line_count; i++) {
				memcpy(dst, src, line_len);
				src += line_len;
				dst += dst_pitch;
			}
		}
		nouveau_bo_unmap(pNv->GART);

		if (linear)
			src_offset += line_count * src_pitch;
		h -= line_count;
		y += line_count;
	}

	return TRUE;
}

static inline void *
NVExaPixmapMap(PixmapPtr pPix)
{
	struct nouveau_pixmap *nvpix;

	nvpix = exaGetPixmapDriverPrivate(pPix);
	if (!nvpix || !nvpix->bo)
		return NULL;

	nouveau_bo_map(nvpix->bo, NOUVEAU_BO_RDWR);
	return nvpix->bo->map;
}

static inline void
NVExaPixmapUnmap(PixmapPtr pPix)
{
	struct nouveau_pixmap *nvpix;

	nvpix = exaGetPixmapDriverPrivate(pPix);
	if (!nvpix || !nvpix->bo)
		return;

	nouveau_bo_unmap(nvpix->bo);
}

static Bool NVDownloadFromScreen(PixmapPtr pSrc,
				 int x,  int y,
				 int w,  int h,
				 char *dst,  int dst_pitch)
{
	ScrnInfoPtr pScrn = xf86Screens[pSrc->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	int src_pitch, cpp, offset;
	const char *src;

	src_pitch  = exaGetPixmapPitch(pSrc);
	cpp = pSrc->drawable.bitsPerPixel >> 3;
	offset = (y * src_pitch) + (x * cpp);

	if (pNv->GART) {
		if (NVAccelDownloadM2MF(pSrc, x, y, w, h, dst, dst_pitch))
			return TRUE;
	}

	src = NVExaPixmapMap(pSrc);
	if (!src)
		return FALSE;
	src += offset;
	exaWaitSync(pSrc->drawable.pScreen);
	if (NVAccelMemcpyRect(dst, src, h, dst_pitch, src_pitch, w*cpp)) {
		NVExaPixmapUnmap(pSrc);
		return TRUE;
	}
	NVExaPixmapUnmap(pSrc);

	return FALSE;
}

static inline Bool
NVAccelUploadIFC(ScrnInfoPtr pScrn, const char *src, int src_pitch, 
		 PixmapPtr pDst, int x, int y, int w, int h, int cpp)
{
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *surf2d = pNv->NvContextSurfaces;
	struct nouveau_grobj *clip = pNv->NvClipRectangle;
	struct nouveau_grobj *ifc = pNv->NvImageFromCpu;
	int line_len = w * cpp;
	int iw, id, surf_fmt, ifc_fmt;
	int padbytes;

	if (pNv->Architecture >= NV_ARCH_50)
		return FALSE;

	if (h > 1024)
		return FALSE;

	if (line_len<4)
		return FALSE;

	switch (cpp) {
	case 2: ifc_fmt = 1; break;
	case 4: ifc_fmt = 4; break;
	default:
		return FALSE;
	}

	if (!NVAccelGetCtxSurf2DFormatFromPixmap(pDst, &surf_fmt))
		return FALSE;

	BEGIN_RING(chan, surf2d, NV04_CONTEXT_SURFACES_2D_FORMAT, 4);
	OUT_RING  (chan, surf_fmt);
	OUT_RING  (chan, (exaGetPixmapPitch(pDst) << 16) | exaGetPixmapPitch(pDst));
	OUT_PIXMAPl(chan, pDst, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR);
	OUT_PIXMAPl(chan, pDst, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR);

	/* Pad out input width to cover both COLORA() and COLORB() */
	iw  = (line_len + 7) & ~7;
	padbytes = iw - line_len;
	id  = iw / 4; /* line push size */
	iw /= cpp;

	/* Don't support lines longer than max push size yet.. */
	if (id > 1792)
		return FALSE;

	BEGIN_RING(chan, clip, NV01_CONTEXT_CLIP_RECTANGLE_POINT, 2);
	OUT_RING  (chan, 0x0); 
	OUT_RING  (chan, 0x7FFF7FFF);

	BEGIN_RING(chan, ifc, NV01_IMAGE_FROM_CPU_OPERATION, 2);
	OUT_RING  (chan, NV01_IMAGE_FROM_CPU_OPERATION_SRCCOPY);
	OUT_RING  (chan, ifc_fmt);
	BEGIN_RING(chan, ifc, NV01_IMAGE_FROM_CPU_POINT, 3);
	OUT_RING  (chan, (y << 16) | x); /* dst point */
	OUT_RING  (chan, (h << 16) | w); /* width/height out */
	OUT_RING  (chan, (h << 16) | iw); /* width/height in */

	if (padbytes)
		h--;
	while (h--) {
		/* send a line */
		BEGIN_RING(chan, ifc, NV01_IMAGE_FROM_CPU_COLOR(0), id);
		OUT_RINGp (chan, src, id);

		src += src_pitch;
	}
	if (padbytes) {
		char padding[8];
		int aux = (padbytes + 7) >> 2;
		BEGIN_RING(chan, ifc, NV01_IMAGE_FROM_CPU_COLOR(0), id);
		OUT_RINGp (chan, src, id - aux);
		memcpy(padding, src + (id - aux) * 4, padbytes);
		OUT_RINGp (chan, padding, aux);
	}

	return TRUE;
}

static inline Bool
NVAccelUploadM2MF(PixmapPtr pdpix, int x, int y, int w, int h,
		  const char *src, int src_pitch)
{
	ScrnInfoPtr pScrn = xf86Screens[pdpix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_channel *chan = pNv->chan;
	struct nouveau_grobj *m2mf = pNv->NvMemFormat;
	unsigned cpp = pdpix->drawable.bitsPerPixel / 8;
	unsigned line_len = w * cpp;
	unsigned dst_pitch = 0, dst_offset = 0, linear = 0;
	struct nouveau_pixmap *nvpix;

	nvpix = exaGetPixmapDriverPrivate(pdpix);
	if (!nvpix || !nvpix->bo)
		return FALSE;

	BEGIN_RING(chan, m2mf, 0x184, 2);
	OUT_RELOCo(chan, pNv->GART, NOUVEAU_BO_GART | NOUVEAU_BO_RD);
	OUT_PIXMAPo(chan, pdpix,
		    NOUVEAU_BO_VRAM | NOUVEAU_BO_GART | NOUVEAU_BO_WR);

	if (!nvpix->bo->tiled) {
		linear     = 1;
		dst_pitch  = exaGetPixmapPitch(pdpix);
		dst_offset = (y * dst_pitch) + (x * cpp);
	}

	if (pNv->Architecture >= NV_ARCH_50) {
		BEGIN_RING(chan, m2mf, 0x0200, 1);
		OUT_RING  (chan, 1);

		if (linear) {
			BEGIN_RING(chan, m2mf, 0x021c, 1);
			OUT_RING  (chan, 1);
		} else {
			BEGIN_RING(chan, m2mf, 0x021c, 6);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, 0);
			OUT_RING  (chan, exaGetPixmapPitch(pdpix));
			OUT_RING  (chan, pdpix->drawable.height);
			OUT_RING  (chan, 1);
			OUT_RING  (chan, 0);
		}
	}

	while (h) {
		int line_count, i;

		/* Determine max amount of data we can DMA at once */
		if (h * line_len <= pNv->GART->size) {
			line_count = h;
		} else {
			line_count = pNv->GART->size / line_len;
			if (line_count > h)
				line_count = h;
		}

		/* HW limitations */
		if (line_count > 2047)
			line_count = 2047;

		/* Upload to GART */
		nouveau_bo_map(pNv->GART, NOUVEAU_BO_WR);
		if (src_pitch == line_len) {
			memcpy(pNv->GART->map, src, src_pitch * line_count);
			src += src_pitch * line_count;
		} else {
			void *dst = pNv->GART->map;

			for (i = 0; i < line_count; i++) {
				memcpy(dst, src, line_len);
				src += src_pitch;
				dst += line_len;
			}
		}
		nouveau_bo_unmap(pNv->GART);

		if (pNv->Architecture >= NV_ARCH_50) {
			if (!linear) {
				BEGIN_RING(chan, m2mf, 0x0234, 1);
				OUT_RING  (chan, (y << 16) | (x * cpp));
			}

			BEGIN_RING(chan, m2mf, 0x0238, 2);
			OUT_RELOCh(chan, pNv->GART, 0, NOUVEAU_BO_GART |
				   NOUVEAU_BO_RD);
			OUT_PIXMAPh(chan, pdpix, dst_offset, NOUVEAU_BO_VRAM | 
				    NOUVEAU_BO_GART | NOUVEAU_BO_WR);
		}

		/* DMA to VRAM */
		BEGIN_RING(chan, m2mf,
			   NV04_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
		OUT_RELOCl(chan, pNv->GART, 0, NOUVEAU_BO_GART | NOUVEAU_BO_RD);
		OUT_PIXMAPl(chan, pdpix, dst_offset, NOUVEAU_BO_VRAM |
			    NOUVEAU_BO_GART | NOUVEAU_BO_WR);
		OUT_RING  (chan, line_len);
		OUT_RING  (chan, dst_pitch);
		OUT_RING  (chan, line_len);
		OUT_RING  (chan, line_count);
		OUT_RING  (chan, (1<<8)|1);
		OUT_RING  (chan, 0);

		if (linear)
			dst_offset += line_count * dst_pitch;
		h -= line_count;
		y += line_count;
	}

	return TRUE;
}

static Bool NVUploadToScreen(PixmapPtr pDst,
			     int x, int y, int w, int h,
			     char *src, int src_pitch)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	int dst_pitch, cpp;
	char *dst;

	dst_pitch  = exaGetPixmapPitch(pDst);
	cpp = pDst->drawable.bitsPerPixel >> 3;

	/* try hostdata transfer */
	if (w * h * cpp < 16*1024) /* heuristic */
	{
		if (pNv->Architecture < NV_ARCH_50) {
			if (NVAccelUploadIFC(pScrn, src, src_pitch, pDst,
					     x, y, w, h, cpp)) {
				exaMarkSync(pDst->drawable.pScreen);
				return TRUE;
			}
		} else {
			if (NV50EXAUploadSIFC(src, src_pitch, pDst,
					      x, y, w, h, cpp)) {
				exaMarkSync(pDst->drawable.pScreen);
				return TRUE;
			}
		}
	}

	/* try gart-based transfer */
	if (pNv->GART) {
		if (NVAccelUploadM2MF(pDst, x, y, w, h, src, src_pitch)) {
			exaMarkSync(pDst->drawable.pScreen);
			return TRUE;
		}
	}

	/* fallback to memcpy-based transfer */
	dst = NVExaPixmapMap(pDst);
	if (!dst)
		return FALSE;
	dst += (y * dst_pitch) + (x * cpp);
	exaWaitSync(pDst->drawable.pScreen);
	if (NVAccelMemcpyRect(dst, src, h, dst_pitch, src_pitch, w*cpp)) {
		NVExaPixmapUnmap(pDst);
		return TRUE;
	}
	NVExaPixmapUnmap(pDst);

	return FALSE;
}

static Bool
NVExaPrepareAccess(PixmapPtr pPix, int index)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_pixmap *nvpix;
	(void)pNv;

	nvpix = exaGetPixmapDriverPrivate(pPix);
	if (!nvpix || !nvpix->bo)
		return FALSE;

	/*XXX: ho hum.. sync if needed */

	if (nvpix->mapped)
		return TRUE;

	if (nouveau_bo_map(nvpix->bo, NOUVEAU_BO_RDWR))
		return FALSE;
	pPix->devPrivate.ptr = nvpix->bo->map;
	nvpix->mapped = TRUE;
	return TRUE;
}

static void
NVExaFinishAccess(PixmapPtr pPix, int index)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_pixmap *nvpix;
	(void)pNv;

	nvpix = exaGetPixmapDriverPrivate(pPix);
	if (!nvpix || !nvpix->bo || !nvpix->mapped)
		return;

	nouveau_bo_unmap(nvpix->bo);
	pPix->devPrivate.ptr = NULL;
	nvpix->mapped = FALSE;
}

static Bool
NVExaPixmapIsOffscreen(PixmapPtr pPix)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_pixmap *nvpix;
	(void)pNv;

	nvpix = exaGetPixmapDriverPrivate(pPix);
	if (!nvpix || !nvpix->bo)
		return FALSE;

	return TRUE;
}

static void *
NVExaCreatePixmap(ScreenPtr pScreen, int size, int align)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_pixmap *nvpix;

	nvpix = xcalloc(1, sizeof(struct nouveau_pixmap));
	if (!nvpix)
		return NULL;

	if (size) {
		uint32_t flags = NOUVEAU_BO_VRAM;

		if (pNv->Architecture >= NV_ARCH_50)
			flags |= NOUVEAU_BO_TILED;

		if (nouveau_bo_new(pNv->dev, flags, 0, size,
				   &nvpix->bo)) {
			xfree(nvpix);
			return NULL;
		}
	}

	return nvpix;
}

static void
NVExaDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	struct nouveau_pixmap *nvpix = driverPriv;

	if (!driverPriv)
		return;

	nouveau_bo_del(&nvpix->bo);
	xfree(nvpix);
}

static Bool
NVExaModifyPixmapHeader(PixmapPtr pPixmap, int width, int height, int depth,
			int bitsPerPixel, int devKind, pointer pPixData)
{
	ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_pixmap *nvpix;

	if (pPixData == pNv->FBMap) {
		nvpix = exaGetPixmapDriverPrivate(pPixmap);
		if (!nvpix)
			return FALSE;

		if (nouveau_bo_ref(pNv->dev, pNv->FB->handle, &nvpix->bo))
			return FALSE;

		miModifyPixmapHeader(pPixmap, width, height, depth,
				     bitsPerPixel, devKind, NULL);
		return TRUE;
	}

	return FALSE;
}

Bool
NVExaPixmapIsOnscreen(PixmapPtr pPixmap)
{
	ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_pixmap *nvpix;

	nvpix = exaGetPixmapDriverPrivate(pPixmap);
	if (nvpix && nvpix->bo == pNv->FB)
		return TRUE;

	return FALSE;
}

Bool
NVExaInit(ScreenPtr pScreen) 
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);

	if(!(pNv->EXADriverPtr = (ExaDriverPtr) xnfcalloc(sizeof(ExaDriverRec), 1))) {
		pNv->NoAccel = TRUE;
		return FALSE;
	}

	pNv->EXADriverPtr->exa_major = EXA_VERSION_MAJOR;
	pNv->EXADriverPtr->exa_minor = EXA_VERSION_MINOR;

	pNv->EXADriverPtr->flags = EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS;
	pNv->EXADriverPtr->PrepareAccess = NVExaPrepareAccess;
	pNv->EXADriverPtr->FinishAccess = NVExaFinishAccess;
	pNv->EXADriverPtr->PixmapIsOffscreen = NVExaPixmapIsOffscreen;
	pNv->EXADriverPtr->CreatePixmap = NVExaCreatePixmap;
	pNv->EXADriverPtr->DestroyPixmap = NVExaDestroyPixmap;
	pNv->EXADriverPtr->ModifyPixmapHeader = NVExaModifyPixmapHeader;

	if (pNv->Architecture < NV_ARCH_50)
		pNv->EXADriverPtr->pixmapOffsetAlign = 256; 
	else
		pNv->EXADriverPtr->pixmapOffsetAlign = 65536; /* fuck me! */
	pNv->EXADriverPtr->pixmapPitchAlign = 64; 

	if (pNv->Architecture >= NV_ARCH_50) {
		pNv->EXADriverPtr->maxX = 8192;
		pNv->EXADriverPtr->maxY = 8192;
	} else
	if (pNv->Architecture >= NV_ARCH_20) {
		pNv->EXADriverPtr->maxX = 4096;
		pNv->EXADriverPtr->maxY = 4096;
	} else {
		pNv->EXADriverPtr->maxX = 2048;
		pNv->EXADriverPtr->maxY = 2048;
	}

	pNv->EXADriverPtr->MarkSync   = NVExaMarkSync;
	pNv->EXADriverPtr->WaitMarker = NVExaWaitMarker;

	/* Install default hooks */
	pNv->EXADriverPtr->DownloadFromScreen = NVDownloadFromScreen; 
	pNv->EXADriverPtr->UploadToScreen = NVUploadToScreen; 

	if (pNv->Architecture < NV_ARCH_50) {
		pNv->EXADriverPtr->PrepareCopy = NVExaPrepareCopy;
		pNv->EXADriverPtr->Copy = NVExaCopy;
		pNv->EXADriverPtr->DoneCopy = NVExaDoneCopy;

		pNv->EXADriverPtr->PrepareSolid = NVExaPrepareSolid;
		pNv->EXADriverPtr->Solid = NVExaSolid;
		pNv->EXADriverPtr->DoneSolid = NVExaDoneSolid;
	} else {
		pNv->EXADriverPtr->PrepareCopy = NV50EXAPrepareCopy;
		pNv->EXADriverPtr->Copy = NV50EXACopy;
		pNv->EXADriverPtr->DoneCopy = NV50EXADoneCopy;

		pNv->EXADriverPtr->PrepareSolid = NV50EXAPrepareSolid;
		pNv->EXADriverPtr->Solid = NV50EXASolid;
		pNv->EXADriverPtr->DoneSolid = NV50EXADoneSolid;
	}

	switch (pNv->Architecture) {	
	case NV_ARCH_10:
	case NV_ARCH_20:
 		pNv->EXADriverPtr->CheckComposite   = NV10CheckComposite;
 		pNv->EXADriverPtr->PrepareComposite = NV10PrepareComposite;
 		pNv->EXADriverPtr->Composite        = NV10Composite;
 		pNv->EXADriverPtr->DoneComposite    = NV10DoneComposite;
		break;
	case NV_ARCH_30:
		pNv->EXADriverPtr->CheckComposite   = NV30EXACheckComposite;
		pNv->EXADriverPtr->PrepareComposite = NV30EXAPrepareComposite;
		pNv->EXADriverPtr->Composite        = NV30EXAComposite;
		pNv->EXADriverPtr->DoneComposite    = NV30EXADoneComposite;
		break;
	case NV_ARCH_40:
		pNv->EXADriverPtr->CheckComposite   = NV40EXACheckComposite;
		pNv->EXADriverPtr->PrepareComposite = NV40EXAPrepareComposite;
		pNv->EXADriverPtr->Composite        = NV40EXAComposite;
		pNv->EXADriverPtr->DoneComposite    = NV40EXADoneComposite;
		break;
	case NV_ARCH_50:
		pNv->EXADriverPtr->CheckComposite   = NV50EXACheckComposite;
		pNv->EXADriverPtr->PrepareComposite = NV50EXAPrepareComposite;
		pNv->EXADriverPtr->Composite        = NV50EXAComposite;
		pNv->EXADriverPtr->DoneComposite    = NV50EXADoneComposite;
		break;
	default:
		break;
	}

	if (!exaDriverInit(pScreen, pNv->EXADriverPtr))
		return FALSE;
	else
		/* EXA init catches this, but only for xserver >= 1.4 */
		if (pNv->VRAMPhysicalSize / 2 < NOUVEAU_ALIGN(pScrn->virtualX, 64) * NOUVEAU_ALIGN(pScrn->virtualY, 64) * (pScrn->bitsPerPixel >> 3)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "The virtual screen size's resolution is too big for the video RAM framebuffer at this colour depth.\n");
			return FALSE;
		}

	return TRUE;
}
