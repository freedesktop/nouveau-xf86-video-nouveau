#include "nv_include.h"

#define _XF86DRI_SERVER_
#include "GL/glxint.h"
#include "GL/glxtokens.h"
#include "sarea.h"
#include "xf86drm.h"
#include "dri.h"
#include "nv_dripriv.h"
#include "nv_dri.h"

Bool NVDRMSetParam(NVPtr pNv, unsigned int param, unsigned int value)
{
	drm_nouveau_setparam_t setparam;
	int ret;

	setparam.param = param;
	setparam.value = value;
	ret = drmCommandWriteRead(pNv->drm_fd, DRM_NOUVEAU_SETPARAM, &setparam,
			sizeof(setparam));
	if (ret)
		return FALSE;
	return TRUE;
}

unsigned int NVDRMGetParam(NVPtr pNv, unsigned int param)
{
	drm_nouveau_getparam_t getparam;

	getparam.param = param;
	drmCommandWriteRead(pNv->drm_fd, DRM_NOUVEAU_GETPARAM, &getparam, sizeof(getparam));

	return getparam.value;
}

static Bool NVCreateContext(ScreenPtr pScreen, VisualPtr visual,
		drm_context_t hwContext, void *pVisualConfigPriv,
		DRIContextType contextStore)
{
	return TRUE;
}


static void NVDestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
		DRIContextType contextStore)
{
	return;
}

static void NVDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
		DRIContextType oldContextType,
		void *oldContext,
		DRIContextType newContextType,
		void *newContext)
{
	/* we really should do something here */
	return;
}

static void NVDRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 indx)
{   
	return;
}

static void NVDRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
		RegionPtr prgnSrc, CARD32 indx)
{
	return;
}	

static void NVDRITransitionTo2d(ScreenPtr pScreen)
{
	return;
}

static void NVDRITransitionTo3d(ScreenPtr pScreen)
{
	return;
}

static void NVDRITransitionSingleToMulti3d(ScreenPtr pScreen)
{           
	return;
}           
        
static void NVDRITransitionMultiToSingle3d(ScreenPtr pScreen)
{       
	return;
}

static Bool NVDRIInitVisualConfigs(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn=xf86Screens[pScreen->myNum];
	__GLXvisualConfig* pConfigs = NULL;
	NVConfigPrivPtr pNVConfigs = NULL;
	NVConfigPrivPtr* pNVConfigPtrs = NULL;
	int db,depth,alpha,stencil;
	int depths[]={24,16,0};
	int num_configs,i;

	switch(pScrn->depth)
	{
		case 8:
		case 15:
			xf86DrvMsg(pScreen->myNum, X_ERROR, "[dri] no DRI at %d bpp ",pScrn->depth);
			break;
		case 16:
		case 24:
			num_configs=2*3*((pScrn->depth==24)?2:1)*2; /* db*depth*alpha*stencil */
			if (!(pConfigs=(__GLXvisualConfig*)xcalloc(sizeof(__GLXvisualConfig),num_configs)))
				return FALSE;
			if (!(pNVConfigs=(NVConfigPrivPtr)xcalloc(sizeof(NVConfigPrivRec), num_configs))) {
				xfree(pConfigs);
				return FALSE;
			}
			if (!(pNVConfigPtrs=(NVConfigPrivPtr *)xcalloc(sizeof(NVConfigPrivPtr),num_configs))) {
				xfree(pConfigs);
				xfree(pNVConfigs);
				return FALSE;
			}

			i = 0;
			for(db=1;db>=0;db--)
			for(depth=0;depth<3;depth++)
			for(alpha=0;alpha<((pScrn->depth==24)?2:1);alpha++)
			for(stencil=0;stencil<2;stencil++)
			{
				pConfigs[i].vid                = (VisualID)(-1);
				pConfigs[i].class              = -1;
				pConfigs[i].rgba               = TRUE;
				if (pScrn->depth==16)
				{					
					pConfigs[i].redSize            = 5;
					pConfigs[i].greenSize          = 6;
					pConfigs[i].blueSize           = 5;
					pConfigs[i].alphaSize          = 0;
					pConfigs[i].redMask            = 0x0000F800;
					pConfigs[i].greenMask          = 0x000007E0;
					pConfigs[i].blueMask           = 0x0000001F;
					pConfigs[i].alphaMask          = 0x00000000;
				} else {
					pConfigs[i].redSize            = 8;
					pConfigs[i].greenSize          = 8;
					pConfigs[i].blueSize           = 8;
					pConfigs[i].redMask            = 0x00FF0000;
					pConfigs[i].greenMask          = 0x0000FF00;
					pConfigs[i].blueMask           = 0x000000FF;
					if (alpha) {
						pConfigs[i].alphaSize          = 8;
						pConfigs[i].alphaMask          = 0xFF000000;
					} else {
						pConfigs[i].alphaSize          = 0;
						pConfigs[i].alphaMask          = 0x00000000;
					}
				}

				pConfigs[i].accumRedSize   = 0;
				pConfigs[i].accumGreenSize = 0;
				pConfigs[i].accumBlueSize  = 0;
				pConfigs[i].accumAlphaSize = 0;
				if (db)
					pConfigs[i].doubleBuffer   = TRUE;
				else
					pConfigs[i].doubleBuffer   = FALSE;
				pConfigs[i].stereo             = FALSE;
				pConfigs[i].bufferSize         = pScrn->depth;
				if (depths[depth] == 24 && stencil) {
					pConfigs[i].depthSize          = depths[depth];
					pConfigs[i].stencilSize        = 8;
				} else {
					pConfigs[i].depthSize          = depths[depth];
					pConfigs[i].stencilSize        = 0;
				}
				pConfigs[i].auxBuffers         = 0;
				pConfigs[i].level              = 0;
				pConfigs[i].visualRating       = GLX_NONE;
				pConfigs[i].transparentPixel   = GLX_NONE;
				pConfigs[i].transparentRed     = 0;
				pConfigs[i].transparentGreen   = 0;
				pConfigs[i].transparentBlue    = 0;
				pConfigs[i].transparentAlpha   = 0;
				pConfigs[i].transparentIndex   = 0;
				i++;
			}
			break;
	}
	GlxSetVisualConfigs(num_configs, pConfigs, (void**)pNVConfigPtrs);
	return TRUE;
}

Bool NVDRIGetVersion(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	char *busId;
	int fd;

	{
		pointer ret;
		int errmaj, errmin;

		ret = LoadSubModule(pScrn->module, "dri", NULL, NULL, NULL,
				    NULL, &errmaj, &errmin);
		if (!ret) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,
					"error %d\n", errmaj);
			LoaderErrorMsg(pScrn->name, "dri", errmaj, errmin);
		}

		if (!ret && errmaj != LDR_ONCEONLY)
			return FALSE;
	}

	xf86LoaderReqSymLists(drmSymbols, NULL);
	xf86LoaderReqSymLists(driSymbols, NULL);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Loaded DRI module\n");

	busId = DRICreatePCIBusID(pNv->PciInfo);

	fd = drmOpen("nouveau", busId);
	xfree(busId);
	if (fd < 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"[dri] Failed to open the DRM\n");
		return FALSE;
	}

	/* Check the lib version */
	if (xf86LoaderCheckSymbol("drmGetLibVersion"))
		pNv->pLibDRMVersion = drmGetLibVersion(0);
	if (pNv->pLibDRMVersion == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"NVDRIGetVersion failed because libDRM is really "
		"way to old to even get a version number out of it.\n"
		"[dri] Disabling DRI.\n");
		return FALSE;
	}

	pNv->pKernelDRMVersion = drmGetVersion(fd);
	drmClose(fd);
	if (pNv->pKernelDRMVersion == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"failed to get DRM version\n");
		return FALSE;
	}
	
	/* temporary lock step versioning */
#if NOUVEAU_DRM_HEADER_PATCHLEVEL != 5
#error nouveau_drm.h doesn't match expected patchlevel, update libdrm.
#endif
	if (pNv->pKernelDRMVersion->version_patchlevel !=
			NOUVEAU_DRM_HEADER_PATCHLEVEL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"wrong DRM version\n");
		return FALSE;
	}

	return TRUE;
}

Bool NVDRIScreenInit(ScrnInfoPtr pScrn)
{
	DRIInfoPtr     pDRIInfo;
	NOUVEAUDRIPtr  pNOUVEAUDRI;
	NVPtr pNv = NVPTR(pScrn);
	ScreenPtr pScreen;
	pScreen = screenInfo.screens[pScrn->scrnIndex];
	int drm_page_size;
	int irq;

	drm_page_size = getpagesize();
	if (!(pDRIInfo = DRICreateInfoRec())) return FALSE;
	
	pNv->pDRIInfo                        = pDRIInfo;
	pDRIInfo->drmDriverName              = "nouveau";
	pDRIInfo->clientDriverName           = "nouveau";
	pDRIInfo->busIdString                = DRICreatePCIBusID(pNv->PciInfo);

	pDRIInfo->ddxDriverMajorVersion      = NV_MAJOR_VERSION;
	pDRIInfo->ddxDriverMinorVersion      = NV_MINOR_VERSION;
	pDRIInfo->ddxDriverPatchVersion      = NV_PATCHLEVEL;

	/* FIXME: this gets passed to drmMap somewhere, and will currently fail if
	 *        the pNv->FB is not where we specify here.. Somehow we need to be
	 *        able to alloc pNv->FB before doing DRIScreenInit, or maybe we just
	 *        need a map of the entire framebuffer added to the drm.. I don't know :)
	 *
	 *        At the moment the setup here will probably only work if using an AGP cmdbuf..
	 */
	pDRIInfo->frameBufferPhysicalAddress = (void *)pNv->VRAMPhysical;
	pDRIInfo->frameBufferSize            = pNv->VRAMPhysicalSize / 2;
	pDRIInfo->frameBufferStride          = pScrn->displayWidth * pScrn->bitsPerPixel/8;

	pDRIInfo->ddxDrawableTableEntry      = 1;
	pDRIInfo->maxDrawableTableEntry      = 1;

	if (!(pNOUVEAUDRI = (NOUVEAUDRIPtr)xcalloc(sizeof(NOUVEAUDRIRec), 1))) {
		DRIDestroyInfoRec(pDRIInfo);
		pNv->pDRIInfo = NULL;
		return FALSE;
	}
	pDRIInfo->devPrivate                 = pNOUVEAUDRI; 
	pDRIInfo->devPrivateSize             = sizeof(NOUVEAUDRIRec);
	pDRIInfo->contextSize                = sizeof(NVDRIContextRec);
	pDRIInfo->SAREASize                  = (drm_page_size > SAREA_MAX) ? drm_page_size : SAREA_MAX;

	pDRIInfo->CreateContext              = NVCreateContext;
	pDRIInfo->DestroyContext             = NVDestroyContext;
	pDRIInfo->SwapContext                = NVDRISwapContext;
	pDRIInfo->InitBuffers                = NVDRIInitBuffers;
	pDRIInfo->MoveBuffers                = NVDRIMoveBuffers;
	pDRIInfo->bufferRequests             = DRI_ALL_WINDOWS;
	pDRIInfo->TransitionTo2d             = NVDRITransitionTo2d;
	pDRIInfo->TransitionTo3d             = NVDRITransitionTo3d;
	pDRIInfo->TransitionSingleToMulti3D  = NVDRITransitionSingleToMulti3d;
	pDRIInfo->TransitionMultiToSingle3D  = NVDRITransitionMultiToSingle3d;

	pDRIInfo->createDummyCtx     = FALSE;
	pDRIInfo->createDummyCtxPriv = FALSE;

	if (!DRIScreenInit(pScreen, pDRIInfo, &pNv->drm_fd)) {
		xf86DrvMsg(pScreen->myNum, X_ERROR,
				"[dri] DRIScreenInit failed.  Disabling DRI.\n");
		xfree(pDRIInfo->devPrivate);
		pDRIInfo->devPrivate = NULL;
		DRIDestroyInfoRec(pDRIInfo);
		pDRIInfo = NULL;
		return FALSE;
	}
	if (!NVDRIInitVisualConfigs(pScreen)) {
		xf86DrvMsg(pScreen->myNum, X_ERROR,
				"[dri] NVDRIInitVisualConfigs failed.  Disabling DRI.\n");
		xfree(pDRIInfo->devPrivate);
		pDRIInfo->devPrivate = NULL;
		DRIDestroyInfoRec(pDRIInfo);
		pDRIInfo = NULL;
		return FALSE;
	}

#if 0
	pNv->IRQ = 0;
#else
	/* Ask DRM to install IRQ handler */
	irq = drmGetInterruptFromBusID(pNv->drm_fd,
			((pciConfigPtr)pNv->PciInfo->thisCard)->busnum,
			((pciConfigPtr)pNv->PciInfo->thisCard)->devnum,
			((pciConfigPtr)pNv->PciInfo->thisCard)->funcnum);

	if (drmCtlInstHandler(pNv->drm_fd, irq)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to install IRQ handler\n");
		pNv->IRQ = 0;
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "IRQ handler initialised.  IRQ %d\n", irq);
		pNv->IRQ = irq;
	}
#endif

	return TRUE;
}

Bool NVDRIFinishScreenInit(ScrnInfoPtr pScrn)
{
	ScreenPtr      pScreen = screenInfo.screens[pScrn->scrnIndex];
	NVPtr          pNv = NVPTR(pScrn);
	NOUVEAUDRIPtr  pNOUVEAUDRI;
	int cpp = pScrn->bitsPerPixel/8;

	if (!DRIFinishScreenInit(pScreen)) {
		return FALSE;
	}

	pNOUVEAUDRI 			= (NOUVEAUDRIPtr)pNv->pDRIInfo->devPrivate;

	pNOUVEAUDRI->device_id		= pNv->Chipset;

	pNOUVEAUDRI->width		= pScrn->virtualX;
	pNOUVEAUDRI->height		= pScrn->virtualY;
	pNOUVEAUDRI->depth		= pScrn->depth;
	pNOUVEAUDRI->bpp		= pScrn->bitsPerPixel;

	pNOUVEAUDRI->front_offset 	= pNv->FB->offset - pNv->VRAMPhysical;
	pNOUVEAUDRI->front_pitch	= pScrn->virtualX;
	/* back/depth buffers will likely be allocated on a per-drawable
	 * basis, but these may be useful if we want to support shared back
	 * buffers at some point.
	 */
	pNOUVEAUDRI->back_offset	= 0;
	pNOUVEAUDRI->back_pitch		= 0;
	pNOUVEAUDRI->depth_offset	= 0;
	pNOUVEAUDRI->depth_pitch	= 0;

	return TRUE;
}

