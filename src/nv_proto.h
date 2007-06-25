/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_proto.h,v 1.11 2004/03/20 01:52:16 mvojkovi Exp $ */

#ifndef __NV_PROTO_H__
#define __NV_PROTO_H__

/* in nv_accel_common.c */
Bool NVAccelCommonInit(ScrnInfoPtr pScrn);
uint32_t NVAccelGetPixmapOffset(PixmapPtr pPix);
Bool NVAccelGetCtxSurf2DFormatFromPixmap(PixmapPtr pPix, int *fmt_ret);
Bool NVAccelGetCtxSurf2DFormatFromPicture(PicturePtr pPix, int *fmt_ret);
Bool NVAccelSetCtxSurf2D(PixmapPtr psPix, PixmapPtr pdPix, int fmt);

/* in nv_driver.c */
Bool   NVSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
void   NVAdjustFrame(int scrnIndex, int x, int y, int flags);
Bool   NVI2CInit(ScrnInfoPtr pScrn);

/* in nv_mem.c */
NVAllocRec *NVAllocateMemory(NVPtr pNv, int type, int size);
void        NVFreeMemory(NVPtr pNv, NVAllocRec *mem);

/* in nv_notifier.c */
drm_nouveau_notifier_alloc_t *NVNotifierAlloc(ScrnInfoPtr, uint32_t handle);
void        NVNotifierDestroy(ScrnInfoPtr, drm_nouveau_notifier_alloc_t *);
void        NVNotifierReset(ScrnInfoPtr, drm_nouveau_notifier_alloc_t *);
uint32_t    NVNotifierStatus(ScrnInfoPtr, drm_nouveau_notifier_alloc_t *);
uint32_t    NVNotifierErrorCode(ScrnInfoPtr, drm_nouveau_notifier_alloc_t *);
uint32_t    NVNotifierReturnVal(ScrnInfoPtr, drm_nouveau_notifier_alloc_t *);
Bool        NVNotifierWaitStatus(ScrnInfoPtr, drm_nouveau_notifier_alloc_t *,
				 uint32_t status, uint32_t timeout);

/* in nv_dri.c */
unsigned int NVDRMGetParam(NVPtr pNv, unsigned int param);
Bool NVDRMSetParam(NVPtr pNv, unsigned int param, unsigned int value);
Bool NVDRIScreenInit(ScrnInfoPtr pScrn);
Bool NVDRIFinishScreenInit(ScrnInfoPtr pScrn);
extern const char *drmSymbols[], *driSymbols[];
Bool NVDRIGetVersion(ScrnInfoPtr pScrn);

/* in nv_video.c */
void NVInitVideo(ScreenPtr);
void NVResetVideo (ScrnInfoPtr pScrnInfo);

/* in nv_setup.c */
void   RivaEnterLeave(ScrnInfoPtr pScrn, Bool enter);
void   NVCommonSetup(ScrnInfoPtr pScrn);

/* in nv_cursor.c */
Bool   NVCursorInit(ScreenPtr pScreen);

/* in nv_dma.c */
void  NVDmaKickoff(NVPtr pNv);
void  NVDmaKickoffCallback(NVPtr pNv);
void  NVDmaWait(ScrnInfoPtr pScrn, int size);
void  NVSync(ScrnInfoPtr pScrn);
void  NVResetGraphics(ScrnInfoPtr pScrn);
Bool  NVDmaCreateContextObject(NVPtr pNv, int handle, int class);
Bool  NVInitDma(ScrnInfoPtr pScrn);

/* in nv_xaa.c */
Bool   NVXaaInit(ScreenPtr pScreen);
void   NVWaitVSync(ScrnInfoPtr pScrn);
void   NVSetRopSolid(ScrnInfoPtr pScrn, CARD32 rop, CARD32 planemask);

/* in nv_exa.c */
Bool NVExaInit(ScreenPtr pScreen);

/* in riva_hw.c */
void NVCalcStateExt(NVPtr,struct _riva_hw_state *,int,int,int,int,int,int);
void NVLoadStateExt(ScrnInfoPtr pScrn,struct _riva_hw_state *);
void NVUnloadStateExt(NVPtr,struct _riva_hw_state *);
void NVSetStartAddress(NVPtr,CARD32);
uint8_t nvReadVGA(NVPtr pNv, uint8_t index);
void nvWriteVGA(NVPtr pNv, uint8_t index, uint8_t data);
void nvWriteRAMDAC(NVPtr pNv, uint8_t head, uint32_t ramdac_reg, CARD32 val);
CARD32 nvReadRAMDAC(NVPtr pNv, uint8_t head, uint32_t ramdac_reg);
void nvWriteCRTC(NVPtr pNv, uint8_t head, uint32_t reg, CARD32 val);
CARD32 nvReadCRTC(NVPtr pNv, uint8_t head, uint32_t reg);

/* in nv_shadow.c */
void NVRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
void NVRefreshArea8(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
void NVRefreshArea16(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
void NVRefreshArea32(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
void NVPointerMoved(int index, int x, int y);

/* in nv_bios.c */
unsigned int NVParseBios(ScrnInfoPtr pScrn);

void nForceUpdateArbitrationSettings (unsigned      VClk,  unsigned      pixelDepth,
				      unsigned     *burst, unsigned     *lwm,
				      NVPtr        pNv);


/* nv_crtc.c */
Bool NVSetMode(ScrnInfoPtr pScrn, DisplayModePtr pMode, Rotation rotation);
Bool NVCrtcSetMode(xf86CrtcPtr crtc, DisplayModePtr pMode, Rotation rotation, int x, int y);
DisplayModePtr NVCrtcFindClosestMode(xf86CrtcPtr crtc, DisplayModePtr pMode);
void NVCrtcSetBase (xf86CrtcPtr crtc, int x, int y);
void NVCrtcLoadPalette(xf86CrtcPtr crtc);
void NVCrtcBlankScreen(xf86CrtcPtr crtc, Bool on);

/* nv_hw.c */
void nForceUpdateArbitrationSettings (unsigned VClk, unsigned pixelDepth,
				      unsigned     *burst, unsigned     *lwm,
				      NVPtr        pNv);
void nv30UpdateArbitrationSettings (NVPtr        pNv,
				    unsigned     *burst,
				    unsigned     *lwm);
void nv10UpdateArbitrationSettings (unsigned      VClk, 
				    unsigned      pixelDepth, 
				    unsigned     *burst,
				    unsigned     *lwm,
				    NVPtr        pNv);
void nv4UpdateArbitrationSettings (unsigned      VClk, 
				   unsigned      pixelDepth, 
				   unsigned     *burst,
				   unsigned     *lwm,
				   NVPtr        pNv);

void NVInitSurface(ScrnInfoPtr pScrn, RIVA_HW_STATE *state);
void NVInitGraphContext(ScrnInfoPtr pScrn);

/* nv_i2c.c */
Bool NV_I2CInit(ScrnInfoPtr pScrn, I2CBusPtr *bus_ptr, int i2c_reg, char *name);

/* in nv30_exa.c */
Bool NVAccelInitNV40TCL(ScrnInfoPtr pScrn);
Bool NV30EXACheckComposite(int, PicturePtr, PicturePtr, PicturePtr);
Bool NV30EXAPrepareComposite(int, PicturePtr, PicturePtr, PicturePtr,
				  PixmapPtr, PixmapPtr, PixmapPtr);
void NV30EXAComposite(PixmapPtr, int, int, int, int, int, int, int, int);
void NV30EXADoneComposite(PixmapPtr);

#endif /* __NV_PROTO_H__ */

