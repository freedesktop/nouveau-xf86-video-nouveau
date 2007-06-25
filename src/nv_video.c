/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_video.c,v 1.23 2004/03/20 22:07:06 mvojkovi Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Resources.h"
#include "compiler.h"
#include "xf86PciInfo.h"
#include "xf86Pci.h"
#include "xf86fbman.h"
#include "regionstr.h"

#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#include "xaa.h"
#include "xaalocal.h"
#include "exa.h"
#include "damage.h"
#include "dixstruct.h"
#include "fourcc.h"

#include "nv_include.h"
#include "nv_dma.h"

#define OFF_DELAY 	500  /* milliseconds */
#define FREE_DELAY 	5000

#define OFF_TIMER 	0x01
#define FREE_TIMER	0x02
#define CLIENT_VIDEO_ON	0x04

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)

#define NUM_BLIT_PORTS 32

typedef struct _NVPortPrivRec {
	short		brightness;
	short		contrast;
	short		saturation;
	short		hue;
	RegionRec	clip;
	CARD32		colorKey;
	Bool		autopaintColorKey;
	Bool		doubleBuffer;
	CARD32		videoStatus;
	int		currentBuffer;
	Time		videoTime;
	Bool		grabbedByV4L;
	Bool		iturbt_709;
	Bool		blitter;
	Bool		SyncToVBlank;
	NVAllocRec *	video_mem;
	int		pitch;
	int		offset;
} NVPortPrivRec, *NVPortPrivPtr;

#define GET_OVERLAY_PRIVATE(pNv) \
	(NVPortPrivPtr)((pNv)->overlayAdaptor->pPortPrivates[0].ptr)

#define GET_BLIT_PRIVATE(pNv) \
	(NVPortPrivPtr)((pNv)->blitAdaptor->pPortPrivates[0].ptr)

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvContrast, xvColorKey, xvSaturation, 
            xvHue, xvAutopaintColorKey, xvSetDefaults, xvDoubleBuffer,
            xvITURBT709, xvSyncToVBlank;

/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding =
{ 
	0,
	"XV_IMAGE",
	2046, 2046,
	{1, 1}
};

#define NUM_FORMATS_ALL 6

XF86VideoFormatRec NVFormats[NUM_FORMATS_ALL] = 
{
	{15, TrueColor}, {16, TrueColor}, {24, TrueColor},
	{15, DirectColor}, {16, DirectColor}, {24, DirectColor}
};

#define NUM_OVERLAY_ATTRIBUTES 9
XF86AttributeRec NVOverlayAttributes[NUM_OVERLAY_ATTRIBUTES] =
{
	{XvSettable | XvGettable, 0, 1, "XV_DOUBLE_BUFFER"},
	{XvSettable | XvGettable, 0, (1 << 24) - 1, "XV_COLORKEY"},
	{XvSettable | XvGettable, 0, 1, "XV_AUTOPAINT_COLORKEY"},
	{XvSettable             , 0, 0, "XV_SET_DEFAULTS"},
	{XvSettable | XvGettable, -512, 511, "XV_BRIGHTNESS"},
	{XvSettable | XvGettable, 0, 8191, "XV_CONTRAST"},
	{XvSettable | XvGettable, 0, 8191, "XV_SATURATION"},
	{XvSettable | XvGettable, 0, 360, "XV_HUE"},
	{XvSettable | XvGettable, 0, 1, "XV_ITURBT_709"}
};

#define NUM_BLIT_ATTRIBUTES 2
XF86AttributeRec NVBlitAttributes[NUM_BLIT_ATTRIBUTES] =
{
	{XvSettable             , 0, 0, "XV_SET_DEFAULTS"},
	{XvSettable | XvGettable, 0, 1, "XV_SYNC_TO_VBLANK"}
};


#define NUM_IMAGES_YUV 4
#define NUM_IMAGES_ALL 5

#define FOURCC_RGB 0x0000003
#define XVIMAGE_RGB \
   { \
        FOURCC_RGB, \
        XvRGB, \
        LSBFirst, \
        { 0x03, 0x00, 0x00, 0x00, \
          0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
        32, \
        XvPacked, \
        1, \
        24, 0x00ff0000, 0x0000ff00, 0x000000ff, \
        0, 0, 0, \
        0, 0, 0, \
        0, 0, 0, \
        {'B','G','R','X',\
          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
        XvTopToBottom \
   }

static XF86ImageRec NVImages[NUM_IMAGES_ALL] =
{
	XVIMAGE_YUY2,
	XVIMAGE_YV12,
	XVIMAGE_UYVY,
	XVIMAGE_I420,
	XVIMAGE_RGB
};

static void 
NVSetPortDefaults (ScrnInfoPtr pScrn, NVPortPrivPtr pPriv)
{
	NVPtr pNv = NVPTR(pScrn);

	pPriv->brightness		= 0;
	pPriv->contrast			= 4096;
	pPriv->saturation		= 4096;
	pPriv->hue			= 0;
	pPriv->colorKey			= pNv->videoKey;
	pPriv->autopaintColorKey	= TRUE;
	pPriv->doubleBuffer		= TRUE;
	pPriv->iturbt_709		= FALSE;
}


void 
NVResetVideo (ScrnInfoPtr pScrn)
{
	NVPtr          pNv     = NVPTR(pScrn);
	NVPortPrivPtr  pPriv   = GET_OVERLAY_PRIVATE(pNv);
	int            satSine, satCosine;
	double         angle;

	angle = (double)pPriv->hue * 3.1415927 / 180.0;

	satSine = pPriv->saturation * sin(angle);
	if (satSine < -1024)
		satSine = -1024;
	satCosine = pPriv->saturation * cos(angle);
	if (satCosine < -1024)
		satCosine = -1024;

	nvWriteVIDEO(pNv, NV_PVIDEO_LUMINANCE(0), (pPriv->brightness << 16) |
						   pPriv->contrast);
	nvWriteVIDEO(pNv, NV_PVIDEO_LUMINANCE(1), (pPriv->brightness << 16) |
						   pPriv->contrast);
	nvWriteVIDEO(pNv, NV_PVIDEO_CHROMINANCE(0), (satSine << 16) |
						    (satCosine & 0xffff));
	nvWriteVIDEO(pNv, NV_PVIDEO_CHROMINANCE(1), (satSine << 16) |
						    (satCosine & 0xffff));
	nvWriteVIDEO(pNv, NV_PVIDEO_COLOR_KEY, pPriv->colorKey);
}



static void 
NVStopOverlay (ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	nvWriteVIDEO(pNv, NV_PVIDEO_STOP, 1);
}

static NVAllocRec *
NVAllocateOverlayMemory(ScrnInfoPtr pScrn, NVAllocRec *mem, int size)
{
	NVPtr pNv = NVPTR(pScrn);

	/* The code assumes the XAA fb manager is being used here,
	 * which allocates in pixels.  We allocate in bytes so we
	 * need to adjust the size here.
	 */
	size *= (pScrn->bitsPerPixel >> 3);

	if(mem) {
		if(mem->size >= size) 
			return mem;
		NVFreeMemory(pNv, mem);
	}

	return NVAllocateMemory(pNv, NOUVEAU_MEM_FB, size); /* align 32? */
}

static void
NVFreeOverlayMemory(ScrnInfoPtr pScrn)
{
	NVPtr         pNv   = NVPTR(pScrn);
	NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(pNv);

	if(pPriv->video_mem) {
		NVFreeMemory(pNv, pPriv->video_mem);
		pPriv->video_mem = NULL;
	}
}


static void
NVFreeBlitMemory(ScrnInfoPtr pScrn)
{
	NVPtr         pNv   = NVPTR(pScrn);
	NVPortPrivPtr pPriv = GET_BLIT_PRIVATE(pNv);

	if(pPriv->video_mem) {
		NVFreeMemory(pNv, pPriv->video_mem);
		pPriv->video_mem = NULL;
	}
}

static void
NVVideoTimerCallback(ScrnInfoPtr pScrn, Time currentTime)
{
	NVPtr         pNv = NVPTR(pScrn);
	NVPortPrivPtr pOverPriv = NULL;
	NVPortPrivPtr pBlitPriv = NULL;
	Bool needCallback = FALSE;

	if (!pScrn->vtSema)
		return; 

	if (pNv->overlayAdaptor) {
		pOverPriv = GET_OVERLAY_PRIVATE(pNv);
		if (!pOverPriv->videoStatus)
			pOverPriv = NULL;
	}

	if (pNv->blitAdaptor) {
		pBlitPriv = GET_BLIT_PRIVATE(pNv);
		if (!pBlitPriv->videoStatus)
			pBlitPriv = NULL;
	}

	if (pOverPriv) {
		if (pOverPriv->videoTime < currentTime) {
			if (pOverPriv->videoStatus & OFF_TIMER) {
				NVStopOverlay(pScrn);
				pOverPriv->videoStatus = FREE_TIMER;
				pOverPriv->videoTime = currentTime + FREE_DELAY;
				needCallback = TRUE;
			} else
			if (pOverPriv->videoStatus & FREE_TIMER) {
				NVFreeOverlayMemory(pScrn);
				pOverPriv->videoStatus = 0;
			}
		} else {
			needCallback = TRUE;
		}
	}

	if (pBlitPriv) {
		if (pBlitPriv->videoTime < currentTime) {
			NVFreeBlitMemory(pScrn);
			pBlitPriv->videoStatus = 0;              
		} else {
			needCallback = TRUE;
		}
	}

	pNv->VideoTimerCallback = needCallback ? NVVideoTimerCallback : NULL;
}

static void
NVPutOverlayImage(ScrnInfoPtr pScrn, int offset, int id,
		  int dstPitch, BoxPtr dstBox,
		  int x1, int y1, int x2, int y2,
		  short width, short height,
		  short src_w, short src_h,
		  short drw_w, short drw_h,
		  RegionPtr clipBoxes)
{
	NVPtr         pNv    = NVPTR(pScrn);
	NVPortPrivPtr pPriv  = GET_OVERLAY_PRIVATE(pNv);
	int           buffer = pPriv->currentBuffer;

	/* paint the color key */
	if(pPriv->autopaintColorKey && (pPriv->grabbedByV4L ||
		!REGION_EQUAL(pScrn->pScreen, &pPriv->clip, clipBoxes))) {
		/* we always paint V4L's color key */
		if (!pPriv->grabbedByV4L)
			REGION_COPY(pScrn->pScreen, &pPriv->clip, clipBoxes);
		xf86XVFillKeyHelper(pScrn->pScreen, pPriv->colorKey, clipBoxes);
	}

	if(pNv->CurrentLayout.mode->Flags & V_DBLSCAN) {
		dstBox->y1 <<= 1;
		dstBox->y2 <<= 1;
		drw_h <<= 1;
	}

	nvWriteVIDEO(pNv, NV_PVIDEO_BASE(buffer)     , offset);
	nvWriteVIDEO(pNv, NV_PVIDEO_SIZE_IN(buffer)  , (height << 16) | width);
	nvWriteVIDEO(pNv, NV_PVIDEO_POINT_IN(buffer) ,
			  ((y1 << 4) & 0xffff0000) | (x1 >> 12));
	nvWriteVIDEO(pNv, NV_PVIDEO_DS_DX(buffer)    , (src_w << 20) / drw_w);
	nvWriteVIDEO(pNv, NV_PVIDEO_DT_DY(buffer)    , (src_h << 20) / drw_h);
	nvWriteVIDEO(pNv, NV_PVIDEO_POINT_OUT(buffer),
			  (dstBox->y1 << 16) | dstBox->x1);
	nvWriteVIDEO(pNv, NV_PVIDEO_SIZE_OUT(buffer) ,
			  ((dstBox->y2 - dstBox->y1) << 16) |
			   (dstBox->x2 - dstBox->x1));

	dstPitch |= NV_PVIDEO_FORMAT_DISPLAY_COLOR_KEY;   /* use color key */
	if(id != FOURCC_UYVY)
		dstPitch |= NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8;
	if(pPriv->iturbt_709)
		dstPitch |= NV_PVIDEO_FORMAT_MATRIX_ITURBT709;

	nvWriteVIDEO(pNv, NV_PVIDEO_FORMAT(buffer), dstPitch);
	nvWriteVIDEO(pNv, NV_PVIDEO_STOP, 0);
	nvWriteVIDEO(pNv, NV_PVIDEO_BUFFER, buffer ? 0x10 : 0x1);

	pPriv->videoStatus = CLIENT_VIDEO_ON;
}

#ifndef ExaOffscreenMarkUsed
extern void ExaOffscreenMarkUsed(PixmapPtr);
#endif
#ifndef exaGetDrawablePixmap
extern PixmapPtr exaGetDrawablePixmap(DrawablePtr);
#endif
#ifndef exaPixmapIsOffscreen
extern Bool exaPixmapIsOffscreen(PixmapPtr p);
#endif
/* To support EXA 2.0, 2.1 has this in the header */
#ifndef exaMoveInPixmap
extern void exaMoveInPixmap(PixmapPtr pPixmap);
#endif

static void
NVPutBlitImage(ScrnInfoPtr pScrn, int src_offset, int id,
	       int src_pitch, BoxPtr dstBox,
	       int x1, int y1, int x2, int y2,
	       short width, short height,
	       short src_w, short src_h,
	       short drw_w, short drw_h,
	       RegionPtr clipBoxes,
	       DrawablePtr pDraw)
{
	NVPtr          pNv   = NVPTR(pScrn);
	NVPortPrivPtr  pPriv = GET_BLIT_PRIVATE(pNv);
	BoxPtr         pbox;
	int            nbox;
	CARD32         dsdx, dtdy;
	CARD32         dst_size, dst_point;
	CARD32         src_point, src_format;

	if (pNv->useEXA) {
		ScreenPtr pScreen = pScrn->pScreen;
		PixmapPtr pPix    = exaGetDrawablePixmap(pDraw);
		int dst_format;

		/* Try to get the dest drawable into vram */
		if (!exaPixmapIsOffscreen(pPix)) {
			exaMoveInPixmap(pPix);
			ExaOffscreenMarkUsed(pPix);
		}

		/* If we failed, draw directly onto the screen pixmap.
		 * Not sure if this is the best approach, maybe failing
		 * with BadAlloc would be better?
		 */
		if (!exaPixmapIsOffscreen(pPix)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"XV: couldn't move dst surface into vram\n");
			pPix = pScreen->GetScreenPixmap(pScreen);
		}

		NVAccelGetCtxSurf2DFormatFromPixmap(pPix, &dst_format);
		NVAccelSetCtxSurf2D(pPix, pPix, dst_format);

#ifdef COMPOSITE
		/* Adjust coordinates if drawing to an offscreen pixmap */
		if (pPix->screen_x || pPix->screen_y) {
			REGION_TRANSLATE(pScrn->pScreen, clipBoxes,
							     -pPix->screen_x,
							     -pPix->screen_y);
			dstBox->x1 -= pPix->screen_x;
			dstBox->x2 -= pPix->screen_x;
			dstBox->y1 -= pPix->screen_y;
			dstBox->y2 -= pPix->screen_y;
		}

		DamageDamageRegion((DrawablePtr)pPix, clipBoxes);
#endif
	} else {
		if (pNv->CurrentLayout.depth == 15) {
			NVDmaStart(pNv, NvSubContextSurfaces,
					SURFACE_FORMAT, 1);
			NVDmaNext (pNv, SURFACE_FORMAT_X1R5G5B5);
		}
	}

	pbox = REGION_RECTS(clipBoxes);
	nbox = REGION_NUM_RECTS(clipBoxes);

	dsdx = (src_w << 20) / drw_w;
	dtdy = (src_h << 20) / drw_h;

	dst_size  = ((dstBox->y2 - dstBox->y1) << 16) |
		     (dstBox->x2 - dstBox->x1);
	dst_point = (dstBox->y1 << 16) | dstBox->x1;

	src_pitch |= (STRETCH_BLIT_SRC_FORMAT_ORIGIN_CENTER << 16) |
		    (STRETCH_BLIT_SRC_FORMAT_FILTER_BILINEAR << 24);
	src_point = ((y1 << 4) & 0xffff0000) | (x1 >> 12);

	switch(id) {
	case FOURCC_RGB:
		src_format = STRETCH_BLIT_FORMAT_X8R8G8B8;
		break;
	case FOURCC_UYVY:
		src_format = STRETCH_BLIT_FORMAT_UYVY;
		break;
	default:
		src_format = STRETCH_BLIT_FORMAT_YUYV;
		break;
	}

	if(pPriv->SyncToVBlank) {
		NVDmaKickoff(pNv);
		NVWaitVSync(pScrn);
	}

	if(pNv->BlendingPossible) {
		NVDmaStart(pNv, NvSubScaledImage, STRETCH_BLIT_FORMAT, 2);
		NVDmaNext (pNv, src_format);
		NVDmaNext (pNv, STRETCH_BLIT_OPERATION_COPY);
	} else {
		NVDmaStart(pNv, NvSubScaledImage, STRETCH_BLIT_FORMAT, 1);
		NVDmaNext (pNv, src_format);
	}

	while(nbox--) {
		NVDmaStart(pNv, NvSubRectangle, RECT_SOLID_COLOR, 1);
		NVDmaNext (pNv, 0);

		NVDmaStart(pNv, NvSubScaledImage, STRETCH_BLIT_CLIP_POINT, 6);
		NVDmaNext (pNv, (pbox->y1 << 16) | pbox->x1); 
		NVDmaNext (pNv, ((pbox->y2 - pbox->y1) << 16) |
				 (pbox->x2 - pbox->x1));
		NVDmaNext (pNv, dst_point);
		NVDmaNext (pNv, dst_size);
		NVDmaNext (pNv, dsdx);
		NVDmaNext (pNv, dtdy);

		NVDmaStart(pNv, NvSubScaledImage, STRETCH_BLIT_SRC_SIZE, 4);
		NVDmaNext (pNv, (height << 16) | width);
		NVDmaNext (pNv, src_pitch);
		NVDmaNext (pNv, src_offset);
		NVDmaNext (pNv, src_point);
		pbox++;
	}

	if (!pNv->useEXA) {
		if(pNv->CurrentLayout.depth == 15) {
			NVDmaStart(pNv, NvSubContextSurfaces,
					SURFACE_FORMAT, 1);
			NVDmaNext (pNv, SURFACE_FORMAT_R5G6B5);
		}
	}

	NVDmaKickoff(pNv);

	if (pNv->useEXA)
		exaMarkSync(pScrn->pScreen);
	else
		SET_SYNC_FLAG(pNv->AccelInfoRec);

	pPriv->videoStatus = FREE_TIMER;
	pPriv->videoTime = currentTime.milliseconds + FREE_DELAY;
	pNv->VideoTimerCallback = NVVideoTimerCallback;
}

/*
 * StopVideo
 */
static void
NVStopOverlayVideo(ScrnInfoPtr pScrn, pointer data, Bool Exit)
{
	NVPtr         pNv   = NVPTR(pScrn);
	NVPortPrivPtr pPriv = (NVPortPrivPtr)data;

	if(pPriv->grabbedByV4L) return;

	REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

	if(Exit) {
		if (pPriv->videoStatus & CLIENT_VIDEO_ON)
			NVStopOverlay(pScrn);
		NVFreeOverlayMemory(pScrn);
		pPriv->videoStatus = 0;
	} else {
		if (pPriv->videoStatus & CLIENT_VIDEO_ON) {
			pPriv->videoStatus = OFF_TIMER | CLIENT_VIDEO_ON;
			pPriv->videoTime = currentTime.milliseconds + OFF_DELAY;
			pNv->VideoTimerCallback = NVVideoTimerCallback;
		}
	}
}

static void
NVStopBlitVideo(ScrnInfoPtr pScrn, pointer data, Bool Exit)
{
}

static int
NVSetOverlayPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
			  INT32 value, pointer data)
{
	NVPortPrivPtr pPriv = (NVPortPrivPtr)data;

	if (attribute == xvBrightness) {
		if ((value < -512) || (value > 512))
			return BadValue;
		pPriv->brightness = value;
	} else
	if (attribute == xvDoubleBuffer) {
		if ((value < 0) || (value > 1))
			return BadValue;
		pPriv->doubleBuffer = value;
	} else
	if (attribute == xvContrast) {
		if ((value < 0) || (value > 8191))
			return BadValue;
		pPriv->contrast = value;
	} else
	if (attribute == xvHue) {
		value %= 360;
		if (value < 0)
			value += 360;
		pPriv->hue = value;
	} else
	if (attribute == xvSaturation) {
		if ((value < 0) || (value > 8191))
			return BadValue;
		pPriv->saturation = value;
	} else
	if (attribute == xvColorKey) {
		pPriv->colorKey = value;
		REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
	} else
	if (attribute == xvAutopaintColorKey) {
		if ((value < 0) || (value > 1))
			return BadValue;
		pPriv->autopaintColorKey = value;
	} else
	if (attribute == xvITURBT709) {
		if ((value < 0) || (value > 1))
			return BadValue;
		pPriv->iturbt_709 = value;
	} else
	if (attribute == xvSetDefaults) {
		NVSetPortDefaults(pScrn, pPriv);
	} else
		return BadMatch;

	NVResetVideo(pScrn);
	return Success;
}


static int
NVGetOverlayPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
			  INT32 *value, pointer data)
{
	NVPortPrivPtr pPriv = (NVPortPrivPtr)data;

	if (attribute == xvBrightness)
		*value = pPriv->brightness;
	else if (attribute == xvDoubleBuffer)
		*value = (pPriv->doubleBuffer) ? 1 : 0;
	else if (attribute == xvContrast)
		*value = pPriv->contrast;
	else if (attribute == xvSaturation)
		*value = pPriv->saturation;
	else if (attribute == xvHue)
		*value = pPriv->hue;
	else if (attribute == xvColorKey)
		*value = pPriv->colorKey;
	else if (attribute == xvAutopaintColorKey)
		*value = (pPriv->autopaintColorKey) ? 1 : 0;
	else if (attribute == xvITURBT709)
		*value = (pPriv->iturbt_709) ? 1 : 0;
	else
		return BadMatch;

	return Success;
}

static int
NVSetBlitPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
		       INT32 value, pointer data)
{
	NVPortPrivPtr pPriv = (NVPortPrivPtr)data;
	NVPtr           pNv = NVPTR(pScrn);

	if ((attribute == xvSyncToVBlank) && pNv->WaitVSyncPossible) {
		if ((value < 0) || (value > 1))
			return BadValue;
		pPriv->SyncToVBlank = value;
	} else
	if (attribute == xvSetDefaults) {
		pPriv->SyncToVBlank = pNv->WaitVSyncPossible;
	} else
		return BadMatch;

	return Success;
}

static int
NVGetBlitPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
		       INT32 *value, pointer data)
{
	NVPortPrivPtr pPriv = (NVPortPrivPtr)data;

	if(attribute == xvSyncToVBlank)
		*value = (pPriv->SyncToVBlank) ? 1 : 0;
	else
		return BadMatch;

	return Success;
}


/*
 * QueryBestSize
 */
static void
NVQueryBestSize(ScrnInfoPtr pScrn, Bool motion,
		short vid_w, short vid_h, 
		short drw_w, short drw_h, 
		unsigned int *p_w, unsigned int *p_h, 
		pointer data)
{
	if(vid_w > (drw_w << 3))
		drw_w = vid_w >> 3;
	if(vid_h > (drw_h << 3))
		drw_h = vid_h >> 3;

	*p_w = drw_w;
	*p_h = drw_h; 
}

static void NVCopyData420(unsigned char *src1, unsigned char *src2,
			  unsigned char *src3, unsigned char *dst1,
			  int srcPitch, int srcPitch2,
			  int dstPitch,
			  int h, int w)
{
	CARD32 *dst;
	CARD8 *s1, *s2, *s3;
	int i, j;

	w >>= 1;

	for (j = 0; j < h; j++) {
		dst = (CARD32*)dst1;
		s1 = src1;  s2 = src2;  s3 = src3;
		i = w;

		while (i > 4) {
#if X_BYTE_ORDER == X_BIG_ENDIAN
		dst[0] = (s1[0] << 24) | (s1[1] << 8) | (s3[0] << 16) | s2[0];
		dst[1] = (s1[2] << 24) | (s1[3] << 8) | (s3[1] << 16) | s2[1];
		dst[2] = (s1[4] << 24) | (s1[5] << 8) | (s3[2] << 16) | s2[2];
		dst[3] = (s1[6] << 24) | (s1[7] << 8) | (s3[3] << 16) | s2[3];
#else
		dst[0] = s1[0] | (s1[1] << 16) | (s3[0] << 8) | (s2[0] << 24);
		dst[1] = s1[2] | (s1[3] << 16) | (s3[1] << 8) | (s2[1] << 24);
		dst[2] = s1[4] | (s1[5] << 16) | (s3[2] << 8) | (s2[2] << 24);
		dst[3] = s1[6] | (s1[7] << 16) | (s3[3] << 8) | (s2[3] << 24);
#endif
		dst += 4; s2 += 4; s3 += 4; s1 += 8;
		i -= 4;
		}

		while (i--) {
#if X_BYTE_ORDER == X_BIG_ENDIAN
		dst[0] = (s1[0] << 24) | (s1[1] << 8) | (s3[0] << 16) | s2[0];
#else
		dst[0] = s1[0] | (s1[1] << 16) | (s3[0] << 8) | (s2[0] << 24);
#endif
		dst++; s2++; s3++;
		s1 += 2;
		}

		dst1 += dstPitch;
		src1 += srcPitch;
		if (j & 1) {
			src2 += srcPitch2;
			src3 += srcPitch2;
		}
	}
}


static void
NVMoveDWORDS(CARD32* dest, CARD32* src, int dwords)
{
	while (dwords & ~0x03) {
		*dest = *src;
		*(dest + 1) = *(src + 1);
		*(dest + 2) = *(src + 2);
		*(dest + 3) = *(src + 3);
		src += 4;
		dest += 4;
		dwords -= 4;
	}

	if (!dwords)
		return;
	*dest = *src;

	if (dwords == 1)
		return;
	*(dest + 1) = *(src + 1);

	if (dwords == 2)
		return;
	*(dest + 2) = *(src + 2);
}

#if X_BYTE_ORDER == X_BIG_ENDIAN
static void
NVMoveDWORDSSwapped(CARD32* dest, CARD8* src, int dwords)
{
	while (dwords--) {
	*dest++ = (src[3] << 24) | (src[2] << 16) | (src[1] << 8) | src[0];
	src += 4;
	}
}
#endif

static void
NVCopyData422(unsigned char *src, unsigned char *dst,
	      int srcPitch, int dstPitch,
	      int h, int w)
{
	w >>= 1;  /* pixels to DWORDS */
	while (h--) {
		NVMoveDWORDS((CARD32*)dst, (CARD32*)src, w);
		src += srcPitch;
		dst += dstPitch;
	}
}

static void
NVCopyDataRGB(unsigned char *src, unsigned char *dst,
	      int srcPitch, int dstPitch,
	      int h, int w)
{
	while (h--) {
#if X_BYTE_ORDER == X_BIG_ENDIAN
		NVMoveDWORDSSwapped((CARD32*)dst, (CARD8*)src, w);
#else
		NVMoveDWORDS((CARD32*)dst, (CARD32*)src, w);
#endif
		src += srcPitch;
		dst += dstPitch;
	}
}


/*
 * PutImage
 */
static int
NVPutImage(ScrnInfoPtr  pScrn, short src_x, short src_y,
				   short drw_x, short drw_y,
				   short src_w, short src_h, 
				   short drw_w, short drw_h,
				   int id,
				   unsigned char *buf, 
				   short width, short height, 
				   Bool         Sync,
				   RegionPtr    clipBoxes,
				   pointer      data,
				   DrawablePtr  pDraw
)
{
	NVPortPrivPtr pPriv = (NVPortPrivPtr)data;
	NVPtr pNv = NVPTR(pScrn);
	INT32 xa, xb, ya, yb;
	unsigned char *dst_start;
	int newSize, offset, s2offset, s3offset;
	int srcPitch, srcPitch2, dstPitch;
	int top, left, right, bottom, npixels, nlines, bpp;
	Bool skip = FALSE;
	BoxRec dstBox;
	CARD32 tmp;

	/* s2offset, s3offset - byte offsets into U and V plane of the
	 *                      source where copying starts.  Y plane is
	 *                      done by editing "buf".
	 * offset - byte offset to the first line of the destination.
	 * dst_start - byte address to the first displayed pel.
	 */

	if (pPriv->grabbedByV4L)
		return Success;

	/* make the compiler happy */
	s2offset = s3offset = srcPitch2 = 0;

	if (!pPriv->blitter) {
		if (src_w > (drw_w << 3))
			drw_w = src_w >> 3;
		if (src_h > (drw_h << 3))
			drw_h = src_h >> 3;
	}

	/* Clip */
	xa = src_x;
	xb = src_x + src_w;
	ya = src_y;
	yb = src_y + src_h;

	dstBox.x1 = drw_x;
	dstBox.x2 = drw_x + drw_w;
	dstBox.y1 = drw_y;
	dstBox.y2 = drw_y + drw_h;

	if (!xf86XVClipVideoHelper(&dstBox, &xa, &xb, &ya, &yb, clipBoxes,
				   width, height))
		return Success;

	if (!pPriv->blitter) {
		dstBox.x1 -= pScrn->frameX0;
		dstBox.x2 -= pScrn->frameX0;
		dstBox.y1 -= pScrn->frameY0;
		dstBox.y2 -= pScrn->frameY0;
	}

	bpp = pScrn->bitsPerPixel >> 3;

	switch(id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		srcPitch = (width + 3) & ~3;	/* of luma */
		s2offset = srcPitch * height;
		srcPitch2 = ((width >> 1) + 3) & ~3;
		s3offset = (srcPitch2 * (height >> 1)) + s2offset;
		dstPitch = ((width << 1) + 63) & ~63;
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
		srcPitch = width << 1;
		dstPitch = ((width << 1) + 63) & ~63;
		break;
	case FOURCC_RGB:
		srcPitch = width << 2;
		dstPitch = ((width << 2) + 63) & ~63;
		break;
	default:
		return BadImplementation;
	}
	newSize = height * dstPitch / bpp;

	if (pPriv->doubleBuffer)
		newSize <<= 1;

	pPriv->video_mem = NVAllocateOverlayMemory(pScrn, pPriv->video_mem, 
							      newSize);
	if (!pPriv->video_mem)
		return BadAlloc;

	offset = pPriv->video_mem->offset;
	if (pPriv->doubleBuffer) {
		int mask = 1 << (pPriv->currentBuffer << 2);

#if 0
		/* burn the CPU until the next buffer is available */
		while(nvReadVIDEO(pNv,  NV_PVIDEO_BUFFER) & mask);
#else
		/* overwrite the newest buffer if there's not one free */
		if (nvReadVIDEO(pNv, NV_PVIDEO_BUFFER) & mask) {
			if (!pPriv->currentBuffer)
				offset += (newSize * bpp) >> 1;
			skip = TRUE;
		} else
#endif
		if (pPriv->currentBuffer)
			offset += (newSize * bpp) >> 1;
	}

	dst_start = pPriv->video_mem->map +
			(offset - (uint32_t)pPriv->video_mem->offset);

	/* We need to enlarge the copied rectangle by a pixel so the HW
	 * filtering doesn't pick up junk laying outside of the source */
	left = (xa - 0x00010000) >> 16;
	if (left < 0) left = 0;
	top = (ya - 0x00010000) >> 16;
	if (top < 0) top = 0;
	right = (xb + 0x0001ffff) >> 16;
	if (right > width) right = width;
	bottom = (yb + 0x0001ffff) >> 16;
	if (bottom > height) bottom = height;

	if(pPriv->blitter) NVSync(pScrn);

	switch(id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		left &= ~1;
		npixels = ((right + 1) & ~1) - left;
		top &= ~1;
		nlines = ((bottom + 1) & ~1) - top;

		dst_start += (left << 1) + (top * dstPitch);
		tmp = ((top >> 1) * srcPitch2) + (left >> 1);
		s2offset += tmp;
		s3offset += tmp;
		if (id == FOURCC_I420) {
			tmp = s2offset;
			s2offset = s3offset;
			s3offset = tmp;
		}

		NVCopyData420(buf + (top * srcPitch) + left,
				buf + s2offset, buf + s3offset,
				dst_start, srcPitch, srcPitch2,
				dstPitch, nlines, npixels);
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
		left &= ~1;
		npixels = ((right + 1) & ~1) - left;
		nlines = bottom - top;

		left <<= 1;
		buf += (top * srcPitch) + left;
		dst_start += left + (top * dstPitch);

		NVCopyData422(buf, dst_start, srcPitch,
				dstPitch, nlines, npixels);
		break;
	case FOURCC_RGB:
		npixels = right - left;
		nlines = bottom - top;

		left <<= 2;
		buf += (top * srcPitch) + left;
		dst_start += left + (top * dstPitch);

		NVCopyDataRGB(buf, dst_start, srcPitch,
				dstPitch, nlines, npixels);
		break;
	default:
		return BadImplementation;
	}

	if (!skip) {
		if (pPriv->blitter) {
			NVPutBlitImage(pScrn, offset, id,
				       dstPitch, &dstBox,
				       xa, ya, xb, yb,
				       width, height,
				       src_w, src_h, drw_w, drw_h,
				       clipBoxes, pDraw);
		} else {
			NVPutOverlayImage(pScrn, offset, id,
					  dstPitch, &dstBox, 
					  xa, ya, xb, yb,
					  width, height,
					  src_w, src_h, drw_w, drw_h,
					  clipBoxes);
			pPriv->currentBuffer ^= 1;
		}
	}

	return Success;
}
/*
 * QueryImageAttributes
 */
static int
NVQueryImageAttributes(ScrnInfoPtr pScrn, int id, 
		       unsigned short *w, unsigned short *h, 
		       int *pitches, int *offsets)
{
	int size, tmp;

	if (*w > 2046)
		*w = 2046;
	if (*h > 2046)
		*h = 2046;

	*w = (*w + 1) & ~1;
	if (offsets)
		offsets[0] = 0;

	switch (id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		*h = (*h + 1) & ~1;
		size = (*w + 3) & ~3;
		if (pitches)
			pitches[0] = size;
		size *= *h;
		if (offsets)
			offsets[1] = size;
		tmp = ((*w >> 1) + 3) & ~3;
		if (pitches)
			pitches[1] = pitches[2] = tmp;
		tmp *= (*h >> 1);
		size += tmp;
		if (offsets)
			offsets[2] = size;
		size += tmp;
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
		size = *w << 1;
		if (pitches)
			pitches[0] = size;
		size *= *h;
		break;
	case FOURCC_RGB:
		size = *w << 2;
		if (pitches)
			pitches[0] = size;
		size *= *h;
		break;
	default:
		*w = *h = size = 0;
		break;
	}

	return size;
}

/***** Exported offscreen surface stuff ****/


static int
NVAllocSurface(ScrnInfoPtr pScrn, int id,
	       unsigned short w, unsigned short h,
	       XF86SurfacePtr surface)
{
	NVPtr pNv = NVPTR(pScrn);
	NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(pNv); 
	int size, bpp;

	bpp = pScrn->bitsPerPixel >> 3;

	if (pPriv->grabbedByV4L)
		return BadAlloc;

	if ((w > 2046) || (h > 2046))
		return BadValue;

	w = (w + 1) & ~1;
	pPriv->pitch = ((w << 1) + 63) & ~63;
	size = h * pPriv->pitch / bpp;

	pPriv->video_mem = NVAllocateOverlayMemory(pScrn,
						   pPriv->video_mem,
						   size);
	if (!pPriv->video_mem)
		return BadAlloc;

	pPriv->offset = 0;
	
	surface->width = w;
	surface->height = h;
	surface->pScrn = pScrn;
	surface->pitches = &pPriv->pitch; 
	surface->offsets = &pPriv->offset;
	surface->devPrivate.ptr = (pointer)pPriv;
	surface->id = id;

	/* grab the video */
	NVStopOverlay(pScrn);
	pPriv->videoStatus = 0;
	REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
	pPriv->grabbedByV4L = TRUE;

	return Success;
}

static int
NVStopSurface(XF86SurfacePtr surface)
{
	NVPortPrivPtr pPriv = (NVPortPrivPtr)(surface->devPrivate.ptr);

	if (pPriv->grabbedByV4L && pPriv->videoStatus) {
		NVStopOverlay(surface->pScrn);
		pPriv->videoStatus = 0;
	}

	return Success;
}

static int 
NVFreeSurface(XF86SurfacePtr surface)
{
	NVPortPrivPtr pPriv = (NVPortPrivPtr)(surface->devPrivate.ptr);

	if (pPriv->grabbedByV4L) {
		NVStopSurface(surface);
		NVFreeOverlayMemory(surface->pScrn);
		pPriv->grabbedByV4L = FALSE;
	}

	return Success;
}

static int
NVGetSurfaceAttribute(ScrnInfoPtr pScrn, Atom attribute, INT32 *value)
{
	NVPtr pNv = NVPTR(pScrn);
	NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(pNv);

	return NVGetOverlayPortAttribute(pScrn, attribute,
					 value, (pointer)pPriv);
}

static int
NVSetSurfaceAttribute(ScrnInfoPtr pScrn, Atom attribute, INT32 value)
{
	NVPtr pNv = NVPTR(pScrn);
	NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(pNv);

	return NVSetOverlayPortAttribute(pScrn, attribute,
					 value, (pointer)pPriv);
}

static int
NVDisplaySurface(XF86SurfacePtr surface,
		 short src_x, short src_y, 
		 short drw_x, short drw_y,
		 short src_w, short src_h, 
		 short drw_w, short drw_h,
		 RegionPtr clipBoxes)
{
	ScrnInfoPtr pScrn = surface->pScrn;
	NVPortPrivPtr pPriv = (NVPortPrivPtr)(surface->devPrivate.ptr);
	INT32 xa, xb, ya, yb;
	BoxRec dstBox;

	if (!pPriv->grabbedByV4L)
		return Success;

	if (src_w > (drw_w << 3))
		drw_w = src_w >> 3;
	if (src_h > (drw_h << 3))
		drw_h = src_h >> 3;

	/* Clip */
	xa = src_x;
	xb = src_x + src_w;
	ya = src_y;
	yb = src_y + src_h;

	dstBox.x1 = drw_x;
	dstBox.x2 = drw_x + drw_w;
	dstBox.y1 = drw_y;
	dstBox.y2 = drw_y + drw_h;

	if(!xf86XVClipVideoHelper(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, 
				  surface->width, surface->height))
		return Success;

	dstBox.x1 -= pScrn->frameX0;
	dstBox.x2 -= pScrn->frameX0;
	dstBox.y1 -= pScrn->frameY0;
	dstBox.y2 -= pScrn->frameY0;

	pPriv->currentBuffer = 0;

	NVPutOverlayImage(pScrn, surface->offsets[0], surface->id,
			  surface->pitches[0], &dstBox, xa, ya, xb, yb,
			  surface->width, surface->height, src_w, src_h,
			  drw_w, drw_h, clipBoxes);

	return Success;
}

static XF86VideoAdaptorPtr
NVSetupBlitVideo (ScreenPtr pScreen)
{
	ScrnInfoPtr         pScrn = xf86Screens[pScreen->myNum];
	NVPtr               pNv       = NVPTR(pScrn);
	XF86VideoAdaptorPtr adapt;
	NVPortPrivPtr       pPriv;
	int i;

	if (!(adapt = xcalloc(1, sizeof(XF86VideoAdaptorRec) +
					sizeof(NVPortPrivRec) +
					(sizeof(DevUnion) * NUM_BLIT_PORTS)))) {
		return NULL;
	}

	adapt->type		= XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags		= 0;
	adapt->name		= "NV Video Blitter";
	adapt->nEncodings	= 1;
	adapt->pEncodings	= &DummyEncoding;
	adapt->nFormats		= NUM_FORMATS_ALL;
	adapt->pFormats		= NVFormats;
	adapt->nPorts		= NUM_BLIT_PORTS;
	adapt->pPortPrivates	= (DevUnion*)(&adapt[1]);

	pPriv = (NVPortPrivPtr)(&adapt->pPortPrivates[NUM_BLIT_PORTS]);
	for(i = 0; i < NUM_BLIT_PORTS; i++)
		adapt->pPortPrivates[i].ptr = (pointer)(pPriv);

	if(pNv->WaitVSyncPossible) {
		adapt->pAttributes = NVBlitAttributes;
		adapt->nAttributes = NUM_BLIT_ATTRIBUTES;
	} else {
		adapt->pAttributes = NULL;
		adapt->nAttributes = 0;
	}

	adapt->pImages			= NVImages;
	adapt->nImages			= NUM_IMAGES_ALL;
	adapt->PutVideo			= NULL;
	adapt->PutStill			= NULL;
	adapt->GetVideo			= NULL;
	adapt->GetStill			= NULL;
	adapt->StopVideo		= NVStopBlitVideo;
	adapt->SetPortAttribute		= NVSetBlitPortAttribute;
	adapt->GetPortAttribute		= NVGetBlitPortAttribute;
	adapt->QueryBestSize		= NVQueryBestSize;
	adapt->PutImage			= NVPutImage;
	adapt->QueryImageAttributes	= NVQueryImageAttributes;

	pPriv->videoStatus		= 0;
	pPriv->grabbedByV4L		= FALSE;
	pPriv->blitter			= TRUE;
	pPriv->doubleBuffer		= FALSE;
	pPriv->SyncToVBlank		= pNv->WaitVSyncPossible;

	pNv->blitAdaptor		= adapt;
	xvSyncToVBlank			= MAKE_ATOM("XV_SYNC_TO_VBLANK");

	return adapt;
}

static XF86VideoAdaptorPtr 
NV10SetupOverlayVideo(ScreenPtr pScreen)
{
	ScrnInfoPtr         pScrn = xf86Screens[pScreen->myNum];
	NVPtr               pNv       = NVPTR(pScrn);
	XF86VideoAdaptorPtr adapt;
	NVPortPrivPtr       pPriv;

	if (!(adapt = xcalloc(1, sizeof(XF86VideoAdaptorRec) + 
					sizeof(NVPortPrivRec) + 
					sizeof(DevUnion)))) {
		return NULL;
	}

	adapt->type		= XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags		= VIDEO_OVERLAID_IMAGES|VIDEO_CLIP_TO_VIEWPORT;
	adapt->name		= "NV Video Overlay";
	adapt->nEncodings	= 1;
	adapt->pEncodings	= &DummyEncoding;
	adapt->nFormats		= NUM_FORMATS_ALL;
	adapt->pFormats		= NVFormats;
	adapt->nPorts		= 1;
	adapt->pPortPrivates	= (DevUnion*)(&adapt[1]);

	pPriv = (NVPortPrivPtr)(&adapt->pPortPrivates[1]);
	adapt->pPortPrivates[0].ptr	= (pointer)(pPriv);

	adapt->pAttributes		= NVOverlayAttributes;
	adapt->nAttributes		= NUM_OVERLAY_ATTRIBUTES;
	adapt->pImages			= NVImages;
	adapt->nImages			= NUM_IMAGES_YUV;
	adapt->PutVideo			= NULL;
	adapt->PutStill			= NULL;
	adapt->GetVideo			= NULL;
	adapt->GetStill			= NULL;
	adapt->StopVideo		= NVStopOverlayVideo;
	adapt->SetPortAttribute		= NVSetOverlayPortAttribute;
	adapt->GetPortAttribute		= NVGetOverlayPortAttribute;
	adapt->QueryBestSize		= NVQueryBestSize;
	adapt->PutImage			= NVPutImage;
	adapt->QueryImageAttributes	= NVQueryImageAttributes;

	pPriv->videoStatus		= 0;
	pPriv->currentBuffer		= 0;
	pPriv->grabbedByV4L		= FALSE;
	pPriv->blitter			= FALSE;

	NVSetPortDefaults (pScrn, pPriv);

	/* gotta uninit this someplace */
	REGION_NULL(pScreen, &pPriv->clip);

	pNv->overlayAdaptor	= adapt;

	xvBrightness		= MAKE_ATOM("XV_BRIGHTNESS");
	xvDoubleBuffer		= MAKE_ATOM("XV_DOUBLE_BUFFER");
	xvContrast		= MAKE_ATOM("XV_CONTRAST");
	xvColorKey		= MAKE_ATOM("XV_COLORKEY");
	xvSaturation		= MAKE_ATOM("XV_SATURATION");
	xvHue			= MAKE_ATOM("XV_HUE");
	xvAutopaintColorKey	= MAKE_ATOM("XV_AUTOPAINT_COLORKEY");
	xvSetDefaults		= MAKE_ATOM("XV_SET_DEFAULTS");
	xvITURBT709		= MAKE_ATOM("XV_ITURBT_709");

	NVResetVideo(pScrn);

	return adapt;
}

XF86OffscreenImageRec NVOffscreenImages[2] = {
	{
		&NVImages[0],
		VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
		NVAllocSurface,
		NVFreeSurface,
		NVDisplaySurface,
		NVStopSurface,
		NVGetSurfaceAttribute,
		NVSetSurfaceAttribute,
		2046, 2046,
		NUM_OVERLAY_ATTRIBUTES - 1,
		&NVOverlayAttributes[1]
	},
	{
		&NVImages[2],
		VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
		NVAllocSurface,
		NVFreeSurface,
		NVDisplaySurface,
		NVStopSurface,
		NVGetSurfaceAttribute,
		NVSetSurfaceAttribute,
		2046, 2046,
		NUM_OVERLAY_ATTRIBUTES - 1,
		&NVOverlayAttributes[1]
	}
};

static void
NVInitOffscreenImages (ScreenPtr pScreen)
{
	xf86XVRegisterOffscreenImages(pScreen, NVOffscreenImages, 2);
}

static Bool
NVChipsetHasOverlay(NVPtr pNv)
{
	switch (pNv->Architecture) {
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
		return TRUE;
	case NV_ARCH_40:
		if ((pNv->Chipset & 0xfff0) == CHIPSET_NV40)
			return TRUE;
		break;
	default:
		break;
	}

	return FALSE;
}

static XF86VideoAdaptorPtr
NVSetupOverlayVideo(ScreenPtr pScreen)
{
	ScrnInfoPtr          pScrn = xf86Screens[pScreen->myNum];
	XF86VideoAdaptorPtr  overlayAdaptor = NULL;
	NVPtr                pNv   = NVPTR(pScrn);

	if (!NVChipsetHasOverlay(pNv))
		return NULL;

	/*XXX: Do we still want to provide the overlay anyway, but make the
	 *     blit adaptor the default if composite is enabled?
	 */
#ifdef COMPOSITE
	if (!noCompositeExtension) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"XV: Video overlay not available, composite enabled\n");
		return NULL;
	}
#endif

	overlayAdaptor = NV10SetupOverlayVideo(pScreen);
	if (overlayAdaptor)
		NVInitOffscreenImages(pScreen);

	return overlayAdaptor;
}

void NVInitVideo (ScreenPtr pScreen)
{
	ScrnInfoPtr          pScrn = xf86Screens[pScreen->myNum];
	XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
	XF86VideoAdaptorPtr  overlayAdaptor = NULL;
	XF86VideoAdaptorPtr  blitAdaptor = NULL;
	int                  num_adaptors;

	if (pScrn->bitsPerPixel == 8)
		return;

	overlayAdaptor = NVSetupOverlayVideo(pScreen);
	blitAdaptor    = NVSetupBlitVideo(pScreen);

	num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);
	if(blitAdaptor || overlayAdaptor) {
		int size = num_adaptors;

		if(overlayAdaptor) size++;
		if(blitAdaptor)    size++;

		newAdaptors = xalloc(size * sizeof(XF86VideoAdaptorPtr *));
		if(newAdaptors) {
			if(num_adaptors) {
				memcpy(newAdaptors, adaptors, num_adaptors *
						sizeof(XF86VideoAdaptorPtr));
			}

			if(overlayAdaptor) {
				newAdaptors[num_adaptors] = overlayAdaptor;
				num_adaptors++;
			}

			if(blitAdaptor) {
				newAdaptors[num_adaptors] = blitAdaptor;
				num_adaptors++;
			}

			adaptors = newAdaptors;
		}
	}

	if (num_adaptors)
		xf86XVScreenInit(pScreen, adaptors, num_adaptors);
	if (newAdaptors)
		xfree(newAdaptors);
}

