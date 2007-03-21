#include "nv_include.h"

static Bool
NVAccelInitNullObject(NVPtr pNv)
{
	static int have_object = FALSE;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvNullObject,
						   0x30))
			return FALSE;
		have_object = TRUE;
	}

	return TRUE;
}

static Bool
NVAccelInitDmaFB(NVPtr pNv)
{
	static int have_object = FALSE;

	if (!have_object) {
		if (!NVDmaCreateDMAObject(pNv, NvDmaFB, NV_DMA_IN_MEMORY,
					  NOUVEAU_MEM_FB, 0,
					  pNv->VRAMSize,
					  NOUVEAU_MEM_ACCESS_RW))
				return FALSE;
		have_object = TRUE;
	}

	return TRUE;
}

uint32_t
NVAccelGetPixmapOffset(NVPtr pNv, PixmapPtr pPix)
{
	CARD32 offset;

	if (pPix->drawable.type == DRAWABLE_WINDOW) {
		offset = pNv->FB->offset - pNv->VRAMPhysical;
	} else {
		offset  = (uint32_t)((unsigned long)pPix->devPrivate.ptr -
				(unsigned long)pNv->FB->map);
		offset += pNv->FB->offset - pNv->VRAMPhysical;
	}

	return offset;
}

static Bool
NVAccelInitDmaAGP(NVPtr pNv)
{
	static int have_object = FALSE;

	if (!pNv->AGPSize)
		return TRUE;

	if (!have_object) {
		if (!NVDmaCreateDMAObject(pNv, NvDmaAGP, NV_DMA_IN_MEMORY,
					  NOUVEAU_MEM_AGP, 0,
					  pNv->AGPSize,
					  NOUVEAU_MEM_ACCESS_RW)) {
			ErrorF("Couldn't create AGP object, disabling AGP\n");
			NVFreeMemory(pNv, pNv->AGPScratch);
			pNv->AGPScratch = NULL;
		}
		have_object = TRUE;
	}

	return TRUE;
}

static Bool
NVAccelInitDmaNotifier0(NVPtr pNv)
{
	static int have_object = FALSE;

	if (!have_object) {
		pNv->Notifier0 = NVDmaCreateNotifier(pNv, NvDmaNotifier0);
		if (!pNv->Notifier0)
			return FALSE;
		have_object = TRUE;
	}

	return TRUE;
}

/* FLAGS_ROP_AND, DmaFB, DmaFB, 0 */
static Bool
NVAccelInitContextSurfaces(NVPtr pNv)
{
	static int have_object = FALSE;
	uint32_t   class;

	class = (pNv->Architecture >= NV_10) ? NV10_CONTEXT_SURFACES_2D :
					       NV04_SURFACE;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvContextSurfaces, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubContextSurfaces,
					NvContextSurfaces);
	NVDmaStart(pNv, NvSubContextSurfaces, NV04_SURFACE_DMA_NOTIFY, 1);
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubContextSurfaces, NV04_SURFACE_DMA_IMAGE_SOURCE, 2);
	NVDmaNext (pNv, NvDmaFB);
	NVDmaNext (pNv, NvDmaFB);

	return TRUE;
}

Bool
NVAccelGetCtxSurf2DFormatFromPixmap(PixmapPtr pPix, int *fmt_ret)
{
	switch (pPix->drawable.bitsPerPixel) {
	case 32:
		*fmt_ret = SURFACE_FORMAT_A8R8G8B8;
		break;
	case 24:
		*fmt_ret = SURFACE_FORMAT_X8R8G8B8;
		break;
	case 16:
		*fmt_ret = SURFACE_FORMAT_R5G6B5;
		break;
	case 8:
		*fmt_ret = SURFACE_FORMAT_Y8;
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

Bool
NVAccelGetCtxSurf2DFormatFromPicture(PicturePtr pPict, int *fmt_ret)
{
	switch (pPict->format) {
	case PICT_a8r8g8b8:
		*fmt_ret = SURFACE_FORMAT_A8R8G8B8;
		break;
	case PICT_x8r8g8b8:
		*fmt_ret = SURFACE_FORMAT_X8R8G8B8;
		break;
	case PICT_r5g6b5:
		*fmt_ret = SURFACE_FORMAT_R5G6B5;
		break;
	case PICT_a8:
		*fmt_ret = SURFACE_FORMAT_Y8;
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

Bool
NVAccelSetCtxSurf2D(NVPtr pNv, PixmapPtr psPix, PixmapPtr pdPix, int format)
{
	NVDmaStart(pNv, NvSubContextSurfaces, SURFACE_FORMAT, 4);
	NVDmaNext (pNv, format);
	NVDmaNext (pNv, ((uint32_t)exaGetPixmapPitch(pdPix) << 16) |
			 (uint32_t)exaGetPixmapPitch(psPix));
	NVDmaNext (pNv, NVAccelGetPixmapOffset(pNv, psPix));
	NVDmaNext (pNv, NVAccelGetPixmapOffset(pNv, pdPix));

	return TRUE;
}

/* FLAGS_ROP_AND|FLAGS_MONO, 0, 0, 0 */
static Bool
NVAccelInitImagePattern(NVPtr pNv)
{
	static int have_object = FALSE;
	uint32_t   class;

	class = NV04_IMAGE_PATTERN;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvImagePattern, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubImagePattern,
					NvImagePattern);
	NVDmaStart(pNv, NvSubImagePattern,
			0x180, /*NV04_IMAGE_PATTERN_SET_DMA_NOTIFY*/ 1);
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubImagePattern, NV04_IMAGE_PATTERN_MONO_FORMAT, 3);
#if X_BYTE_ORDER == X_BIG_ENDIAN
	NVDmaNext (pNv, 2 /* NV04_IMAGE_PATTERN_BIGENDIAN/LE_M1 */);
#else
	NVDmaNext (pNv, 1 /* NV04_IMAGE_PATTERN_LOWENDIAN/CGA6_M1 */);
#endif
	NVDmaNext (pNv, 0 /* NV04_IMAGE_PATTERN_MONOCHROME_SHAPE_8X8 */);
	NVDmaNext (pNv, 1 /* NV04_IMAGE_PATTERN_SELECT_MONOCHROME */);

	return TRUE;
}

/* FLAGS_ROP_AND, 0, 0, 0 */
static Bool
NVAccelInitRasterOp(NVPtr pNv)
{
	static int have_object = FALSE;
	uint32_t   class;

	class = NV03_PRIMITIVE_RASTER_OP;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvRop, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubRop, NvRop);
	NVDmaStart(pNv, NvSubRop, NV03_PRIMITIVE_RASTER_OP_DMA_NOTIFY, 1);
	NVDmaNext (pNv, NvNullObject);

	return TRUE;
}

/* FLAGS_ROP_AND | FLAGS_MONO, 0, 0, 0 */
static Bool
NVAccelInitRectangle(NVPtr pNv)
{
	static int have_object = FALSE;
	uint32_t   class;

	class = NV04_GDI_RECTANGLE_TEXT;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvRectangle, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubRectangle, NvRectangle);
	NVDmaStart(pNv, NvSubRectangle,
			NV04_GDI_RECTANGLE_TEXT_SET_DMA_NOTIFY, 1);
	NVDmaNext (pNv, NvDmaNotifier0);
	NVDmaStart(pNv, NvSubRectangle,
			0x184 /*NV04_GDI_RECTANGLE_TEXT_SET_DMA_FONTS*/, 1);
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubRectangle, NV04_GDI_RECTANGLE_TEXT_SURFACE, 1);
	NVDmaNext (pNv, NvContextSurfaces);
	NVDmaStart(pNv, NvSubRectangle, NV04_GDI_RECTANGLE_TEXT_ROP5, 1);
	NVDmaNext (pNv, NvRop);
	NVDmaStart(pNv, NvSubRectangle, NV04_GDI_RECTANGLE_TEXT_PATTERN, 1);
	NVDmaNext (pNv, NvImagePattern);
	NVDmaStart(pNv, NvSubRectangle, NV04_GDI_RECTANGLE_TEXT_OPERATION, 1);
	NVDmaNext (pNv, 1 /* ROP_AND */);

	return TRUE;
}

/* FLAGS_ROP_AND, DmaFB, DmaFB, 0 */
static Bool
NVAccelInitImageBlit(NVPtr pNv)
{
	static int have_object = FALSE;
	uint32_t   class;

	class = (pNv->WaitVSyncPossible) ? NV10_IMAGE_BLIT : NV_IMAGE_BLIT;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvImageBlit, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubImageBlit,	NvImageBlit);
	NVDmaStart(pNv, NvSubImageBlit, NV_IMAGE_BLIT_DMA_NOTIFY, 1);
	NVDmaNext (pNv, NvDmaNotifier0);
	NVDmaStart(pNv, NvSubImageBlit, NV_IMAGE_BLIT_COLOR_KEY, 1);
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubImageBlit, NV_IMAGE_BLIT_SURFACE, 1);
	NVDmaNext (pNv, NvContextSurfaces);
	NVDmaStart(pNv, NvSubImageBlit, NV_IMAGE_BLIT_CLIP_RECTANGLE, 3);
	NVDmaNext (pNv, NvNullObject);
	NVDmaNext (pNv, NvImagePattern);
	NVDmaNext (pNv, NvRop);
	NVDmaStart(pNv, NvSubImageBlit, NV_IMAGE_BLIT_OPERATION, 1);
	NVDmaNext (pNv, 1 /* NV_IMAGE_BLIT_OPERATION_ROP_AND */);

	return TRUE;
}

/* FLAGS_SRCCOPY, DmaFB, DmaFB, 0 */
static Bool
NVAccelInitScaledImage(NVPtr pNv)
{
	static int have_object = FALSE;
	uint32_t   class;

	switch (pNv->Architecture) {
	case NV_ARCH_04:
		class = NV04_SCALED_IMAGE_FROM_MEMORY;
		break;
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
		class = NV10_SCALED_IMAGE_FROM_MEMORY;
		break;
	case NV_ARCH_40:
	default:
		class = NV10_SCALED_IMAGE_FROM_MEMORY | 0x3000;
		break;
	}

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvScaledImage, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubScaledImage, NvScaledImage);
	NVDmaStart(pNv, NvSubScaledImage,
			NV04_SCALED_IMAGE_FROM_MEMORY_DMA_NOTIFY, 1);
	NVDmaNext (pNv, NvDmaNotifier0);
	NVDmaStart(pNv, NvSubScaledImage,
			NV04_SCALED_IMAGE_FROM_MEMORY_DMA_IMAGE, 1);
	NVDmaNext (pNv, NvDmaFB); /* source object */
	NVDmaStart(pNv, NvSubScaledImage,
			NV04_SCALED_IMAGE_FROM_MEMORY_SURFACE, 1);
	NVDmaNext (pNv, NvContextSurfaces);
	NVDmaStart(pNv, NvSubScaledImage, 0x188, 1); /* PATTERN */
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubScaledImage, 0x18c, 1); /* ROP */
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubScaledImage, 0x190, 1); /* BETA1 */
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubScaledImage, 0x194, 1); /* BETA4 */
	NVDmaNext (pNv, NvNullObject);
	NVDmaStart(pNv, NvSubScaledImage,
			NV04_SCALED_IMAGE_FROM_MEMORY_OPERATION, 1);
	NVDmaNext (pNv, 3 /* SRCCOPY */);

	return TRUE;
}

/* FLAGS_NONE, 0, 0, 0 */
static Bool
NVAccelInitClipRectangle(NVPtr pNv)
{
	static int have_object = FALSE;
	int class = NV01_CONTEXT_CLIP_RECTANGLE;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvClipRectangle, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubClipRectangle, NvClipRectangle);
	NVDmaStart(pNv, NvSubClipRectangle, 0x180, 1); /* DMA_NOTIFY */
	NVDmaNext (pNv, NvNullObject);

	return TRUE;
}

/* FLAGS_ROP_AND | FLAGS_CLIP_ENABLE, 0, 0, 0 */
static Bool
NVAccelInitSolidLine(NVPtr pNv)
{
	static int have_object = FALSE;
	int class = NV04_SOLID_LINE;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvSolidLine, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubSolidLine, NvSolidLine);
	NVDmaStart(pNv, NvSubSolidLine, NV04_SOLID_LINE_CLIP_RECTANGLE, 3);
	NVDmaNext (pNv, NvClipRectangle);
	NVDmaNext (pNv, NvImagePattern);
	NVDmaNext (pNv, NvRop);
	NVDmaStart(pNv, NvSubSolidLine, NV04_SOLID_LINE_SURFACE, 1);
	NVDmaNext (pNv, NvContextSurfaces);
	NVDmaStart(pNv, NvSubSolidLine, NV04_SOLID_LINE_OPERATION, 1);
	NVDmaNext (pNv, 1); /* ROP_AND */

	return TRUE;
}

/* FLAGS_NONE, NvDmaFB, NvDmaAGP, NvDmaNotifier0 */
static Bool
NVAccelInitMemFormat(NVPtr pNv)
{
	static int have_object = FALSE;
	uint32_t   class;

	class = NV_MEMORY_TO_MEMORY_FORMAT;

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, NvMemFormat, class))
				return FALSE;
		have_object = TRUE;
	}

	NVDmaSetObjectOnSubchannel(pNv, NvSubMemFormat, NvMemFormat);
	NVDmaStart(pNv, NvSubMemFormat,
			NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY, 1);
	NVDmaNext (pNv, NvDmaNotifier0);
	NVDmaStart(pNv, NvSubMemFormat,
			NV_MEMORY_TO_MEMORY_FORMAT_OBJECT_IN, 2);
	NVDmaNext (pNv, NvDmaFB);
	NVDmaNext (pNv, NvDmaFB);

	pNv->M2MFDirection = -1;
	return TRUE;
}

#define INIT_CONTEXT_OBJECT(name) do {                                        \
	ret = NVAccelInit##name(pNv);                                         \
	if (!ret) {                                                           \
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,                         \
			   "Failed to initialise context object: "#name       \
			   " (%d)\n", ret);                                   \
		return FALSE;                                                 \
	}                                                                     \
} while(0)

Bool
NVAccelCommonInit(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	Bool ret;
	if(pNv->NoAccel) return TRUE;

	INIT_CONTEXT_OBJECT(NullObject);
	INIT_CONTEXT_OBJECT(DmaFB);
	INIT_CONTEXT_OBJECT(DmaAGP);
	INIT_CONTEXT_OBJECT(DmaNotifier0);

	INIT_CONTEXT_OBJECT(ContextSurfaces);
	INIT_CONTEXT_OBJECT(ImagePattern);
	INIT_CONTEXT_OBJECT(RasterOp);
	INIT_CONTEXT_OBJECT(Rectangle);
	INIT_CONTEXT_OBJECT(ImageBlit);
	INIT_CONTEXT_OBJECT(ScaledImage);

	/* XAA-only */
	INIT_CONTEXT_OBJECT(ClipRectangle);
	INIT_CONTEXT_OBJECT(SolidLine);

	/* EXA-only */
	INIT_CONTEXT_OBJECT(MemFormat);

	return TRUE;
}

