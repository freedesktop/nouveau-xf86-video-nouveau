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

void NVDmaWait (NVPtr pNv, int size)
{
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
							NVDoSync(pNv);
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
			NVDoSync(pNv);
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

void NVDoSync(NVPtr pNv)
{
	int t_start, timeout = 2000;

	if(pNv->DMAKickoffCallback)
		(*pNv->DMAKickoffCallback)(pNv);

	t_start = GetTimeInMillis();
	/* Wait for entire FIFO to be processed */
	while((GetTimeInMillis() - t_start) < timeout && (READ_GET(pNv) != pNv->dmaPut));
	/* Wait for PGRAPH to go completely idle */
	while((GetTimeInMillis() - t_start) < timeout && pNv->PGRAPH[NV_PGRAPH_STATUS/4]);

	if ((GetTimeInMillis() - t_start) >= timeout) {
		if (pNv->LockedUp)
			return;
		NVDumpLockupInfo(pNv);
		pNv->LockedUp = TRUE; /* avoid re-entering FatalError on shutdown */
		FatalError("DMA queue hang: dmaPut=%x, current=%x, status=%x\n",
				pNv->dmaPut, READ_GET(pNv), pNv->PGRAPH[NV_PGRAPH_STATUS/4]);
	}
}

void NVSync(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	if(pNv->NoAccel) return;
	NVDoSync(pNv);
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
		NVDmaWait(pNv, SKIPS); 
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
		if (pNv->AGPScratch)
			NVDmaSetObjectOnSubchannel(pNv, NvSubMemFormat, NvMemFormat);
		if (pNv->use3D) {
			NVDmaSetObjectOnSubchannel(pNv, NvSub3D, Nv3D);
			pNv->Reset3D(pNv);
		}
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
	NVDmaNext (pNv, (CARD32)(pNv->FB->offset - pNv->VRAMPhysical));
	NVDmaNext (pNv, (CARD32)(pNv->FB->offset - pNv->VRAMPhysical));

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

Bool NVDmaCreateDMAObject(NVPtr pNv, uint32_t handle, int class,
				     int target,
				     CARD32 offset, CARD32 size, int access)
{
	drm_nouveau_dma_object_init_t dma;
	int ret;

	dma.channel = pNv->fifo.channel;
	dma.handle  = handle;
	dma.class   = class;
	dma.access  = access;
	dma.target  = target;
	dma.size    = size;
	dma.offset  = offset;
	ret = drmCommandWrite(pNv->drm_fd, DRM_NOUVEAU_DMA_OBJECT_INIT,
					   &dma, sizeof(dma));

	return ret == 0;
}

Bool NVDmaCreateDMAObjectFromMem(NVPtr pNv, uint32_t handle, int class,
					    NVAllocRec *mem, int access)
{
	uint32_t offset = mem->offset;
	int      target;

	target = mem->type & (NOUVEAU_MEM_FB | NOUVEAU_MEM_AGP);
	if (!target)
		return FALSE;

	if (target & NOUVEAU_MEM_FB)
		offset -= pNv->VRAMPhysical;
	else if (target & NOUVEAU_MEM_AGP)
		offset -= pNv->AGPPhysical;

	return NVDmaCreateDMAObject(pNv, handle, class, target,
					 offset, mem->size, access);
}

/*
A DMA notifier is a DMA object that references a small (32 byte it
seems, we use 256 for saftey) memory area that will be used by the HW to give feedback
about a DMA operation.
*/
NVAllocRec *NVDmaCreateNotifier(NVPtr pNv, int handle)
{
	NVAllocRec *notifier = NULL;

#ifndef __powerpc__
	notifier = NVAllocateMemory(pNv, NOUVEAU_MEM_AGP |
					 NOUVEAU_MEM_FB_ACCEPTABLE,
					 256);
#else
	notifier = NVAllocateMemory(pNv, NOUVEAU_MEM_FB, 256);
#endif

	if (!notifier)
		return NULL;

	if (!NVDmaCreateDMAObjectFromMem(pNv, handle, NV_DMA_IN_MEMORY,
					      notifier,
					      NOUVEAU_MEM_ACCESS_RW)) {
		NVFreeMemory(pNv, notifier);
		return NULL;
	}

	return notifier;
}

/* How do we wait for DMA completion (by notifiers) ?
 *
 * Either repeatedly read the notifier address and wait until it changes,
 * or enable a 'wakeup' interrupt by writing NOTIFY_WRITE_LE_AWAKEN into
 * the 'notify' field of the object in the channel.  My guess is that 
 * this causes an interrupt in PGRAPH/NOTIFY as soon as the transfer is
 * completed.  Clients probably can use poll on the nv* devices to get this 
 * event.  All this is a guess.  I don't know any details, and I have not
 * tested is.  Also, I have no idea how the 'nvdriver' reacts if it gets 
 * notify events that are not registered.
 *
 * Writing NV_NOTIFY_WRITE_LE_AWAKEN into the 'Notify' field of an object
 * in a channel really causes an interrupt in the PGRAPH engine.  Thus
 * we can determine whether a DMA transfer has finished in the interrupt
 * handler.
 * 
 * We can't use interrupts in user land, so we do the simple polling approach.
 * The method returns FALSE in case of an error.
 */
Bool NVDmaWaitForNotifier(NVPtr pNv, void *notifier)
{
	int t_start, timeout = 0;
	volatile CARD32 *n;

	n = (volatile CARD32 *)notifier;
	NVDEBUG("NVDmaWaitForNotifier @%p", n);
	t_start = GetTimeInMillis();
	while (1) {
		CARD32 a = n[0];
		CARD32 b = n[1];
		CARD32 c = n[2];
		CARD32 status = n[3];
		NVDEBUG("status: n[0]=%x, n[1]=%x, n[2]=%x, n[3]=%x\n", a, b, c, status);
		NVDEBUG("status: GET: 0x%08x\n", READ_GET(pNv));

		if (GetTimeInMillis() - t_start >= 2000) {
			/* We've timed out, call NVSync() to detect lockups */
			if (timeout++ == 0) {
				NVDoSync(pNv);
				/* If we're still here, wait another second for notifier.. */
				t_start = GetTimeInMillis() + 1000;
				break;
			}
			/* Still haven't recieved notification, log error */
			ErrorF("Notifier timeout\n");
			return FALSE;
		}

		if (status == 0xffffffff)
			continue;
		if (!status)
			break;
		if (status & 0xffff)
			return FALSE;
	}

	return TRUE;
}

Bool NVDmaCreateContextObject(NVPtr pNv, int handle, int class)
{
	drm_nouveau_object_init_t cto;
	int ret;

	cto.channel = pNv->fifo.channel;
	cto.handle  = handle;
	cto.class   = class;
	ret = drmCommandWrite(pNv->drm_fd, DRM_NOUVEAU_OBJECT_INIT,
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
	if (pNv->AGPScratch)
		cb_location = NOUVEAU_MEM_AGP;
	else
#endif
	cb_location = NOUVEAU_MEM_FB;
	if((s = (char *)xf86GetOptValString(pNv->Options, OPTION_CMDBUF_LOCATION))) {
		if(!xf86NameCmp(s, "AGP"))
			cb_location = NOUVEAU_MEM_AGP;
		else if (!xf86NameCmp(s, "VRAM"))
			cb_location = NOUVEAU_MEM_FB;
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
	int i;

	NVInitDmaCB(pScrn);

	if(pNv->NoAccel) return TRUE;

	if (drmCommandWriteRead(pNv->drm_fd, DRM_NOUVEAU_FIFO_ALLOC, &pNv->fifo, sizeof(pNv->fifo)) != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Could not initialise kernel module\n");
		return FALSE;
	}

	if (drmMap(pNv->drm_fd, pNv->fifo.cmdbuf, pNv->fifo.cmdbuf_size, (drmAddressPtr)&pNv->dmaBase)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to map DMA command buffer\n");
		return FALSE;
	}

	if (drmMap(pNv->drm_fd, pNv->fifo.ctrl, pNv->fifo.ctrl_size, (drmAddressPtr)&pNv->FIFO)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to map FIFO control regs\n");
		return FALSE;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using FIFO channel %d\n", pNv->fifo.channel);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  Control registers : %p (0x%08x)\n", pNv->FIFO, pNv->fifo.ctrl);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  DMA command buffer: %p (0x%08x)\n", pNv->dmaBase, pNv->fifo.cmdbuf);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  DMA cmdbuf length : %d KiB\n", pNv->fifo.cmdbuf_size / 1024);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  DMA base PUT      : 0x%08x\n", pNv->fifo.put_base);

	pNv->dmaPut = pNv->dmaCurrent = READ_GET(pNv);
	pNv->dmaMax = (pNv->fifo.cmdbuf_size >> 2) - 1;
	pNv->dmaFree = pNv->dmaMax - pNv->dmaCurrent;

	for (i=0; i<SKIPS; i++)
		NVDmaNext(pNv,0);
	pNv->dmaFree -= SKIPS;

	return TRUE;
}

