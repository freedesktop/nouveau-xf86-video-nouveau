/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_proto.h,v 1.11 2004/03/20 01:52:16 mvojkovi Exp $ */

#ifndef __NV_PROTO_H__
#define __NV_PROTO_H__

/* in nv_driver.c */
Bool   NVSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
void   NVAdjustFrame(int scrnIndex, int x, int y, int flags);
Bool   NVI2CInit(ScrnInfoPtr pScrn);
NVAllocRec *NVAllocateMemory(NVPtr pNv, int type, int size);
void        NVFreeMemory(NVPtr pNv, NVAllocRec *mem);

/* in nv_dri.c */
unsigned int NVDRMGetParam(NVPtr pNv, unsigned int param);
Bool NVDRMSetParam(NVPtr pNv, unsigned int param, unsigned int value);
Bool NVInitAGP(ScrnInfoPtr pScrn);
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
void  NVDmaWait(NVPtr pNv, int size);
void  NVDoSync(NVPtr pNv);
void  NVSync(ScrnInfoPtr pScrn);
void  NVResetGraphics(ScrnInfoPtr pScrn);
Bool  NVDmaCreateDMAObject(NVPtr pNv, int handle, int target,
			   CARD32 base_address, CARD32 size, int access);
NVAllocRec *NVDmaCreateNotifier(NVPtr pNv, int handle);
Bool  NVDmaWaitForNotifier(NVPtr pNv, void *notifier);
Bool  NVDmaCreateContextObject(NVPtr pNv, int handle, int class, CARD32 flags,
			       CARD32 dma_in, CARD32 dma_out,
			       CARD32 dma_notifier);
Bool  NVInitDma(ScrnInfoPtr pScrn);

/* in nv_xaa.c */
Bool   NVXaaInit(ScreenPtr pScreen);
void   NVWaitVSync(NVPtr pNv);
void   NVSetRopSolid(ScrnInfoPtr pScrn, CARD32 rop, CARD32 planemask);

/* in nv_exa.c */
Bool NVExaInit(ScreenPtr pScreen);

/* in nv30_exa.c */
Bool NV30EXAPreInit(ScrnInfoPtr pScrn);
void NV30EXAResetGraphics(NVPtr pNv);
void NV30EXAInstallHooks(NVPtr pNv);

/* in riva_hw.c */
void NVCalcStateExt(NVPtr,struct _riva_hw_state *,int,int,int,int,int,int);
void NVLoadStateExt(ScrnInfoPtr pScrn,struct _riva_hw_state *);
void NVUnloadStateExt(NVPtr,struct _riva_hw_state *);
void NVSetStartAddress(NVPtr,CARD32);
int  NVShowHideCursor(NVPtr,int);
void NVLockUnlock(NVPtr,int);
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
Bool NVSetMode(ScrnInfoPtr pScrn, DisplayModePtr pMode);
Bool NVCrtcSetMode(xf86CrtcPtr crtc, DisplayModePtr pMode);
Bool NVCrtcInUse (xf86CrtcPtr crtc);
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
void NVInitGraphContext(ScrnInfoPtr pScrn, RIVA_HW_STATE *state);

/* nv_i2c.c */
Bool NV_I2CInit(ScrnInfoPtr pScrn, I2CBusPtr *bus_ptr, int i2c_reg, char *name);

#endif /* __NV_PROTO_H__ */

