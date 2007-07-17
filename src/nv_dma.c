#include <errno.h>
#include "nv_include.h"
#include "nvreg.h"

void NVDmaKickoff(NVPtr pNv)
{
	if(pNv->dmaCurrent != pNv->dmaPut) {
		pNv->dmaPut = pNv->dmaCurrent;
		WRITE_PUT(pNv,  pNv->dmaPut);
	}
}

void NVDmaKickoffCallback(NVPtr pNv)
{
	NVDmaKickoff(pNv);
	pNv->DMAKickoffCallback = NULL;
}

/* There is a HW race condition with videoram command buffers.
 * You can't jump to the location of your put offset.  We write put
 * at the jump offset + SKIPS dwords with noop padding in between
 * to solve this problem
 */
#define SKIPS  8

void NVDmaWait (ScrnInfoPtr pScrn, int size)
{
	NVPtr pNv = NVPTR(pScrn);
	int t_start;
	int dmaGet;

	size++;

	t_start = GetTimeInMillis();
	while(pNv->dmaFree < size) {
		dmaGet = READ_GET(pNv);

		if(pNv->dmaPut >= dmaGet) {
			pNv->dmaFree = pNv->dmaMax - pNv->dmaCurrent;
			if(pNv->dmaFree < size) {
				NVDmaNext(pNv, (0x20000000|pNv->fifo.put_base));
				if(dmaGet <= SKIPS) {
					if(pNv->dmaPut <= SKIPS) /* corner case - will be idle */
						WRITE_PUT(pNv, SKIPS + 1);
					do {
						if (GetTimeInMillis() - t_start > 2000)
							NVSync(pScrn);
						dmaGet = READ_GET(pNv);
					} while(dmaGet <= SKIPS);
				}
				WRITE_PUT(pNv, SKIPS);
				pNv->dmaCurrent = pNv->dmaPut = SKIPS;
				pNv->dmaFree = dmaGet - (SKIPS + 1);
			}
		} else
			pNv->dmaFree = dmaGet - pNv->dmaCurrent - 1;

		if (GetTimeInMillis() - t_start > 2000)
			NVSync(pScrn);
	}
}

static void NVDumpLockupInfo(NVPtr pNv)
{
	int i,start;
	start=READ_GET(pNv)-20;
	if (start<0) start=0;
	xf86DrvMsg(0, X_INFO, "Fifo dump (lockup 0x%04x,0x%04x):\n",READ_GET(pNv),pNv->dmaPut);
	for(i=start;i<pNv->dmaPut+10;i++)
		xf86DrvMsg(0, X_INFO, "[0x%04x] 0x%08x\n", i, pNv->dmaBase[i]);
	xf86DrvMsg(0, X_INFO, "End of fifo dump\n");
}

static void
NVLockedUp(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	/* avoid re-entering FatalError on shutdown */
	if (pNv->LockedUp)
		return;
	pNv->LockedUp = TRUE;

	NVDumpLockupInfo(pNv);

	FatalError("DMA queue hang: dmaPut=%x, current=%x, status=%x\n",
		   pNv->dmaPut, READ_GET(pNv), pNv->PGRAPH[NV_PGRAPH_STATUS/4]);
}

void NVSync(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	int t_start, timeout = 2000;

	if(pNv->NoAccel)
		return;

	if(pNv->DMAKickoffCallback)
		(*pNv->DMAKickoffCallback)(pNv);

	/* Wait for entire FIFO to be processed */
	t_start = GetTimeInMillis();
	while((GetTimeInMillis() - t_start) < timeout &&
			(READ_GET(pNv) != pNv->dmaPut));
	if ((GetTimeInMillis() - t_start) >= timeout) {
		NVLockedUp(pScrn);
		return;
	}

	/* Wait for channel to go completely idle */
	NVNotifierReset(pScrn, pNv->Notifier0);
	NVDmaStart(pNv, NvSubImageBlit, 0x104, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, NvSubImageBlit, 0x100, 1);
	NVDmaNext (pNv, 0);
	NVDmaKickoff(pNv);
	if (!NVNotifierWaitStatus(pScrn, pNv->Notifier0, 0, timeout))
		NVLockedUp(pScrn);
}

void NVResetGraphics(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	CARD32 surfaceFormat, patternFormat, rectFormat, lineFormat;
	int pitch, i;

	if(pNv->NoAccel) return;

	pitch = pNv->CurrentLayout.displayWidth * (pNv->CurrentLayout.bitsPerPixel >> 3);

	pNv->dmaPut = pNv->dmaCurrent = READ_GET(pNv);
	pNv->dmaMax = (pNv->fifo.cmdbuf_size >> 2) - 1;
	pNv->dmaFree = pNv->dmaMax - pNv->dmaCurrent;

	/* assert there's enough room for the skips */
	if(pNv->dmaFree <= SKIPS)
		NVDmaWait(pScrn, SKIPS); 
	for (i=0; i<SKIPS; i++) {
		NVDmaNext(pNv,0);
		pNv->dmaBase[i]=0;
	}
	pNv->dmaFree -= SKIPS;

	NVAccelCommonInit(pScrn);

	/* EXA + XAA + Xv */
	NVDmaSetObjectOnSubchannel(pNv, NvSubContextSurfaces, NvContextSurfaces);
	NVDmaSetObjectOnSubchannel(pNv, NvSubRectangle   , NvRectangle      );
	NVDmaSetObjectOnSubchannel(pNv, NvSubScaledImage , NvScaledImage    );
	/* EXA + XAA */
	NVDmaSetObjectOnSubchannel(pNv, NvSubRop         , NvRop            );
	NVDmaSetObjectOnSubchannel(pNv, NvSubImagePattern, NvImagePattern   );
	NVDmaSetObjectOnSubchannel(pNv, NvSubImageBlit   , NvImageBlit      );
	if (pNv->useEXA) {
		if (pNv->GARTScratch)
			NVDmaSetObjectOnSubchannel(pNv, NvSubMemFormat, NvMemFormat);
	} else if (!pNv->useEXA) {
		NVDmaSetObjectOnSubchannel(pNv, NvSubClipRectangle, NvClipRectangle);
		NVDmaSetObjectOnSubchannel(pNv, NvSubSolidLine, NvSolidLine);
	}

	switch(pNv->CurrentLayout.depth) {
	case 24:
		surfaceFormat = SURFACE_FORMAT_X8R8G8B8;
		patternFormat = PATTERN_FORMAT_DEPTH24;
		rectFormat = RECT_FORMAT_DEPTH24;
		lineFormat = LINE_FORMAT_DEPTH24;
		break;
	case 16:
	case 15:
		surfaceFormat = SURFACE_FORMAT_R5G6B5;
		patternFormat = PATTERN_FORMAT_DEPTH16;
		rectFormat = RECT_FORMAT_DEPTH16;
		lineFormat = LINE_FORMAT_DEPTH16;
		break;
	default:
		surfaceFormat = SURFACE_FORMAT_Y8;
		patternFormat = PATTERN_FORMAT_DEPTH8;
		rectFormat = RECT_FORMAT_DEPTH8;
		lineFormat = LINE_FORMAT_DEPTH8;
		break;
	}

	NVDmaStart(pNv, NvSubContextSurfaces, SURFACE_FORMAT, 4);
	NVDmaNext (pNv, surfaceFormat);
	NVDmaNext (pNv, pitch | (pitch << 16));
	NVDmaNext (pNv, (uint32_t)pNv->FB->offset);
	NVDmaNext (pNv, (uint32_t)pNv->FB->offset);

	NVDmaStart(pNv, NvSubImagePattern, PATTERN_FORMAT, 1);
	NVDmaNext (pNv, patternFormat);

	NVDmaStart(pNv, NvSubRectangle, RECT_FORMAT, 1);
	NVDmaNext (pNv, rectFormat);

	if (!pNv->useEXA) {
		NVDmaStart(pNv, NvSubSolidLine, LINE_FORMAT, 1);
		NVDmaNext (pNv, lineFormat);
	}

	pNv->currentRop = ~0;  /* set to something invalid */
	NVSetRopSolid(pScrn, GXcopy, ~0);

	pNv->M2MFDirection = -1; /* invalid */
	/*NVDmaKickoff(pNv);*/
}

Bool NVDmaCreateContextObject(NVPtr pNv, int handle, int class)
{
	struct drm_nouveau_grobj_alloc cto;
	int ret;

	cto.channel = pNv->fifo.channel;
	cto.handle  = handle;
	cto.class   = class;
	ret = drmCommandWrite(pNv->drm_fd, DRM_NOUVEAU_GROBJ_ALLOC,
					   &cto, sizeof(cto));
	return ret == 0;
}

static void NVInitDmaCB(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	unsigned int cb_location;
	int cb_size;
	char *s;

	/* I'm not bothering to check for failures here, the DRM will fall back
	 * on defaults if anything's wrong (ie. out of AGP, invalid sizes)
	 */
#ifndef __powerpc__
	if (pNv->GARTScratch)
		cb_location = NOUVEAU_MEM_AGP | NOUVEAU_MEM_PCI_ACCEPTABLE;
	else
#endif
	cb_location = NOUVEAU_MEM_FB;
	if((s = (char *)xf86GetOptValString(pNv->Options, OPTION_CMDBUF_LOCATION))) {
		if(!xf86NameCmp(s, "AGP"))
			cb_location = NOUVEAU_MEM_AGP;
		else if (!xf86NameCmp(s, "VRAM"))
			cb_location = NOUVEAU_MEM_FB;
		else if (!xf86NameCmp(s, "PCI"))
			cb_location = NOUVEAU_MEM_PCI;
		else
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Invalid value \"%s\" for CBLocation\n", s);
	}
	NVDRMSetParam(pNv, NOUVEAU_SETPARAM_CMDBUF_LOCATION, cb_location);

	/* CBSize == size of space reserved for *all* FIFOs in MiB */
	if (xf86GetOptValInteger(pNv->Options, OPTION_CMDBUF_SIZE, &cb_size))
		NVDRMSetParam(pNv, NOUVEAU_SETPARAM_CMDBUF_SIZE, (cb_size<<20));
}

Bool NVInitDma(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	int i, ret;

	NVInitDmaCB(pScrn);

	if (pNv->NoAccel)
		return TRUE;

	pNv->fifo.fb_ctxdma_handle = NvDmaFB;
	pNv->fifo.tt_ctxdma_handle = NvDmaTT;
	ret = drmCommandWriteRead(pNv->drm_fd, DRM_NOUVEAU_FIFO_ALLOC,
				  &pNv->fifo, sizeof(pNv->fifo));
	if (ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Could not allocate GPU channel: %d\n", ret);
		return FALSE;
	}

	ret = drmMap(pNv->drm_fd, pNv->fifo.cmdbuf, pNv->fifo.cmdbuf_size,
		     (drmAddressPtr)&pNv->dmaBase);
	if (ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to map DMA push buffer: %d\n", ret);
		return FALSE;
	}

	ret = drmMap(pNv->drm_fd, pNv->fifo.ctrl, pNv->fifo.ctrl_size,
		     (drmAddressPtr)&pNv->FIFO);
	if (ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to map FIFO control regs: %d\n", ret);
		return FALSE;
	}

	ret = drmMap(pNv->drm_fd, pNv->fifo.notifier, pNv->fifo.notifier_size,
		     (drmAddressPtr)&pNv->NotifierBlock);
	if (ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to map notifier block: %d\n", ret);
		return FALSE;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Using FIFO channel %d\n", pNv->fifo.channel);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "  Control registers : %p (0x%08x)\n",
		   pNv->FIFO, pNv->fifo.ctrl);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "  DMA command buffer: %p (0x%08x)\n",
		   pNv->dmaBase, pNv->fifo.cmdbuf);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "  DMA cmdbuf length : %d KiB\n",
		   pNv->fifo.cmdbuf_size / 1024);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "  DMA base PUT      : 0x%08x\n", pNv->fifo.put_base);

	pNv->dmaPut = pNv->dmaCurrent = READ_GET(pNv);
	pNv->dmaMax = (pNv->fifo.cmdbuf_size >> 2) - 1;
	pNv->dmaFree = pNv->dmaMax - pNv->dmaCurrent;

	for (i=0; i<SKIPS; i++)
		NVDmaNext(pNv,0);
	pNv->dmaFree -= SKIPS;

	return TRUE;
}

