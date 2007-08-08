#include "nv_include.h"

#define NV50_2D_NOP                                                   0x00000100
#define NV50_2D_UNK110                                                0x00000110
#define NV50_2D_DST_FORMAT                                            0x00000200
#define NV50_2D_SRC_FORMAT                                            0x00000230
#define NV50_2D_SRC_FORMAT_8BPP                                       0x000000f3
#define NV50_2D_SRC_FORMAT_15BPP                                      0x000000f8
#define NV50_2D_SRC_FORMAT_16BPP                                      0x000000e8
#define NV50_2D_SRC_FORMAT_24BPP                                      0x000000e6
#define NV50_2D_SRC_FORMAT_32BPP                                      0x000000cf
#define NV50_2D_CLIP_X                                                0x00000280
#define NV50_2D_SET_OPERATION                                         0x000002ac
#define NV50_2D_SET_OPERATION_ROP_AND                                 0x00000001
#define NV50_2D_SET_OPERATION_SRCCOPY                                 0x00000003
#define NV50_2D_RASTER_OP                                             0x000002a0
#define NV50_2D_PATTERN_FORMAT                                        0x000002e8
#define NV50_2D_PATTERN_COLOR_0                                       0x000002f0
#define NV50_2D_RECT_UNK580                                           0x00000580
#define NV50_2D_RECT_FORMAT                                           0x00000584
#define NV50_2D_RECT_X1                                               0x00000600
#define NV50_2D_BLIT_DST_X                                            0x000008b0

#define NV50EXA_LOCALS(p)                                              \
	ScrnInfoPtr pScrn = xf86Screens[(p)->drawable.pScreen->myNum]; \
	NVPtr pNv = NVPTR(pScrn);                                      \
	(void)pNv

static Bool
NV50EXA2DSurfaceFormat(PixmapPtr pPix, uint32_t *fmt)
{
	NV50EXA_LOCALS(pPix);

	switch (pPix->drawable.depth) {
	case 8 : *fmt = NV50_2D_SRC_FORMAT_8BPP; break;
	case 15: *fmt = NV50_2D_SRC_FORMAT_15BPP; break;
	case 16: *fmt = NV50_2D_SRC_FORMAT_16BPP; break;
	case 24: *fmt = NV50_2D_SRC_FORMAT_24BPP; break;
	case 32: *fmt = NV50_2D_SRC_FORMAT_32BPP; break;
	default:
		 xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			    "Unknown surface format for bpp=%d\n",
			    pPix->drawable.depth);
		 return FALSE;
	}

	return TRUE;
}

static Bool
NV50EXAAcquireSurface2D(PixmapPtr pPix, int is_src)
{
	NV50EXA_LOCALS(pPix);
	int mthd = is_src ? NV50_2D_SRC_FORMAT : NV50_2D_DST_FORMAT;
	uint32_t fmt;

	if (!NV50EXA2DSurfaceFormat(pPix, &fmt))
		return FALSE;

	NVDmaStart(pNv, Nv2D, mthd, 2);
	NVDmaNext (pNv, fmt);
	NVDmaNext (pNv, 1);

	NVDmaStart(pNv, Nv2D, mthd + 0x14, 5);
	NVDmaNext (pNv, (uint32_t)exaGetPixmapPitch(pPix));
	NVDmaNext (pNv, pPix->drawable.width);
	NVDmaNext (pNv, pPix->drawable.height);
	NVDmaNext (pNv, 0);
	NVDmaNext (pNv, NVAccelGetPixmapOffset(pPix));

	if (is_src == 0) {
		NVDmaStart(pNv, Nv2D, NV50_2D_CLIP_X, 4);
		NVDmaNext (pNv, 0);
		NVDmaNext (pNv, 0);
		NVDmaNext (pNv, pPix->drawable.width);
		NVDmaNext (pNv, pPix->drawable.height);
	}

	return TRUE;
}

static Bool
NV50EXAAcquireSurfaces(PixmapPtr pdPix)
{
	NV50EXA_LOCALS(pdPix);

	return TRUE;
}

static void
NV50EXAReleaseSurfaces(PixmapPtr pdPix)
{
	NV50EXA_LOCALS(pdPix);

	NVDmaStart(pNv, Nv2D, NV50_2D_NOP, 1);
	NVDmaNext (pNv, 0);
	NVDmaKickoff(pNv);
}

static void
NV50EXASetPattern(PixmapPtr pdPix, int col0, int col1, int pat0, int pat1)
{
	NV50EXA_LOCALS(pdPix);

	NVDmaStart(pNv, Nv2D, NV50_2D_PATTERN_COLOR_0, 4);
	NVDmaNext (pNv, col0);
	NVDmaNext (pNv, col1);
	NVDmaNext (pNv, pat0);
	NVDmaNext (pNv, pat1);
}

extern const int NVCopyROP[16];
static void
NV50EXASetROP(PixmapPtr pdPix, int alu, Pixel planemask)
{
	NV50EXA_LOCALS(pdPix);
	int rop = NVCopyROP[alu];

	NVDmaStart(pNv, Nv2D, NV50_2D_SET_OPERATION, 1);
	if(alu == GXcopy && planemask == ~0) {
		NVDmaNext (pNv, NV50_2D_SET_OPERATION_SRCCOPY);
		return;
	} else {
		NVDmaNext (pNv, NV50_2D_SET_OPERATION_ROP_AND);
	}

	NVDmaStart(pNv, Nv2D, NV50_2D_PATTERN_FORMAT, 1);
	switch (pdPix->drawable.depth) {
		case  8: NVDmaNext(pNv, 3); break;
		case 15: NVDmaNext(pNv, 1); break;
		case 16: NVDmaNext(pNv, 0); break;
		case 24:
		case 32:
		default:
			 NVDmaNext(pNv, 2);
			 break;
	}

	if(planemask != ~0) {
		NV50EXASetPattern(pdPix, 0, planemask, ~0, ~0);
		rop = (rop & 0xf0) | 0x0a;
	} else
	if((pNv->currentRop & 0x0f) == 0x0a) {
		NV50EXASetPattern(pdPix, ~0, ~0, ~0, ~0);
	}

	if (pNv->currentRop != rop) {
		NVDmaStart(pNv, Nv2D, NV50_2D_RASTER_OP, 1);
		NVDmaNext (pNv, rop);
		pNv->currentRop = rop;
	}
}

Bool
NV50EXAPrepareSolid(PixmapPtr pdPix, int alu, Pixel planemask, Pixel fg)
{
	NV50EXA_LOCALS(pdPix);
	uint32_t fmt;

	if(pdPix->drawable.depth > 24)
		return FALSE;
	if (!NV50EXA2DSurfaceFormat(pdPix, &fmt))
		return FALSE;

	if (!NV50EXAAcquireSurface2D(pdPix, 0))
		return FALSE;
	if (!NV50EXAAcquireSurfaces(pdPix))
		return FALSE;
	NV50EXASetROP(pdPix, alu, planemask);

	NVDmaStart(pNv, Nv2D, NV50_2D_RECT_UNK580, 3);
	NVDmaNext (pNv, 4);
	NVDmaNext (pNv, fmt);
	NVDmaNext (pNv, fg);

	return TRUE;
}

void
NV50EXASolid(PixmapPtr pdPix, int x1, int y1, int x2, int y2)
{
	NV50EXA_LOCALS(pdPix);

	NVDmaStart(pNv, Nv2D, NV50_2D_RECT_X1, 4);
	NVDmaNext (pNv, x1);
	NVDmaNext (pNv, y1);
	NVDmaNext (pNv, x2);
	NVDmaNext (pNv, y2);

	if((x2 - x1) * (y2 - y1) >= 512)
		NVDmaKickoff(pNv);
}

void
NV50EXADoneSolid(PixmapPtr pdPix)
{
	NV50EXA_LOCALS(pdPix);

	NV50EXAReleaseSurfaces(pdPix);
}

Bool
NV50EXAPrepareCopy(PixmapPtr psPix, PixmapPtr pdPix, int dx, int dy,
		   int alu, Pixel planemask)
{
	NV50EXA_LOCALS(pdPix);

	if (!NV50EXAAcquireSurface2D(psPix, 1))
		return FALSE;
	if (!NV50EXAAcquireSurface2D(pdPix, 0))
		return FALSE;
	if (!NV50EXAAcquireSurfaces(pdPix))
		return FALSE;
	NV50EXASetROP(pdPix, alu, planemask);

	return TRUE;
}

void
NV50EXACopy(PixmapPtr pdPix, int srcX , int srcY,
			     int dstX , int dstY,
			     int width, int height)
{
	NV50EXA_LOCALS(pdPix);

	NVDmaStart(pNv, Nv2D, NV50_2D_UNK110, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, Nv2D, NV50_2D_BLIT_DST_X, 12);
	NVDmaNext (pNv, dstX);
	NVDmaNext (pNv, dstY);
	NVDmaNext (pNv, width);
	NVDmaNext (pNv, height);
	NVDmaNext (pNv, 0);
	NVDmaNext (pNv, 1);
	NVDmaNext (pNv, 0);
	NVDmaNext (pNv, 1);
	NVDmaNext (pNv, 0);
	NVDmaNext (pNv, srcX);
	NVDmaNext (pNv, 0);
	NVDmaNext (pNv, srcY);

	if(width * height >= 512)
		NVDmaKickoff(pNv);
}

void
NV50EXADoneCopy(PixmapPtr pdPix)
{
	NV50EXA_LOCALS(pdPix);

	NV50EXAReleaseSurfaces(pdPix);
}


