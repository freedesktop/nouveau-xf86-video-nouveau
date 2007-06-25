 /***************************************************************************\
|*                                                                           *|
|*       Copyright 2003 NVIDIA, Corporation.  All rights reserved.           *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 2003 NVIDIA, Corporation.  All rights reserved.           *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/*
  Exa Modifications (c) Lars Knoll (lars@trolltech.com)
 */

#include "nv_include.h"
#include "exa.h"

#include "nv_dma.h"
#include "nv_local.h"

#include <sys/time.h>

static void setM2MFDirection(ScrnInfoPtr pScrn, int dir)
{
	NVPtr pNv = NVPTR(pScrn);

	if (pNv->M2MFDirection != dir) {
		NVDmaStart(pNv, NvSubMemFormat, MEMFORMAT_DMA_OBJECT_IN, 2);
		NVDmaNext (pNv, dir ? NvDmaTT : NvDmaFB);
		NVDmaNext (pNv, dir ? NvDmaFB : NvDmaTT);
		pNv->M2MFDirection = dir;
	}
}

static CARD32 rectFormat(DrawablePtr pDrawable)
{
	switch(pDrawable->bitsPerPixel) {
	case 32:
	case 24:
		return RECT_FORMAT_DEPTH24;
		break;
	case 16:
		return RECT_FORMAT_DEPTH16;
		break;
	default:
		return RECT_FORMAT_DEPTH8;
		break;
	}
}

/* EXA acceleration hooks */
static void NVExaWaitMarker(ScreenPtr pScreen, int marker)
{
	NVSync(xf86Screens[pScreen->myNum]);
}

static Bool NVExaPrepareSolid(PixmapPtr pPixmap,
			      int   alu,
			      Pixel planemask,
			      Pixel fg)
{
	ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	int fmt;

	planemask |= ~0 << pPixmap->drawable.bitsPerPixel;
	if (planemask != ~0 || alu != GXcopy) {
		if (pPixmap->drawable.bitsPerPixel == 32)
			return FALSE;
		NVDmaStart(pNv, NvSubRectangle, 0x2fc, 1);
		NVDmaNext (pNv, 1 /* ROP_AND */);
		NVSetRopSolid(pScrn, alu, planemask);
	} else {
		NVDmaStart(pNv, NvSubRectangle, 0x2fc, 1);
		NVDmaNext (pNv, 3 /* SRCCOPY */);
	}

	if (!NVAccelGetCtxSurf2DFormatFromPixmap(pPixmap, &fmt))
		return FALSE;

	/* When SURFACE_FORMAT_A8R8G8B8 is used with GDI_RECTANGLE_TEXT, the 
	 * alpha channel gets forced to 0xFF for some reason.  We're using 
	 * SURFACE_FORMAT_Y32 as a workaround
	 */
	if (fmt == SURFACE_FORMAT_A8R8G8B8)
		fmt = 0xb;

	if (!NVAccelSetCtxSurf2D(pPixmap, pPixmap, fmt))
		return FALSE;

	NVDmaStart(pNv, NvSubRectangle, RECT_FORMAT, 1);
	NVDmaNext (pNv, rectFormat(&pPixmap->drawable));
	NVDmaStart(pNv, NvSubRectangle, RECT_SOLID_COLOR, 1);
	NVDmaNext (pNv, fg);

	pNv->DMAKickoffCallback = NVDmaKickoffCallback;
	return TRUE;
}

static void NVExaSolid (PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
	ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	int width = x2-x1;
	int height = y2-y1;

	NVDmaStart(pNv, NvSubRectangle, RECT_SOLID_RECTS(0), 2);
	NVDmaNext (pNv, (x1 << 16) | y1);
	NVDmaNext (pNv, (width << 16) | height);

	if((width * height) >= 512)
		NVDmaKickoff(pNv);
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
	int fmt;

	if (pSrcPixmap->drawable.bitsPerPixel !=
			pDstPixmap->drawable.bitsPerPixel)
		return FALSE;

	planemask |= ~0 << pDstPixmap->drawable.bitsPerPixel;
	if (planemask != ~0 || alu != GXcopy) {
		if (pDstPixmap->drawable.bitsPerPixel == 32)
			return FALSE;
		NVDmaStart(pNv, NvSubImageBlit, 0x2fc, 1);
		NVDmaNext (pNv, 1 /* ROP_AND */);
		NVSetRopSolid(pScrn, alu, planemask);
	} else {
		NVDmaStart(pNv, NvSubImageBlit, 0x2fc, 1);
		NVDmaNext (pNv, 3 /* SRCCOPY */);
	}

	if (!NVAccelGetCtxSurf2DFormatFromPixmap(pDstPixmap, &fmt))
		return FALSE;
	if (!NVAccelSetCtxSurf2D(pSrcPixmap, pDstPixmap, fmt))
		return FALSE;

	pNv->DMAKickoffCallback = NVDmaKickoffCallback;
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

	/* Now check whether we have the same values for srcY and dstY and
	   whether the used chipset is buggy. Currently we flag all of G70
	   cards as buggy, which is probably much to broad. KoalaBR 
	   16 is an abritrary threshold. It should define the maximum number
	   of lines between dstY and srcY  If the number of lines is below
	   we guess, that the bug won't trigger...
	 */
	if ( ((abs(srcY - dstY)< 16)||(abs(srcX-dstX)<16)) &&
		((((pNv->Chipset & 0xfff0) == CHIPSET_G70) ||
		 ((pNv->Chipset & 0xfff0) == CHIPSET_G71) ||
		 ((pNv->Chipset & 0xfff0) == CHIPSET_G72) ||
		 ((pNv->Chipset & 0xfff0) == CHIPSET_G73) ||
		 ((pNv->Chipset & 0xfff0) == CHIPSET_C512))) )
	{
		int dx=abs(srcX - dstX),dy=abs(srcY - dstY);
		// Ok, let's do it manually unless someone comes up with a better idea
		// 1. If dstY and srcY are really the same, do a copy rowwise
		if (dy<dx) {
			int i,xpos,inc;
			NVDEBUG("ExaCopy: Lines identical:\n");
			if (srcX>=dstX) {
				xpos=0;
				inc=1;
			} else {
				xpos=width-1;
				inc=-1;
			}
			for (i = 0; i < width; i++) {
				NVDmaStart(pNv, NvSubImageBlit, BLIT_POINT_SRC, 3);
				NVDmaNext (pNv, (srcY << 16) | (srcX+xpos));
				NVDmaNext (pNv, (dstY << 16) | (dstX+xpos));
				NVDmaNext (pNv, (height  << 16) | 1);
				xpos+=inc;
			}
		} else {
			// 2. Otherwise we will try a line by line copy in the hope to avoid
			//    the card's bug.
			int i,ypos,inc;
			NVDEBUG("ExaCopy: Lines nearly the same srcY=%d, dstY=%d:\n", srcY, dstY);
			if (srcY>=dstY) {
				ypos=0;
				inc=1;
			} else {
				ypos=height-1;
				inc=-1;
			}
			for (i = 0; i < height; i++) {
				NVDmaStart(pNv, NvSubImageBlit, BLIT_POINT_SRC, 3);
				NVDmaNext (pNv, ((srcY+ypos) << 16) | srcX);
				NVDmaNext (pNv, ((dstY+ypos) << 16) | dstX);
				NVDmaNext (pNv, (1  << 16) | width);
				ypos+=inc;
			}
		} 
	} else {
		NVDEBUG("ExaCopy: Using default path\n");
		NVDmaStart(pNv, NvSubImageBlit, BLIT_POINT_SRC, 3);
		NVDmaNext (pNv, (srcY << 16) | srcX);
		NVDmaNext (pNv, (dstY << 16) | dstX);
		NVDmaNext (pNv, (height  << 16) | width);
	}

	if((width * height) >= 512)
		NVDmaKickoff(pNv); 
}

static void NVExaDoneCopy (PixmapPtr pDstPixmap) {}

static Bool NVDownloadFromScreen(PixmapPtr pSrc,
				 int x,  int y,
				 int w,  int h,
				 char *dst,  int dst_pitch)
{
	ScrnInfoPtr pScrn = xf86Screens[pSrc->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	CARD32 offset_in, pitch_in, max_lines, line_length;
	Bool ret = TRUE;

	pitch_in = exaGetPixmapPitch(pSrc);
	offset_in = NVAccelGetPixmapOffset(pSrc);
	offset_in += y*pitch_in;
	offset_in += x * (pSrc->drawable.bitsPerPixel >> 3);
	max_lines = 65536/dst_pitch + 1;
	line_length = w * (pSrc->drawable.bitsPerPixel >> 3);

	setM2MFDirection(pScrn, 0);

	NVDEBUG("NVDownloadFromScreen: x=%d, y=%d, w=%d, h=%d\n", x, y, w, h);
	NVDEBUG("    pitch_in=%x dst_pitch=%x offset_in=%x",
			pitch_in, dst_pitch, offset_in);
	while (h > 0) {
		int nlines = h > max_lines ? max_lines : h;
		NVDEBUG("     max_lines=%d, h=%d\n", max_lines, h);

		/* reset the notification object */
		NVNotifierReset(pScrn, pNv->Notifier0);
		NVDmaStart(pNv, NvSubMemFormat, MEMFORMAT_NOTIFY, 1);
		NVDmaNext (pNv, 0);

		NVDmaStart(pNv, NvSubMemFormat, MEMFORMAT_OFFSET_IN, 8);
		NVDmaNext (pNv, offset_in);
		NVDmaNext (pNv, (uint32_t)pNv->AGPScratch->offset);
		NVDmaNext (pNv, pitch_in);
		NVDmaNext (pNv, dst_pitch);
		NVDmaNext (pNv, line_length);
		NVDmaNext (pNv, nlines);
		NVDmaNext (pNv, 0x101);
		NVDmaNext (pNv, 0);

		NVDmaKickoff(pNv);
		if (!NVNotifierWaitStatus(pScrn, pNv->Notifier0, 0, 2000)) {
			ret = FALSE;
			goto error;
		}

		memcpy(dst, pNv->AGPScratch->map, nlines*dst_pitch);
		h -= nlines;
		offset_in += nlines*pitch_in;
		dst += nlines*dst_pitch;
	}

error:
	exaMarkSync(pSrc->drawable.pScreen);
	return ret;
}

Bool
NVAccelUploadM2MF(ScrnInfoPtr pScrn, uint64_t dst_offset, const char *src,
				     int dst_pitch, int src_pitch,
				     int line_len, int line_count)
{
	NVPtr pNv = NVPTR(pScrn);

	setM2MFDirection(pScrn, 1);

	while (line_count) {
		char *dst = pNv->AGPScratch->map;
		int lc, i;

		/* Determine max amount of data we can DMA at once */
		if (line_count * line_len <= pNv->AGPScratch->size) {
			lc = line_count;
		} else {
			lc = pNv->AGPScratch->size / line_len;
			if (lc > line_count)
				lc = line_count;
		}
		/*XXX: and hw limitations? */

		/* Upload to GART */
		if (src_pitch == line_len) {
			memcpy(dst, src, src_pitch * lc);
		} else {
			for (i = 0; i < lc; i++) {
				memcpy(dst, src, line_len);
				src += src_pitch;
				dst += line_len;
			}
		}

		/* DMA to VRAM */
		NVNotifierReset(pScrn, pNv->Notifier0);
		NVDmaStart(pNv, NvSubMemFormat,
				NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY, 1);
		NVDmaNext (pNv, 0);

		NVDmaStart(pNv, NvSubMemFormat,
				NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
		NVDmaNext (pNv, (uint32_t)pNv->AGPScratch->offset);
		NVDmaNext (pNv, (uint32_t)dst_offset);
		NVDmaNext (pNv, line_len);
		NVDmaNext (pNv, dst_pitch);
		NVDmaNext (pNv, line_len);
		NVDmaNext (pNv, lc);
		NVDmaNext (pNv, (1<<8)|1);
		NVDmaNext (pNv, 0);

		NVDmaKickoff(pNv);
		if (!NVNotifierWaitStatus(pScrn, pNv->Notifier0, 0, 0))
			return FALSE;

		line_count -= lc;
	}

	return TRUE;
}

static Bool NVUploadToScreen(PixmapPtr pDst,
			     int x, int y, int w, int h,
			     char *src, int src_pitch)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	int dst_offset, dst_pitch, bpp;
	Bool ret;

	dst_offset = NVAccelGetPixmapOffset(pDst);
	dst_pitch  = exaGetPixmapPitch(pDst);
	bpp = pDst->drawable.bitsPerPixel >> 3;

	if (1) {
		dst_offset += (y * dst_pitch) + (x * bpp);
		ret = NVAccelUploadM2MF(pScrn, dst_offset, src,
					       dst_pitch, src_pitch,
					       w * bpp, h);
	}
	exaMarkSync(pDst->drawable.pScreen);
	return ret;
}


static Bool NVCheckComposite(int	op,
			     PicturePtr pSrcPicture,
			     PicturePtr pMaskPicture,
			     PicturePtr pDstPicture)
{
	CARD32 ret = 0;

	/* PictOpOver doesn't work correctly. The HW command assumes
	 * non premuliplied alpha
	 */
	if (pMaskPicture)
		ret = 0x1;
	else if (op != PictOpOver && op != PictOpSrc)
		ret = 0x2;
	else if (!pSrcPicture->pDrawable)
		ret = 0x4;
	else if (pSrcPicture->transform || pSrcPicture->repeat)
		ret = 0x8;
	else if (pSrcPicture->alphaMap || pDstPicture->alphaMap)
		ret = 0x10;
	else if (pSrcPicture->format != PICT_a8r8g8b8 &&
			pSrcPicture->format != PICT_x8r8g8b8 &&
			pSrcPicture->format != PICT_r5g6b5)
		ret = 0x20;
	else if (pDstPicture->format != PICT_a8r8g8b8 &&
			pDstPicture->format != PICT_x8r8g8b8 &&
			pDstPicture->format != PICT_r5g6b5)
		ret = 0x40;

	return ret == 0;
}

static CARD32 src_size, src_pitch, src_offset;

static Bool NVPrepareComposite(int	  op,
			       PicturePtr pSrcPicture,
			       PicturePtr pMaskPicture,
			       PicturePtr pDstPicture,
			       PixmapPtr  pSrc,
			       PixmapPtr  pMask,
			       PixmapPtr  pDst)
{
	ScrnInfoPtr pScrn = xf86Screens[pSrcPicture->pDrawable->pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	int srcFormat, dstFormat;

	if (pSrcPicture->format == PICT_a8r8g8b8)
		srcFormat = STRETCH_BLIT_FORMAT_A8R8G8B8;
	else if (pSrcPicture->format == PICT_x8r8g8b8)
		srcFormat = STRETCH_BLIT_FORMAT_X8R8G8B8;
	else if (pSrcPicture->format == PICT_r5g6b5)
		srcFormat = STRETCH_BLIT_FORMAT_DEPTH16;
	else
		return FALSE;

	if (!NVAccelGetCtxSurf2DFormatFromPicture(pDstPicture, &dstFormat))
		return FALSE;
	if (!NVAccelSetCtxSurf2D(pDst, pDst, dstFormat))
		return FALSE;

	NVDmaStart(pNv, NvSubScaledImage, STRETCH_BLIT_FORMAT, 2);
	NVDmaNext (pNv, srcFormat);
	NVDmaNext (pNv, (op == PictOpSrc) ? STRETCH_BLIT_OPERATION_COPY :
			STRETCH_BLIT_OPERATION_BLEND);

	src_size = ((pSrcPicture->pDrawable->width+3)&~3) |
		(pSrcPicture->pDrawable->height << 16);
	src_pitch  = exaGetPixmapPitch(pSrc)
		| (STRETCH_BLIT_SRC_FORMAT_ORIGIN_CORNER << 16)
		| (STRETCH_BLIT_SRC_FORMAT_FILTER_POINT_SAMPLE << 24);
	src_offset = NVAccelGetPixmapOffset(pSrc);

	return TRUE;
}

static void NVComposite(PixmapPtr pDst,
			int	  srcX,
			int	  srcY,
			int	  maskX,
			int	  maskY,
			int	  dstX,
			int	  dstY,
			int	  width,
			int	  height)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);

	NVDmaStart(pNv, NvSubScaledImage, STRETCH_BLIT_CLIP_POINT, 6);
	NVDmaNext (pNv, dstX | (dstY << 16));
	NVDmaNext (pNv, width | (height << 16));
	NVDmaNext (pNv, dstX | (dstY << 16));
	NVDmaNext (pNv, width | (height << 16));
	NVDmaNext (pNv, 1<<20);
	NVDmaNext (pNv, 1<<20);

	NVDmaStart(pNv, NvSubScaledImage, STRETCH_BLIT_SRC_SIZE, 4);
	NVDmaNext (pNv, src_size);
	NVDmaNext (pNv, src_pitch);
	NVDmaNext (pNv, src_offset);
	NVDmaNext (pNv, srcX | (srcY<<16));

	NVDmaKickoff(pNv);
}

static void NVDoneComposite (PixmapPtr pDst)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	CARD32 format;

	if (pNv->CurrentLayout.depth == 8)
		format = SURFACE_FORMAT_Y8;
	else if (pNv->CurrentLayout.depth == 16)
		format = SURFACE_FORMAT_R5G6B5;
	else
		format = SURFACE_FORMAT_X8R8G8B8;

	NVDmaStart(pNv, NvSubContextSurfaces, SURFACE_FORMAT, 1);
	NVDmaNext (pNv, format);

	exaMarkSync(pDst->drawable.pScreen);
}

Bool NVExaInit(ScreenPtr pScreen) 
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);

	if(!(pNv->EXADriverPtr = (ExaDriverPtr) xnfcalloc(sizeof(ExaDriverRec), 1))) {
		pNv->NoAccel = TRUE;
		return FALSE;
	}

	pNv->EXADriverPtr->exa_major = EXA_VERSION_MAJOR;
	pNv->EXADriverPtr->exa_minor = EXA_VERSION_MINOR;

	pNv->EXADriverPtr->memoryBase		= pNv->FB->map;
	pNv->EXADriverPtr->offScreenBase	=
		pScrn->virtualX * pScrn->virtualY*(pScrn->bitsPerPixel/8); 
	pNv->EXADriverPtr->memorySize		= pNv->FB->size; 
	pNv->EXADriverPtr->pixmapOffsetAlign	= 256; 
	pNv->EXADriverPtr->pixmapPitchAlign	= 64; 
	pNv->EXADriverPtr->flags		= EXA_OFFSCREEN_PIXMAPS;
	pNv->EXADriverPtr->maxX			= 32768;
	pNv->EXADriverPtr->maxY			= 32768;

	pNv->EXADriverPtr->WaitMarker = NVExaWaitMarker;

	/* Install default hooks */
	if (pNv->AGPScratch) {
		pNv->EXADriverPtr->DownloadFromScreen = NVDownloadFromScreen; 
		pNv->EXADriverPtr->UploadToScreen = NVUploadToScreen; 
	}

	pNv->EXADriverPtr->PrepareCopy = NVExaPrepareCopy;
	pNv->EXADriverPtr->Copy = NVExaCopy;
	pNv->EXADriverPtr->DoneCopy = NVExaDoneCopy;

	pNv->EXADriverPtr->PrepareSolid = NVExaPrepareSolid;
	pNv->EXADriverPtr->Solid = NVExaSolid;
	pNv->EXADriverPtr->DoneSolid = NVExaDoneSolid;

	switch (pNv->Architecture) {
	case NV_ARCH_40:
		pNv->EXADriverPtr->CheckComposite   = NV30EXACheckComposite;
		pNv->EXADriverPtr->PrepareComposite = NV30EXAPrepareComposite;
		pNv->EXADriverPtr->Composite        = NV30EXAComposite;
		pNv->EXADriverPtr->DoneComposite    = NV30EXADoneComposite;
		break;
	default:
		if (!pNv->BlendingPossible)
			break;
		pNv->EXADriverPtr->CheckComposite   = NVCheckComposite;
		pNv->EXADriverPtr->PrepareComposite = NVPrepareComposite;
		pNv->EXADriverPtr->Composite        = NVComposite;
		pNv->EXADriverPtr->DoneComposite    = NVDoneComposite;
		break;
	}

	/* If we're going to try and use 3D, let the card-specific function
	 * override whatever hooks it wants.
	 */
	if (pNv->use3D) pNv->InitEXA3D(pNv);

	return exaDriverInit(pScreen, pNv->EXADriverPtr);
}

