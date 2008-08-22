/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_proto.h,v 1.11 2004/03/20 01:52:16 mvojkovi Exp $ */

#ifndef __NV_PROTO_H__
#define __NV_PROTO_H__

/* in nv_accel_common.c */
Bool NVAccelCommonInit(ScrnInfoPtr pScrn);
Bool NVAccelGetCtxSurf2DFormatFromPixmap(PixmapPtr pPix, int *fmt_ret);
Bool NVAccelGetCtxSurf2DFormatFromPicture(PicturePtr pPix, int *fmt_ret);
PixmapPtr NVGetDrawablePixmap(DrawablePtr pDraw);

/* in nv_driver.c */
Bool   NVI2CInit(ScrnInfoPtr pScrn);

/* in nv_dri.c */
Bool NVDRIScreenInit(ScrnInfoPtr pScrn);
Bool NVDRIFinishScreenInit(ScrnInfoPtr pScrn);
void NVDRICloseScreen(ScrnInfoPtr pScrn);
extern const char *drmSymbols[], *driSymbols[];
Bool NVDRIGetVersion(ScrnInfoPtr pScrn);

/* in nv_dac.c */
Bool   NVDACInit(ScrnInfoPtr pScrn, DisplayModePtr mode);
void   NVDACSave(ScrnInfoPtr pScrn, vgaRegPtr vgaReg,
                 NVRegPtr nvReg, Bool saveFonts);
void   NVDACRestore(ScrnInfoPtr pScrn, vgaRegPtr vgaReg,
                    NVRegPtr nvReg, Bool restoreFonts);
void   NVDACLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
                        LOCO *colors, VisualPtr pVisual );
Bool   NVDACi2cInit(ScrnInfoPtr pScrn);

/* in nv_video.c */
void NVInitVideo(ScreenPtr);
void NVWaitVSync(ScrnInfoPtr pScrn, int crtc);
void NVSetPortDefaults (ScrnInfoPtr pScrn, NVPortPrivPtr pPriv);
unsigned int nv_window_belongs_to_crtc(ScrnInfoPtr, int, int, int, int);

/* in nv_setup.c */
void   RivaEnterLeave(ScrnInfoPtr pScrn, Bool enter);
void   NVCommonSetup(ScrnInfoPtr pScrn);

/* in nv_cursor.c */
Bool   NVCursorInit(ScreenPtr pScreen);
Bool NVCursorInitRandr12(ScreenPtr pScreen);
void nv_crtc_show_cursor(xf86CrtcPtr crtc);
void nv_crtc_hide_cursor(xf86CrtcPtr crtc);
void nv_crtc_set_cursor_position(xf86CrtcPtr crtc, int x, int y);
void nv_crtc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg);
void nv_crtc_load_cursor_image(xf86CrtcPtr crtc, CARD8 *image);
void nv_crtc_load_cursor_argb(xf86CrtcPtr crtc, CARD32 *image);
void nv_crtc_fix_nv40_hw_cursor(ScrnInfoPtr pScrn, uint8_t head);
void nv_crtc_show_hide_cursor(ScrnInfoPtr pScrn, uint8_t head, Bool show);

/* in nv_dma.c */
void  NVSync(ScrnInfoPtr pScrn);
Bool  NVInitDma(ScrnInfoPtr pScrn);

/* in nv_exa.c */
Bool NVExaInit(ScreenPtr pScreen);
Bool NVExaPixmapIsOnscreen(PixmapPtr pPixmap);

/* in nv_hw.c */
void NVCalcStateExt(NVPtr,struct _riva_hw_state *,int,int,int,int,int,int);
void NVLoadStateExt(ScrnInfoPtr pScrn,struct _riva_hw_state *);
void NVUnloadStateExt(NVPtr,struct _riva_hw_state *);
void NVSetStartAddress(NVPtr,CARD32);

/* in nv_shadow.c */
void NVRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox);

/* in nv_bios.c */
unsigned int NVParseBios(ScrnInfoPtr pScrn);
void call_lvds_script(ScrnInfoPtr pScrn, struct dcb_entry *dcbent, int head, enum LVDS_script script, int pxclk);
void parse_lvds_manufacturer_table(ScrnInfoPtr pScrn, bios_t *bios, int pxclk);
void run_tmds_table(ScrnInfoPtr pScrn, struct dcb_entry *dcbent, int head, int pxclk);
int getMNP_single(ScrnInfoPtr pScrn, struct pll_lims *pll_lim, int clk, int *NM, int *log2P);
int getMNP_double(ScrnInfoPtr pScrn, struct pll_lims *pll_lim, int clk, int *NM1, int *NM2, int *log2P);
bool get_pll_limits(ScrnInfoPtr pScrn, uint32_t limit_match, struct pll_lims *pll_lim);

/* nv_crtc.c */
void NVCrtcSetBase(xf86CrtcPtr crtc, int x, int y);
void nv_crtc_init(ScrnInfoPtr pScrn, int crtc_num);
void NVCrtcLockUnlock(xf86CrtcPtr crtc, Bool lock);

/* nv_output.c */
void NvSetupOutputs(ScrnInfoPtr pScrn);

/* nv_hw.c */
uint32_t NVRead(NVPtr pNv, uint32_t reg);
void NVWrite(NVPtr pNv, uint32_t reg, uint32_t val);
uint32_t NVReadCRTC(NVPtr pNv, int head, uint32_t reg);
void NVWriteCRTC(NVPtr pNv, int head, uint32_t reg, uint32_t val);
uint32_t NVReadRAMDAC(NVPtr pNv, int head, uint32_t reg);
void NVWriteRAMDAC(NVPtr pNv, int head, uint32_t reg, uint32_t val);
uint8_t nv_read_tmds(NVPtr pNv, int or, int dl, uint8_t address);
int nv_get_digital_bound_head(NVPtr pNv, int or);
void nv_write_tmds(NVPtr pNv, int or, int dl, uint8_t address, uint8_t data);
void NVWriteVgaCrtc(NVPtr pNv, int head, uint8_t index, uint8_t value);
uint8_t NVReadVgaCrtc(NVPtr pNv, int head, uint8_t index);
void NVWriteVgaCrtc5758(NVPtr pNv, int head, uint8_t index, uint8_t value);
uint8_t NVReadVgaCrtc5758(NVPtr pNv, int head, uint8_t index);
uint8_t NVReadPVIO(NVPtr pNv, int head, uint16_t port);
void NVWritePVIO(NVPtr pNv, int head, uint16_t port, uint8_t value);
void NVWriteVgaSeq(NVPtr pNv, int head, uint8_t index, uint8_t value);
uint8_t NVReadVgaSeq(NVPtr pNv, int head, uint8_t index);
void NVWriteVgaGr(NVPtr pNv, int head, uint8_t index, uint8_t value);
uint8_t NVReadVgaGr(NVPtr pNv, int head, uint8_t index);
void NVSetEnablePalette(NVPtr pNv, int head, bool enable);
void NVWriteVgaAttr(NVPtr pNv, int head, uint8_t index, uint8_t value);
uint8_t NVReadVgaAttr(NVPtr pNv, int head, uint8_t index);
void NVVgaSeqReset(NVPtr pNv, int head, bool start);
void NVVgaProtect(NVPtr pNv, int head, bool protect);
void NVSetOwner(ScrnInfoPtr pScrn, int head);
void NVLockVgaCrtc(NVPtr pNv, int head, bool lock);
void NVBlankScreen(ScrnInfoPtr pScrn, int head, bool blank);
int nv_decode_pll_highregs(NVPtr pNv, uint32_t pll1, uint32_t pll2, bool force_single, int refclk);
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
uint32_t nv_pitch_align(NVPtr pNv, uint32_t width, int bpp);
void nv_save_restore_vga_fonts(ScrnInfoPtr pScrn, bool save);

/* in nv_i2c.c */
Bool NV_I2CInit(ScrnInfoPtr pScrn, I2CBusPtr *bus_ptr, int i2c_reg, char *name);

/* in nv04_video_overlay.c */
void NV04PutOverlayImage(ScrnInfoPtr, struct nouveau_bo *, int, int, int,
			 BoxPtr, int, int, int, int, short, short, short,
			 short, short, short, RegionPtr clipBoxes);
int NV04SetOverlayPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
int NV04GetOverlayPortAttribute(ScrnInfoPtr, Atom, INT32 *, pointer);
void NV04StopOverlay(ScrnInfoPtr);

/* in nv04_video_blitter.c */
void NVPutBlitImage(ScrnInfoPtr, struct nouveau_bo *, int, int, int, BoxPtr,
		    int, int, int, int, short, short, short, short, short,
		    short, RegionPtr, PixmapPtr);
int NVSetBlitPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
int NVGetBlitPortAttribute(ScrnInfoPtr, Atom, INT32 *, pointer);
void NVStopBlitVideo(ScrnInfoPtr, pointer, Bool);

/* in nv10_exa.c */
Bool NVAccelInitNV10TCL(ScrnInfoPtr pScrn);
Bool NV10CheckComposite(int, PicturePtr, PicturePtr, PicturePtr);
Bool NV10PrepareComposite(int, PicturePtr, PicturePtr, PicturePtr,
				  PixmapPtr, PixmapPtr, PixmapPtr);
void NV10Composite(PixmapPtr, int, int, int, int, int, int, int, int);
void NV10DoneComposite(PixmapPtr);

/* in nv10_video_overlay.c */
void NV10PutOverlayImage(ScrnInfoPtr, struct nouveau_bo *, int, int, int, int,
			 BoxPtr, int, int, int, int, short, short, short,
			 short, short, short, RegionPtr clipBoxes);
int NV10SetOverlayPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
int NV10GetOverlayPortAttribute(ScrnInfoPtr, Atom, INT32 *, pointer);
void NV10StopOverlay(ScrnInfoPtr);
void NV10WriteOverlayParameters(ScrnInfoPtr);

/* in nv30_exa.c */
Bool NVAccelInitNV30TCL(ScrnInfoPtr pScrn);
Bool NV30EXACheckComposite(int, PicturePtr, PicturePtr, PicturePtr);
Bool NV30EXAPrepareComposite(int, PicturePtr, PicturePtr, PicturePtr,
				  PixmapPtr, PixmapPtr, PixmapPtr);
void NV30EXAComposite(PixmapPtr, int, int, int, int, int, int, int, int);
void NV30EXADoneComposite(PixmapPtr);

/* in nv30_video_texture.c */
int NV30PutTextureImage(ScrnInfoPtr, struct nouveau_bo *, int, int, int, int,
			BoxPtr, int, int, int, int, uint16_t, uint16_t,
			uint16_t, uint16_t, uint16_t, uint16_t,
			RegionPtr, PixmapPtr, NVPortPrivPtr);
void NV30StopTexturedVideo(ScrnInfoPtr, pointer, Bool);
int NV30GetTexturePortAttribute(ScrnInfoPtr, Atom, INT32 *, pointer);
int NV30SetTexturePortAttribute(ScrnInfoPtr, Atom, INT32, pointer);

/* in nv40_exa.c */
Bool NVAccelInitNV40TCL(ScrnInfoPtr pScrn);
Bool NV40EXACheckComposite(int, PicturePtr, PicturePtr, PicturePtr);
Bool NV40EXAPrepareComposite(int, PicturePtr, PicturePtr, PicturePtr,
				  PixmapPtr, PixmapPtr, PixmapPtr);
void NV40EXAComposite(PixmapPtr, int, int, int, int, int, int, int, int);
void NV40EXADoneComposite(PixmapPtr);

/* in nv40_video_texture.c */
int NV40PutTextureImage(ScrnInfoPtr, struct nouveau_bo *, int, int, int, int,
			BoxPtr, int, int, int, int, uint16_t, uint16_t,
			uint16_t, uint16_t, uint16_t, uint16_t,
			RegionPtr, PixmapPtr, NVPortPrivPtr);
void NV40StopTexturedVideo(ScrnInfoPtr, pointer, Bool);
int NV40GetTexturePortAttribute(ScrnInfoPtr, Atom, INT32 *, pointer);
int NV40SetTexturePortAttribute(ScrnInfoPtr, Atom, INT32, pointer);

/* in nv50_accel.c */
Bool NVAccelInitNV50TCL(ScrnInfoPtr pScrn);

/* in nv50_exa.c */
Bool NV50EXAPrepareSolid(PixmapPtr, int, Pixel, Pixel);
void NV50EXASolid(PixmapPtr, int, int, int, int);
void NV50EXADoneSolid(PixmapPtr);
Bool NV50EXAPrepareCopy(PixmapPtr, PixmapPtr, int, int, int, Pixel);
void NV50EXACopy(PixmapPtr, int, int, int, int, int, int);
void NV50EXADoneCopy(PixmapPtr);
Bool NV50EXACheckComposite(int, PicturePtr, PicturePtr, PicturePtr);
Bool NV50EXAPrepareComposite(int, PicturePtr, PicturePtr, PicturePtr,
				  PixmapPtr, PixmapPtr, PixmapPtr);
void NV50EXAComposite(PixmapPtr, int, int, int, int, int, int, int, int);
void NV50EXADoneComposite(PixmapPtr);
Bool NV50EXAUploadSIFC(const char *src, int src_pitch,
		       PixmapPtr pdPix, int x, int y, int w, int h, int cpp);

/* in nv50_display.c */
Bool NV50DispPreInit(ScrnInfoPtr);
Bool NV50DispInit(ScrnInfoPtr);
void NV50DispShutdown(ScrnInfoPtr);

/* in nv50_cursor.c */
Bool NV50CursorInit(ScreenPtr);
Bool NV50CursorAcquire(ScrnInfoPtr);
void NV50CursorRelease(ScrnInfoPtr);

/* in nv50_crtc.c */
void NV50DisplayCommand(ScrnInfoPtr pScrn, uint32_t addr, uint32_t value);
void NV50CrtcCommand(nouveauCrtcPtr crtc, uint32_t addr, uint32_t value);
void NV50CrtcInit(ScrnInfoPtr pScrn);
void NV50CrtcDestroy(ScrnInfoPtr pScrn);

/* in nv50_output.c */
int NV50OrOffset(nouveauOutputPtr output);
void NV50OutputSetup(ScrnInfoPtr pScrn);
void NV50OutputDestroy(ScrnInfoPtr pScrn);

/* nv50_dac.c */
void NV50DacSetFunctionPointers(nouveauOutputPtr output);

/* nv50_sor.c */
void NV50SorSetFunctionPointers(nouveauOutputPtr output);
DisplayModePtr GetLVDSNativeMode(ScrnInfoPtr pScrn);

/* nv50_connector.c */
void NV50ConnectorInit(ScrnInfoPtr pScrn);
void NV50ConnectorDestroy(ScrnInfoPtr pScrn);

/* nv50_randr.c */
void nv50_crtc_init(ScrnInfoPtr pScrn, int crtc_num);
void nv50_output_create(ScrnInfoPtr pScrn);
int nv_scaling_mode_lookup(char *name, int size);

/* nv50_xv.c */
int nv50_xv_image_put(ScrnInfoPtr, struct nouveau_bo *, int, int, int, int,
		      BoxPtr, int, int, int, int, uint16_t, uint16_t,
		      uint16_t, uint16_t, uint16_t, uint16_t,
		      RegionPtr, PixmapPtr, NVPortPrivPtr);
void nv50_xv_video_stop(ScrnInfoPtr, pointer, Bool);
int nv50_xv_port_attribute_set(ScrnInfoPtr, Atom, INT32, pointer);
int nv50_xv_port_attribute_get(ScrnInfoPtr, Atom, INT32 *, pointer);

/* To support EXA 2.0, 2.1 has this in the header */
#ifndef exaMoveInPixmap
extern void exaMoveInPixmap(PixmapPtr pPixmap);
#endif

#endif /* __NV_PROTO_H__ */

