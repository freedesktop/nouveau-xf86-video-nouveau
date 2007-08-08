
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

#define IMAGE_MAX_W 2046
#define IMAGE_MAX_H 2046

#define OFF_DELAY 	500  /* milliseconds */
#define FREE_DELAY 	5000

#define OFF_TIMER 	0x01
#define FREE_TIMER	0x02
#define CLIENT_VIDEO_ON	0x04

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)

#define NUM_BLIT_PORTS 32

/* Value taken by pPriv -> currentHostBuffer when we failed to allocate the two private buffers in TT memory, so that we can catch this case
and attempt no other allocation afterwards (performance reasons) */
#define NO_PRIV_HOST_BUFFER_AVAILABLE 9999 

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
	NVAllocRec * 	TT_mem_chunk[2];
	int		currentHostBuffer;
	struct drm_nouveau_notifierobj_alloc *DMANotifier[2];
} NVPortPrivRec, *NVPortPrivPtr;


/* Xv DMA notifiers status tracing */

enum {
XV_DMA_NOTIFIER_NOALLOC=0, //notifier not allocated 
XV_DMA_NOTIFIER_INUSE=1,
XV_DMA_NOTIFIER_FREE=2, //notifier allocated, ready for use
};

/* We have six notifiers available, they are not allocated at startup */
int XvDMANotifierStatus[6]= { XV_DMA_NOTIFIER_NOALLOC , XV_DMA_NOTIFIER_NOALLOC , XV_DMA_NOTIFIER_NOALLOC ,
					XV_DMA_NOTIFIER_NOALLOC , XV_DMA_NOTIFIER_NOALLOC , XV_DMA_NOTIFIER_NOALLOC };
struct drm_nouveau_notifierobj_alloc * XvDMANotifiers[6];

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
	IMAGE_MAX_W, IMAGE_MAX_H,
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

/**
 * NVSetPortDefaults
 * set attributes of port "pPriv" to compiled-in (except for colorKey) defaults
 * 
 * @param pScrn screen to get the default colorKey from
 * @param pPriv port to reset to defaults
 */
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
	pPriv->currentHostBuffer	= 0;
}

/**
 * NVResetVideo
 * writes the current attributes from the overlay port to the hardware
 */
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

/**
 * NVStopOverlay
 * Tell the hardware to stop the overlay
 */
static void 
NVStopOverlay (ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	nvWriteVIDEO(pNv, NV_PVIDEO_STOP, 1);
}

/**
 * NVXvDMANotifierAlloc
 * allocates a notifier from the table of 6 we have
 *
 * @return a notifier instance or NULL on error
 */
static struct drm_nouveau_notifierobj_alloc * NVXvDMANotifierAlloc(ScrnInfoPtr pScrn)
{
int i;
for ( i = 0; i < 6; i ++ )
	{
	if ( XvDMANotifierStatus[i] == XV_DMA_NOTIFIER_INUSE ) 
		continue;
	
	if ( XvDMANotifierStatus[i] == XV_DMA_NOTIFIER_FREE )
		{
		XvDMANotifierStatus[i] = XV_DMA_NOTIFIER_INUSE;
		return XvDMANotifiers[i];
		}
	
	if ( XvDMANotifierStatus[i] == XV_DMA_NOTIFIER_NOALLOC )
		{
		XvDMANotifiers[i] = NVNotifierAlloc(pScrn, NvDmaXvNotifier0 + i);
		if (XvDMANotifiers[i]) 
			{
			XvDMANotifierStatus[i] = XV_DMA_NOTIFIER_INUSE;
			return XvDMANotifiers[i];
			}
		else return NULL;
		}
	}
	
return NULL;
}

/**
 * NVXvDMANotifierFree
 * frees a notifier from the table of 6 we have
 *
 * 
 */
static void NVXvDMANotifierFree(ScrnInfoPtr pScrn, struct drm_nouveau_notifierobj_alloc * target)
{
int i;
for ( i = 0; i < 6; i ++ )
	{
	if ( XvDMANotifiers[i] == target )
		break;
	}
XvDMANotifierStatus[i] = XV_DMA_NOTIFIER_FREE;
}

/**
 * NVAllocateVideoMemory
 * allocates video memory for a given port
 * 
 * @param pScrn screen which requests the memory
 * @param mem pointer to previously allocated memory for reallocation
 * @param size size of requested memory segment
 * @return pointer to the allocated memory
 */
static NVAllocRec *
NVAllocateVideoMemory(ScrnInfoPtr pScrn, NVAllocRec *mem, int size)
{
	NVPtr pNv = NVPTR(pScrn);


	if(mem) {
		if(mem->size >= size)
			return mem;
		NVFreeMemory(pNv, mem);
	}

	return NVAllocateMemory(pNv, NOUVEAU_MEM_FB, size); /* align 32? */
}

/**
 * NVAllocateTTMemory
 * allocates TT memory for a given port
 * 
 * @param pScrn screen which requests the memory
 * @param mem pointer to previously allocated memory for reallocation
 * @param size size of requested memory segment
 * @return pointer to the allocated memory
 */
static NVAllocRec *
NVAllocateTTMemory(ScrnInfoPtr pScrn, NVAllocRec *mem, int size)
{
	NVPtr pNv = NVPTR(pScrn);


	if(mem) {
		if(mem->size >= size)
			return mem;
		NVFreeMemory(pNv, mem);
	}
	/*We may take PCI memory, but that does not mean we won't fallback on CPU copy in that case.*/
	return NVAllocateMemory(pNv, NOUVEAU_MEM_AGP | NOUVEAU_MEM_PCI_ACCEPTABLE, size); /* align 32? */
}

/**
 * NVFreePortMemory
 * frees memory held by a given port
 * 
 * @param pScrn screen whose port wants to free memory
 * @param pPriv port to free memory of
 */
static void
NVFreePortMemory(ScrnInfoPtr pScrn, NVPortPrivPtr pPriv)
{
	NVPtr         pNv   = NVPTR(pScrn);

	//xf86DrvMsg(0, X_INFO, "Freeing port memory - TTmem chunks %p %p, notifiers %p %p\n", pPriv->TT_mem_chunk[0], pPriv->TT_mem_chunk[1], pPriv->DMANotifier[0], pPriv->DMANotifier[1]);
	
	if(pPriv->video_mem) {
		NVFreeMemory(pNv, pPriv->video_mem);
		pPriv->video_mem = NULL;
	}
	
	if ( pPriv->TT_mem_chunk[ 0 ] && pPriv->DMANotifier [ 0 ] )
		{
		NVNotifierWaitStatus(pScrn, pPriv->DMANotifier [ 0 ] ,  0, 1000);
		}
	
	if ( pPriv->TT_mem_chunk[ 1 ] && pPriv->DMANotifier [ 1 ] )
		{
		NVNotifierWaitStatus(pScrn, pPriv->DMANotifier [ 1 ] , 0, 1000);
		} 		
		
	if(pPriv->TT_mem_chunk[0]) {
		NVFreeMemory(pNv, pPriv->TT_mem_chunk[0]);
		pPriv->TT_mem_chunk[0] = NULL;
	}
	
	if(pPriv->TT_mem_chunk[1]) {
		NVFreeMemory(pNv, pPriv->TT_mem_chunk[1]);
		pPriv->TT_mem_chunk[1] = NULL;
	}
	
	if(pPriv->DMANotifier[0]) {
		NVXvDMANotifierFree(pScrn, pPriv->DMANotifier[0]);
		pPriv->DMANotifier[0] = NULL;
	}
	
	if(pPriv->DMANotifier[1]) {
		NVXvDMANotifierFree(pScrn, pPriv->DMANotifier[1]);
		pPriv->DMANotifier[1] = NULL;
	}
	
}

/**
 * NVFreeOverlayMemory
 * frees memory held by the overlay port
 * 
 * @param pScrn screen whose overlay port wants to free memory
 */
static void
NVFreeOverlayMemory(ScrnInfoPtr pScrn)
{
	NVPtr	pNv = NVPTR(pScrn);
	NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(pNv);
	NVFreePortMemory(pScrn, pPriv);
}

/**
 * NVFreeBlitMemory
 * frees memory held by the blit port
 * 
 * @param pScrn screen whose blit port wants to free memory
 */
static void
NVFreeBlitMemory(ScrnInfoPtr pScrn)
{
	NVPtr	pNv = NVPTR(pScrn);
	NVPortPrivPtr pPriv = GET_BLIT_PRIVATE(pNv);
	NVFreePortMemory(pScrn, pPriv);
}

/**
 * NVVideoTimerCallback
 * callback function which perform cleanup tasks (stop overlay, free memory).
 * within the driver it is only called once from NVBlockHandler in nv_driver.c
 */
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

/**
 * NVPutOverlayImage
 * program hardware to overlay image into front buffer
 * 
 * @param pScrn screen
 * @param src_offset
 * @param id colorspace of image
 * @param src_pitch
 * @param dstBox
 * @param x1
 * @param y1
 * @param x2
 * @param y2
 * @param width
 * @param height
 * @param src_w
 * @param src_h
 * @param drw_w
 * @param drw_h
 * @param clipBoxes
 */
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
		{
		//xf86DrvMsg(0, X_INFO, "calling xf86XVFillKeyHelper at %u, colorkey is %u\n", GetTimeInMillis(), pPriv->colorKey);
		xf86XVFillKeyHelper(pScrn->pScreen, pPriv->colorKey, clipBoxes);
		}
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

/**
 * NVPutBlitImage
 * 
 * @param pScrn screen
 * @param src_offset
 * @param id colorspace of image
 * @param src_pitch
 * @param dstBox
 * @param x1
 * @param y1
 * @param x2
 * @param y2
 * @param width
 * @param height
 * @param src_w
 * @param src_h
 * @param drw_w
 * @param drw_h
 * @param clipBoxes
 * @param pDraw
 */
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
			NVDmaStart(pNv, NvContextSurfaces,
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
		NVDmaStart(pNv, NvScaledImage, STRETCH_BLIT_FORMAT, 2);
		NVDmaNext (pNv, src_format);
		NVDmaNext (pNv, STRETCH_BLIT_OPERATION_COPY);
	} else {
		NVDmaStart(pNv, NvScaledImage, STRETCH_BLIT_FORMAT, 1);
		NVDmaNext (pNv, src_format);
	}

	while(nbox--) {
		NVDmaStart(pNv, NvRectangle, RECT_SOLID_COLOR, 1);
		NVDmaNext (pNv, 0);

		NVDmaStart(pNv, NvScaledImage, STRETCH_BLIT_CLIP_POINT, 6);
		NVDmaNext (pNv, (pbox->y1 << 16) | pbox->x1); 
		NVDmaNext (pNv, ((pbox->y2 - pbox->y1) << 16) |
				 (pbox->x2 - pbox->x1));
		NVDmaNext (pNv, dst_point);
		NVDmaNext (pNv, dst_size);
		NVDmaNext (pNv, dsdx);
		NVDmaNext (pNv, dtdy);

		NVDmaStart(pNv, NvScaledImage, STRETCH_BLIT_SRC_SIZE, 4);
		NVDmaNext (pNv, (height << 16) | width);
		NVDmaNext (pNv, src_pitch);
		NVDmaNext (pNv, src_offset);
		NVDmaNext (pNv, src_point);
		pbox++;
	}

	if (!pNv->useEXA) {
		if(pNv->CurrentLayout.depth == 15) {
			NVDmaStart(pNv, NvContextSurfaces,
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

/**
 * NVStopBlitVideo
 * TODO ?
 */
static void
NVStopBlitVideo(ScrnInfoPtr pScrn, pointer data, Bool Exit)
{
}

/**
 * NVSetOverlayPortAttribute
 * sets the attribute "attribute" of port "data" to value "value"
 * calls NVResetVideo(pScrn) to apply changes to hardware
 * 
 * @param pScrenInfo
 * @param attribute attribute to set
 * @param value value to which attribute is to be set
 * @param data port from which the attribute is to be set
 * 
 * @return Success, if setting is successful
 * BadValue/BadMatch, if value/attribute are invalid
 * @see NVResetVideo(ScrnInfoPtr pScrn)
 */
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

/**
 * NVGetOverlayPortAttribute
 * 
 * @param pScrn unused
 * @param attribute attribute to be read
 * @param value value of attribute will be stored in this pointer
 * @param data port from which attribute will be read
 * @return Success, if queried attribute exists
 */
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

/**
 * NVSetBlitPortAttribute
 * sets the attribute "attribute" of port "data" to value "value"
 * supported attributes:
 * - xvSyncToVBlank (values: 0,1)
 * - xvSetDefaults (values: NA; SyncToVBlank will be set, if hardware supports it)
 * 
 * @param pScrenInfo
 * @param attribute attribute to set
 * @param value value to which attribute is to be set
 * @param data port from which the attribute is to be set
 * 
 * @return Success, if setting is successful
 * BadValue/BadMatch, if value/attribute are invalid
 */
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

/**
 * NVGetBlitPortAttribute
 * reads the value of attribute "attribute" from port "data" into INT32 "*value"
 * currently only one attribute supported: xvSyncToVBlank
 * 
 * @param pScrn unused
 * @param attribute attribute to be read
 * @param value value of attribute will be stored here
 * @param data port from which attribute will be read
 * @return Success, if queried attribute exists
 */
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


/**
 * QueryBestSize
 * used by client applications to ask the driver:
 * how would you actually scale a video of dimensions
 * vid_w, vid_h, if i wanted you to scale it to dimensions
 * drw_w, drw_h?
 * function stores actual scaling size in pointers p_w, p_h.
 * 
 * - currently the image cannot be scaled to less than
 * 1/8th of the original size in either dimension. why?
 * - what happens if the client requests a scaling to a larger value than
 * the hardware is capable of (IMAGE_MAX_W, IMAGE_MAX_H)?
 * 
 * @param pScrn unused
 * @param motion unused
 * @param vid_w width of source video
 * @param vid_h height of source video
 * @param drw_w desired scaled width as requested by client
 * @param drw_h desired scaled height as requested by client
 * @param p_w actual scaled width as the driver is capable of
 * @param p_h actual scaled height as the driver is capable of
 * @param data unused
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

/**
 * NVCopyData420
 * used by NVPutImage() function to copy (image)data from
 * system RAM to VRAM and change data order.
 * 
 * @param src1 source buffer of luma
 * @param src2 source buffer of chroma1
 * @param src3 source buffer of chroma2
 * @param dst1 destination buffer
 * @param srcPitch pitch of src1
 * @param srcPitch2 pitch of src2, src3
 * @param dstPitch pitch of dst1
 * @param h number of lines to copy
 * @param w length of lines to copy
 */
static inline void NVCopyData420(unsigned char *src1, unsigned char *src2,
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


/**
 * NVPutImage
 * PutImage is "the" important function of the Xv extension.
 * a client (e.g. video player) calls this function for every
 * image (of the video) to be displayed. this function then
 * scales and displays the image.
 * 
 * @param pScrn screen which hold the port where the image is put
 * @param src_x
 * @param src_y
 * @param src_w
 * @param src_h
 * @param drw_x
 * @param drw_y
 * @param drw_w
 * @param drw_h
 * @param id colorspace of image
 * @param buf pointer to buffer containing the source image
 * @param width
 * @param height
 * @param Sync unused
 * @param clipBoxes
 * @param data pointer to port 
 * @param pDraw
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
	INT32 xa = 0, xb = 0, ya = 0, yb = 0; //source box
	int newSize = 0, offset = 0, s2offset = 0, s3offset = 0; //byte size of the whole source data, card VRAM offset, source offsets for U and V planes
	int srcPitch = 0, srcPitch2 = 0, dstPitch = 0; //source pitch, source pitch of U and V planes in case of YV12, VRAM destination pitch
	int top = 0, left = 0, right = 0, bottom = 0, npixels = 0, nlines = 0; //position of the given source data (using src_*), number of pixels and lines we are interested in
	Bool skip = FALSE;
	BoxRec dstBox;
	CARD32 tmp = 0;
	int line_len = 0; //length of a line, like npixels, but in bytes 
	int DMAoffset = 0; //additional VRAM offset to start the DMA copy to
	NVAllocRec * destination_buffer = NULL;
	unsigned char * video_mem_destination = NULL;  

	if (pPriv->grabbedByV4L)
		return Success;

	if (!pPriv->blitter) { /* overlay hardware scaler limitation */
		if (src_w > (drw_w << 3))
			drw_w = src_w >> 3;
		if (src_h > (drw_h << 3))
			drw_h = src_h >> 3;
	}
	
	//xf86DrvMsg(0, X_INFO, "%s: src %hd,%hd->%hd,%hd dst %hd,%hd->%hd,%hd w %hd h %hd\n", pPriv->blitter ? "Blitter" : "Overlay", src_x, src_y, src_w, src_h, drw_x, drw_y, drw_w, drw_h, width, height);

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

	
	/* Set up source and destination pitch (usually the same, except for YV12 that is converted to YUY2, and  some alignment considerations*/
	switch(id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		srcPitch = (width + 3) & ~3;	/* of luma */
		s2offset = srcPitch * height;
		srcPitch2 = ((width >> 1) + 3) & ~3; /*of chroma*/
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
	

	newSize = height * dstPitch;
	
	if (pPriv->doubleBuffer) // double buffering ...
                newSize <<= 1; // ... means double the amount of VRAM needed
	
	pPriv->video_mem = NVAllocateVideoMemory(pScrn, pPriv->video_mem, 
							      newSize);
	if (!pPriv->video_mem)
		return BadAlloc;

	offset = pPriv->video_mem->offset;
	
	/*The overlay supports hardware double buffering. We handle this here*/
	if (pPriv->doubleBuffer) {
		int mask = 1 << (pPriv->currentBuffer << 2);

		/* overwrite the newest buffer if there's not one free */
		if (nvReadVIDEO(pNv, NV_PVIDEO_BUFFER) & mask) {
			if (!pPriv->currentBuffer)
				offset += (height) * dstPitch;
			skip = TRUE;
		} else

		if (pPriv->currentBuffer)
			offset += (height) * dstPitch;
	}


	/* We need to enlarge the copied rectangle by a pixel so the HW
	 * filtering doesn't pick up junk laying outside of the source */
	/* This is fixed point arithmetic, as xf86XVClipVideoHelper probably turns its parameter into fixed point values */
	left = (xa - 0x00010000) >> 16;
	if (left < 0) left = 0;
	top = (ya - 0x00010000) >> 16;
	if (top < 0) top = 0;
	right = (xb + 0x0001ffff) >> 16;
	if (right > width) right = width;
	bottom = (yb + 0x0001ffff) >> 16;
	if (bottom > height) bottom = height;

	/* Reason of this call probably has to do with flushing the GPU before being able to operate on VRAM. We could get rid of it now, at least in most cases. */
	if(pPriv->blitter) NVSync(pScrn);

	
	/* Now we calculate the different values we need for the transfer : width and height of the rectangle of "interesting" data in the source buffer (usually everything,
	but it may not be the case, e.g. with tvtime and overscanning. */	
	switch(id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		left &= ~1;
		npixels = ((right + 1) & ~1) - left;
		top &= ~1;
		nlines = ((bottom + 1) & ~1) - top;

		DMAoffset += (left << 1) + (top * dstPitch);
		tmp = ((top >> 1) * srcPitch2) + (left >> 1);
		s2offset += tmp;
		s3offset += tmp;
		line_len = npixels << 1;
		if (id == FOURCC_I420) {
			tmp = s2offset;
			s2offset = s3offset;
			s3offset = tmp;
		}
		buf  += 0; //we do not touch it yet, because we need the base pointer to find the position of U and V planes
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
		left &= ~1;
		npixels = ((right + 1) & ~1) - left;
		nlines = bottom - top;
		line_len = npixels << 1;
		left <<= 1;
		buf += (top * srcPitch) + left;
		DMAoffset += left + (top * dstPitch);
		break;
	case FOURCC_RGB:
		npixels = right - left;
		nlines = bottom - top;
		left <<= 2;
		buf += (top * srcPitch) + left;
		line_len = npixels << 2;
		DMAoffset += left + (top * dstPitch);
		break;
	default:
		return BadImplementation;
	}

	/*Now we take a decision regarding the way we send the data to the card.
	Either we use double buffering of "private" TT memory
	Either we rely on X's GARTScratch 
	Either we fallback on CPU copy
	*/
	
	/* Try to allocate host-side double buffers, unless we have already failed*/
	/* We take only nlines * line_len bytes - that is, only the pixel data we are interested in - because the stuff in the GART is 
		 written contiguously */
	
	if ( pPriv -> currentHostBuffer != NO_PRIV_HOST_BUFFER_AVAILABLE )
		{
		pPriv->TT_mem_chunk[0] = NVAllocateTTMemory(pScrn, pPriv->TT_mem_chunk[0], 
							      nlines * line_len);
		if ( pPriv->TT_mem_chunk[0] )
			{
			pPriv->TT_mem_chunk[1] = NVAllocateTTMemory(pScrn, pPriv->TT_mem_chunk[1], 
							      nlines * line_len);
			
			if ( ! pPriv->TT_mem_chunk[1] )
				{
				NVFreeMemory(pNv, pPriv->TT_mem_chunk[0]);
				pPriv->TT_mem_chunk[0] = NULL;
				pPriv -> currentHostBuffer = NO_PRIV_HOST_BUFFER_AVAILABLE;
				//xf86DrvMsg(0, X_INFO, "Alloc 1 failed\n");
				}
			}
		else 
			{
			pPriv -> currentHostBuffer = NO_PRIV_HOST_BUFFER_AVAILABLE;
			//xf86DrvMsg(0, X_INFO, "Alloc 0 failed\n");
			}
		}
	
	if ( pPriv->currentHostBuffer != NO_PRIV_HOST_BUFFER_AVAILABLE )
		{ //if we have a private buffer
		destination_buffer = pPriv->TT_mem_chunk[pPriv->currentHostBuffer];
		//xf86DrvMsg(0, X_INFO, "Using private mem chunk #%d\n", pPriv->currentHostBuffer);
			
		/* We know where we are going to write, but we are not sure yet whether we can do it directly, because
			the card could be working on the buffer for the last-but-one frame. So we check if we have a notifier ready or not. 
			If we do, then we must wait for it before overwriting the buffer.
			Else we need one, so we call the Xv notifier allocator.*/
		if ( pPriv->DMANotifier [ pPriv->currentHostBuffer ] )
			{
			//xf86DrvMsg(0, X_INFO, "Waiting for notifier %p (%d)\n", pPriv->DMANotifier[pPriv->currentHostBuffer], pPriv->currentHostBuffer);
			if (!NVNotifierWaitStatus(pScrn, pPriv->DMANotifier[pPriv->currentHostBuffer], 0, 0))
				return FALSE;
			}
		else 
			{
			//xf86DrvMsg(0, X_INFO, "Allocating notifier...\n");
			pPriv->DMANotifier [ pPriv->currentHostBuffer ] = NVXvDMANotifierAlloc(pScrn);
			if (! pPriv->DMANotifier [ pPriv->currentHostBuffer ] )
				{ /* In case we are out of notifiers (then our guy is watching 3 movies at a time!!), we fallback on global GART, and free the private buffers.
					I know that's a lot of code but I believe it's necessary to properly handle all the cases*/
				xf86DrvMsg(0, X_ERROR, "Ran out of Xv notifiers!\n");
				NVFreeMemory(pNv, pPriv->TT_mem_chunk[0]);
				pPriv->TT_mem_chunk[0] = NULL;
				NVFreeMemory(pNv, pPriv->TT_mem_chunk[1]);
				pPriv->TT_mem_chunk[1] = NULL;
				pPriv -> currentHostBuffer = NO_PRIV_HOST_BUFFER_AVAILABLE;
				}
			//xf86DrvMsg(0, X_INFO, "Got notifier %p\n", pPriv->DMANotifier [ pPriv->currentHostBuffer ]);
			}
		}
	
	if ( pPriv -> currentHostBuffer == NO_PRIV_HOST_BUFFER_AVAILABLE )
		{ //otherwise we fall back on DDX's GARTScratch
		destination_buffer = pNv->GARTScratch;
		//xf86DrvMsg(0, X_INFO, "Using global GART memory chunk\n");
		}

	if ( !destination_buffer) //if we have no GART at all
		goto CPU_copy;
	
	if(nlines * line_len <= destination_buffer->size)
		{
		unsigned char *dst = destination_buffer->map;
		int i = 0;
			
		/* Upload to GART */
		switch(id) {
		case FOURCC_YV12:
		case FOURCC_I420:
			NVCopyData420(buf + (top * srcPitch) + left,
				buf + s2offset, buf + s3offset,
				dst, srcPitch, srcPitch2,
				line_len, nlines, npixels);
			break;
		case FOURCC_UYVY:
		case FOURCC_YUY2:
		case FOURCC_RGB:
			for ( i=0; i < nlines; i++)
				{
				memcpy(dst, buf, line_len);
				dst += line_len;
				buf += srcPitch;
				}
			break;
		default:
			return BadImplementation;
		}
		

		NVDmaStart(pNv, NvMemFormat, MEMFORMAT_DMA_OBJECT_IN, 2);
		NVDmaNext (pNv, NvDmaTT);
		NVDmaNext (pNv, NvDmaFB);
		pNv->M2MFDirection = 1;
		
		/* DMA to VRAM */
			
		NVDmaStart(pNv, NvMemFormat,
			NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
		NVDmaNext (pNv, (uint32_t)destination_buffer->offset);
		NVDmaNext (pNv, (uint32_t)offset + DMAoffset);
		NVDmaNext (pNv, line_len);
		NVDmaNext (pNv, dstPitch);
		NVDmaNext (pNv, line_len);
		NVDmaNext (pNv, nlines);
		NVDmaNext (pNv, (1<<8)|1);
		NVDmaNext (pNv, 0);
			
		if ( destination_buffer == pNv->GARTScratch ) 
			{
			NVNotifierReset(pScrn, pNv->Notifier0);
			}
		else {
			NVNotifierReset(pScrn, pPriv->DMANotifier[pPriv->currentHostBuffer]);
			NVDmaStart(pNv, NvMemFormat,
			NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY, 1);
			NVDmaNext (pNv, pPriv->DMANotifier[pPriv->currentHostBuffer]->handle);
			}
			
			
		NVDmaStart(pNv, NvMemFormat,
			NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY, 1);
		NVDmaNext (pNv, 0);
			
		NVDmaStart(pNv, NvMemFormat, 0x100, 1);
		NVDmaNext (pNv, 0);
				
		//Put back NvDmaNotifier0 for EXA
		NVDmaStart(pNv, NvMemFormat,
			NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY, 1);
		NVDmaNext (pNv, NvDmaNotifier0);
		
		NVDmaKickoff(pNv);			

		if ( destination_buffer == pNv->GARTScratch ) 
			if (!NVNotifierWaitStatus(pScrn, pNv->Notifier0, 0, 0))
				return FALSE;
		}
	else { //GART is too small, we fallback on CPU copy
		CPU_copy:
		video_mem_destination = pPriv->video_mem->map + (offset - (uint32_t)pPriv->video_mem->offset);
		int i = 0;
		//xf86DrvMsg(0, X_INFO, "Fallback on CPU copy\n");
		switch(id) 
			{
			case FOURCC_YV12:
			case FOURCC_I420:
		
				NVCopyData420(buf + (top * srcPitch) + left,
					buf + s2offset, buf + s3offset,
					video_mem_destination, srcPitch, srcPitch2,
					dstPitch, nlines, npixels);
			
			break;
			case FOURCC_UYVY:
			case FOURCC_YUY2:
			case FOURCC_RGB:
				for ( i=0; i < nlines; i++)
					{
					int dwords = npixels << 1;
					while (dwords & ~0x03) 
						{
						*video_mem_destination = *buf;
						*(video_mem_destination + 1) = *(buf + 1);
						*(video_mem_destination + 2) = *(buf + 2);
						*(video_mem_destination + 3) = *(buf + 3);
						video_mem_destination += 4;
						buf += 4;
						dwords -= 4;
						}
					switch ( dwords ) 
						{
						case 3:
							*(video_mem_destination + 2) = *(buf + 2);
						case 2:
							*(video_mem_destination + 1) = *(buf + 1);
						case 1:
							*video_mem_destination = *buf;
						}
										
					video_mem_destination += dstPitch - (npixels << 1);
					buf += srcPitch - (npixels << 1);
					}
				break;
			default:
				return BadImplementation;
			}
		}
		
	if ( pPriv->currentHostBuffer != NO_PRIV_HOST_BUFFER_AVAILABLE )
		pPriv->currentHostBuffer ^= 1;
		
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

/**
 * QueryImageAttributes
 * 
 * calculates
 * - size (memory required to store image),
 * - pitches,
 * - offsets
 * of image
 * depending on colorspace (id) and dimensions (w,h) of image
 * values of
 * - w,
 * - h
 * may be adjusted as needed
 * 
 * @param pScrn unused
 * @param id colorspace of image
 * @param w pointer to width of image
 * @param h pointer to height of image
 * @param pitches pitches[i] = length of a scanline in plane[i]
 * @param offsets offsets[i] = offset of plane i from the beginning of the image
 * @return size of the memory required for the XvImage queried
 */
static int
NVQueryImageAttributes(ScrnInfoPtr pScrn, int id, 
		       unsigned short *w, unsigned short *h, 
		       int *pitches, int *offsets)
{
	int size, tmp;

	if (*w > IMAGE_MAX_W)
		*w = IMAGE_MAX_W;
	if (*h > IMAGE_MAX_H)
		*h = IMAGE_MAX_H;

	*w = (*w + 1) & ~1; // width rounded up to an even number
	if (offsets)
		offsets[0] = 0;

	switch (id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		*h = (*h + 1) & ~1; // height rounded up to an even number
		size = (*w + 3) & ~3; // width rounded up to a multiple of 4
		if (pitches)
			pitches[0] = size; // width rounded up to a multiple of 4
		size *= *h;
		if (offsets)
			offsets[1] = size; // number of pixels in "rounded up" image
		tmp = ((*w >> 1) + 3) & ~3; // width/2 rounded up to a multiple of 4
		if (pitches)
			pitches[1] = pitches[2] = tmp; // width/2 rounded up to a multiple of 4
		tmp *= (*h >> 1); // 1/4*number of pixels in "rounded up" image
		size += tmp; // 5/4*number of pixels in "rounded up" image
		if (offsets)
			offsets[2] = size; // 5/4*number of pixels in "rounded up" image
		size += tmp; // = 3/2*number of pixels in "rounded up" image
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
		size = *w << 1; // 2*width
		if (pitches)
			pitches[0] = size; // 2*width
		size *= *h; // 2*width*height
		break;
	case FOURCC_RGB:
		size = *w << 2; // 4*width (32 bit per pixel)
		if (pitches)
			pitches[0] = size; // 4*width
		size *= *h; // 4*width*height
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

	if ((w > IMAGE_MAX_W) || (h > IMAGE_MAX_H))
		return BadValue;

	w = (w + 1) & ~1;
	pPriv->pitch = ((w << 1) + 63) & ~63;
	size = h * pPriv->pitch / bpp;

	pPriv->video_mem = NVAllocateVideoMemory(pScrn,
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

/**
 * NVSetupBlitVideo
 * this function does all the work setting up a blit port
 * 
 * @return blit port
 */
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

/**
 * NV10SetupOverlayVideo
 * this function does all the work setting up an overlay port
 * 
 * @return overlay port
 * @see NVResetVideo(ScrnInfoPtr pScrn)
 */
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
		IMAGE_MAX_W, IMAGE_MAX_H,
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
		IMAGE_MAX_W, IMAGE_MAX_H,
		NUM_OVERLAY_ATTRIBUTES - 1,
		&NVOverlayAttributes[1]
	}
};

static void
NVInitOffscreenImages (ScreenPtr pScreen)
{
	xf86XVRegisterOffscreenImages(pScreen, NVOffscreenImages, 2);
}

/**
 * NVChipsetHasOverlay
 * 
 * newer chips don't support overlay anymore.
 * overlay feature is emulated via textures.
 * 
 * @param pNv 
 * @return true, if chipset supports overlay
 */
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

/**
 * NVSetupOverlayVideo
 * check if chipset supports Overlay and CompositeExtension is disabled.
 * if so, setup overlay port
 * 
 * @return overlay port
 * @see NVChipsetHasOverlay(NVPtr pNv)
 * @see NV10SetupOverlayVideo(ScreenPtr pScreen)
 * @see NVInitOffscreenImages(ScreenPtr pScreen)
 */
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

/**
 * NVInitVideo
 * tries to initialize one new overlay port and one new blit port
 * and add them to the list of ports on screen "pScreen".
 * 
 * @param pScreen
 * @see NVSetupOverlayVideo(ScreenPtr pScreen)
 * @see NVSetupBlitVideo(ScreenPtr pScreen)
 */
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

