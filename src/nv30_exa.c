#include "nv_include.h"
#include "nv_shaders.h"

#define NV30_TCL_PRIMITIVE_3D_VTX_ATTR_2F_X(d)         (0x00001880 + d * 0x0008)
#define NV30_TCL_PRIMITIVE_3D_VTX_ATTR_2F_Y(d)         (0x00001884 + d * 0x0008)
#define NV30_TCL_PRIMITIVE_3D_VTX_ATTR_2I(d)           (0x00001900 + d * 0x0004)

typedef struct nv_pict_surface_format {
	int	 pict_fmt;
	uint32_t card_fmt;
} nv_pict_surface_format_t;

typedef struct nv_pict_texture_format {
	int	 pict_fmt;
	uint32_t card_fmt;
	uint32_t card_swz;
} nv_pict_texture_format_t;

typedef struct nv_pict_op {
	Bool	 src_alpha;
	Bool	 dst_alpha;
	uint32_t src_card_op;
	uint32_t dst_card_op;
} nv_pict_op_t;

typedef struct nv30_exa_state {
	Bool have_mask;

	struct {
		PictTransformPtr transform;
		float width;
		float height;
	} unit[2];
} nv30_exa_state_t;
static nv30_exa_state_t exa_state;
#define NV30EXA_STATE nv30_exa_state_t *state = &exa_state

static nv_pict_surface_format_t
NV30SurfaceFormat[] = {
	{ PICT_a8r8g8b8	, 0x148 },
	{ PICT_x8r8g8b8	, 0x145 },
	{ PICT_r5g6b5	, 0x143 },
//	{ PICT_a8       , 0x149 },
	{ -1, ~0 }
};

static nv_pict_surface_format_t *
NV30_GetPictSurfaceFormat(int format)
{
	int i = 0;

	while (NV30SurfaceFormat[i].pict_fmt != -1) {
		if (NV30SurfaceFormat[i].pict_fmt == format)
			return &NV30SurfaceFormat[i];
		i++;
	}

	return NULL;
}


/* should be in nouveau_reg.h at some point.. */
#define NV30TCL_TX_SWIZZLE_UNIT_S0_X_SHIFT	14
#define NV30TCL_TX_SWIZZLE_UNIT_S0_X_ZERO	 0
#define NV30TCL_TX_SWIZZLE_UNIT_S0_X_ONE	 1
#define NV30TCL_TX_SWIZZLE_UNIT_S0_X_S1		 2
#define NV30TCL_TX_SWIZZLE_UNIT_S0_Y_SHIFT	12
#define NV30TCL_TX_SWIZZLE_UNIT_S0_Z_SHIFT	10
#define NV30TCL_TX_SWIZZLE_UNIT_S0_W_SHIFT	 8
#define NV30TCL_TX_SWIZZLE_UNIT_S1_X_SHIFT	 6
#define NV30TCL_TX_SWIZZLE_UNIT_S1_X_X		 3
#define NV30TCL_TX_SWIZZLE_UNIT_S1_X_Y		 2
#define NV30TCL_TX_SWIZZLE_UNIT_S1_X_Z		 1
#define NV30TCL_TX_SWIZZLE_UNIT_S1_X_W		 0
#define NV30TCL_TX_SWIZZLE_UNIT_S1_Y_SHIFT	 4
#define NV30TCL_TX_SWIZZLE_UNIT_S1_Z_SHIFT	 2
#define NV30TCL_TX_SWIZZLE_UNIT_S1_W_SHIFT	 0

#define _(r,tf,ts0x,ts0y,ts0z,ts0w,ts1x,ts1y,ts1z,ts1w)                       \
  {                                                                           \
  PICT_##r,                                                                   \
  (tf),                                                                       \
  (NV30TCL_TX_SWIZZLE_UNIT_S0_X_##ts0x << NV30TCL_TX_SWIZZLE_UNIT_S0_X_SHIFT)|\
  (NV30TCL_TX_SWIZZLE_UNIT_S0_X_##ts0y << NV30TCL_TX_SWIZZLE_UNIT_S0_Y_SHIFT)|\
  (NV30TCL_TX_SWIZZLE_UNIT_S0_X_##ts0z << NV30TCL_TX_SWIZZLE_UNIT_S0_Z_SHIFT)|\
  (NV30TCL_TX_SWIZZLE_UNIT_S0_X_##ts0w << NV30TCL_TX_SWIZZLE_UNIT_S0_W_SHIFT)|\
  (NV30TCL_TX_SWIZZLE_UNIT_S1_X_##ts1x << NV30TCL_TX_SWIZZLE_UNIT_S1_X_SHIFT)|\
  (NV30TCL_TX_SWIZZLE_UNIT_S1_X_##ts1y << NV30TCL_TX_SWIZZLE_UNIT_S1_Y_SHIFT)|\
  (NV30TCL_TX_SWIZZLE_UNIT_S1_X_##ts1z << NV30TCL_TX_SWIZZLE_UNIT_S1_Z_SHIFT)|\
  (NV30TCL_TX_SWIZZLE_UNIT_S1_X_##ts1w << NV30TCL_TX_SWIZZLE_UNIT_S1_W_SHIFT)\
  }
static nv_pict_texture_format_t
NV30TextureFormat[] = {
	_(a8r8g8b8, 0x85,   S1,   S1,   S1,   S1, X, Y, Z, W),
	_(x8r8g8b8, 0x85,   S1,   S1,   S1,  ONE, X, Y, Z, W),
	_(x8b8g8r8, 0x85,   S1,   S1,   S1,  ONE, Z, Y, X, W),
	_(a1r5g5b5, 0x82,   S1,   S1,   S1,   S1, X, Y, Z, W),
	_(x1r5g5b5, 0x82,   S1,   S1,   S1,  ONE, X, Y, Z, W),
	_(  r5g6b5, 0x84,   S1,   S1,   S1,   S1, X, Y, Z, W),
	_(      a8, 0x81, ZERO, ZERO, ZERO,   S1, X, X, X, X),
	{ -1, ~0, ~0 }
};

static nv_pict_texture_format_t *
NV30_GetPictTextureFormat(int format)
{
	int i = 0;

	while (NV30TextureFormat[i].pict_fmt != -1) {
		if (NV30TextureFormat[i].pict_fmt == format)
			return &NV30TextureFormat[i];
		i++;
	}

	return NULL;
}

#define NV30_TCL_PRIMITIVE_3D_BF_ZERO                                     0x0000
#define NV30_TCL_PRIMITIVE_3D_BF_ONE                                      0x0001
#define NV30_TCL_PRIMITIVE_3D_BF_SRC_COLOR                                0x0300
#define NV30_TCL_PRIMITIVE_3D_BF_ONE_MINUS_SRC_COLOR                      0x0301
#define NV30_TCL_PRIMITIVE_3D_BF_SRC_ALPHA                                0x0302
#define NV30_TCL_PRIMITIVE_3D_BF_ONE_MINUS_SRC_ALPHA                      0x0303
#define NV30_TCL_PRIMITIVE_3D_BF_DST_ALPHA                                0x0304
#define NV30_TCL_PRIMITIVE_3D_BF_ONE_MINUS_DST_ALPHA                      0x0305
#define NV30_TCL_PRIMITIVE_3D_BF_DST_COLOR                                0x0306
#define NV30_TCL_PRIMITIVE_3D_BF_ONE_MINUS_DST_COLOR                      0x0307
#define NV30_TCL_PRIMITIVE_3D_BF_ALPHA_SATURATE                           0x0308
#define BF(bf) NV30_TCL_PRIMITIVE_3D_BF_##bf

static nv_pict_op_t 
NV30PictOp[] = {
/* Clear       */ { 0, 0, BF(               ZERO), BF(               ZERO) },
/* Src         */ { 0, 0, BF(                ONE), BF(               ZERO) },
/* Dst         */ { 0, 0, BF(               ZERO), BF(                ONE) },
/* Over        */ { 1, 0, BF(                ONE), BF(ONE_MINUS_SRC_ALPHA) },
/* OverReverse */ { 0, 1, BF(ONE_MINUS_DST_ALPHA), BF(                ONE) },
/* In          */ { 0, 1, BF(          DST_ALPHA), BF(               ZERO) },
/* InReverse   */ { 1, 0, BF(               ZERO), BF(          SRC_ALPHA) },
/* Out         */ { 0, 1, BF(ONE_MINUS_DST_ALPHA), BF(               ZERO) },
/* OutReverse  */ { 1, 0, BF(               ZERO), BF(ONE_MINUS_SRC_ALPHA) },
/* Atop        */ { 1, 1, BF(          DST_ALPHA), BF(ONE_MINUS_SRC_ALPHA) },
/* AtopReverse */ { 1, 1, BF(ONE_MINUS_DST_ALPHA), BF(          SRC_ALPHA) },
/* Xor         */ { 1, 1, BF(ONE_MINUS_DST_ALPHA), BF(ONE_MINUS_SRC_ALPHA) },
/* Add         */ { 0, 0, BF(                ONE), BF(                ONE) }
};

static nv_pict_op_t *
NV30_GetPictOpRec(int op)
{
	if (op >= PictOpSaturate)
		return NULL;
	return &NV30PictOp[op];
}

#if 0
#define FALLBACK(fmt,args...) do {					\
	ErrorF("FALLBACK %s:%d> " fmt, __func__, __LINE__, ##args);	\
	return FALSE;							\
} while(0)
#else
#define FALLBACK(fmt,args...) do { \
	return FALSE;              \
} while(0)
#endif

static void
NV30_LoadVtxProg(ScrnInfoPtr pScrn, nv_shader_t *shader)
{
	NVPtr pNv = NVPTR(pScrn);
	static int next_hw_id = 0;
	int i;

	if (!shader->hw_id) {
		shader->hw_id = next_hw_id;

		NVDmaStart(pNv, Nv3D,
				NV30_TCL_PRIMITIVE_3D_VP_UPLOAD_FROM_ID, 1);
		NVDmaNext (pNv, (shader->hw_id));

		for (i=0; i<shader->size; i+=4) {
			NVDmaStart(pNv, Nv3D,
					NV30_TCL_PRIMITIVE_3D_VP_UPLOAD_INST0,
					4);
			NVDmaNext (pNv, shader->data[i + 0]);
			NVDmaNext (pNv, shader->data[i + 1]);
			NVDmaNext (pNv, shader->data[i + 2]);
			NVDmaNext (pNv, shader->data[i + 3]);
			next_hw_id++;
		}
	}

	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VP_PROGRAM_START_ID, 1);
	NVDmaNext (pNv, (shader->hw_id));

	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VP_IN_REG, 2);
	NVDmaNext (pNv, shader->card_priv.NV30VP.vp_in_reg);
	NVDmaNext (pNv, shader->card_priv.NV30VP.vp_out_reg);
}

static void
NV30_LoadFragProg(ScrnInfoPtr pScrn, nv_shader_t *shader)
{
	NVPtr pNv = NVPTR(pScrn);
	static NVAllocRec *fp_mem = NULL;
	static int next_hw_id_offset = 0;

	if (!fp_mem) {
		fp_mem = NVAllocateMemory(pNv, NOUVEAU_MEM_FB, 0x1000);
		if (!fp_mem) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "Couldn't alloc fragprog buffer!\n");
			return;
		}
	}

	if (!shader->hw_id) {
		memcpy(fp_mem->map + next_hw_id_offset, shader->data,
							shader->size *
							sizeof(uint32_t));

		shader->hw_id  = fp_mem->offset;
		shader->hw_id += next_hw_id_offset;

		next_hw_id_offset += (shader->size * sizeof(uint32_t));
		next_hw_id_offset = (next_hw_id_offset + 63) & ~63;
	}

	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_FP_ACTIVE_PROGRAM, 1);
	NVDmaNext (pNv, shader->hw_id | 1);

	if (pNv->Architecture == NV_30) {
		NVDmaStart(pNv, Nv3D, 0x1d60, 1);
		NVDmaNext (pNv, 0); /* USES_KIL (1<<7) == 0 */
		NVDmaStart(pNv, Nv3D, 0x1450, 1);
		NVDmaNext (pNv, shader->card_priv.NV30FP.num_regs << 16);
	} else {
		NVDmaStart(pNv, Nv3D, 0x1d60, 1);
		NVDmaNext (pNv, (0<<7) /* !USES_KIL */ |
			 (shader->card_priv.NV30FP.num_regs << 24));
	}
}

static void
NV30_SetupBlend(ScrnInfoPtr pScrn, nv_pict_op_t *blend, Bool dest_has_alpha,
							Bool component_alpha)
{
	NVPtr pNv = NVPTR(pScrn);
	uint32_t sblend, dblend;

	sblend = blend->src_card_op;
	dblend = blend->dst_card_op;

	if (!dest_has_alpha && blend->dst_alpha) {
		if (sblend == BF(DST_ALPHA))
			sblend = BF(ONE);
		else if (sblend == BF(ONE_MINUS_DST_ALPHA))
			sblend = BF(ZERO);
	}

	if (component_alpha && blend->src_alpha) {
		if (dblend == BF(SRC_ALPHA))
			dblend = BF(SRC_COLOR);
		else if (dblend == BF(ONE_MINUS_SRC_ALPHA))
			dblend = BF(ONE_MINUS_SRC_COLOR);
	}

	if (sblend == BF(ONE) && dblend == BF(ZERO)) {
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_BLEND_FUNC_ENABLE, 1);
	NVDmaNext (pNv, 0);
	} else {
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_BLEND_FUNC_ENABLE, 5);
	NVDmaNext (pNv, 1);
	NVDmaNext (pNv, (sblend << 16) | sblend);
	NVDmaNext (pNv, (dblend << 16) | dblend);
	NVDmaNext (pNv, 0x00000000);			/* Blend colour */
	NVDmaNext (pNv, (0x8006 << 16) | 0x8006);	/* FUNC_ADD, FUNC_ADD */
	}
}

static Bool
NV30EXATexture(ScrnInfoPtr pScrn, PixmapPtr pPix, PicturePtr pPict, int unit)
{
	NVPtr pNv = NVPTR(pScrn);
	nv_pict_texture_format_t *fmt;
	uint32_t card_filter, card_repeat;
	NV30EXA_STATE;

	fmt = NV30_GetPictTextureFormat(pPict->format);
	if (!fmt)
		return FALSE;

	if (pPict->repeat && pPict->repeatType == RepeatNormal)
		card_repeat = 1;
	else
		card_repeat = 3;

	if (pPict->filter == PictFilterBilinear)
		card_filter = 2;
	else
		card_filter = 1;

	NVDmaStart(pNv, Nv3D,
			NV30_TCL_PRIMITIVE_3D_TX_ADDRESS_UNIT(unit), 8);
	NVDmaNext (pNv, NVAccelGetPixmapOffset(pPix));
	NVDmaNext (pNv, (2 << 4)  /* 2D */ |
			(fmt->card_fmt << 8) |
			(1 << 13) /* NPOT */ |
			(1<<16) /* 1 mipmap level */ |
			(1<<0) /* NvDmaFB */ |
			(1<<3) /* border disable? */);
	NVDmaNext (pNv, (card_repeat <<  0) /* S */ |
			(card_repeat <<  8) /* T */ |
			(card_repeat << 16) /* R */);
	NVDmaNext (pNv, 0x80000000);
	NVDmaNext (pNv, fmt->card_swz);
	NVDmaNext (pNv, (card_filter << 16) /* min */ |
			(card_filter << 24) /* mag */ |
			0x3fd6 /* engine lock */);
	NVDmaNext (pNv, (pPix->drawable.width << 16) | pPix->drawable.height);
	NVDmaNext (pNv, 0); /* border ARGB */
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_TX_DEPTH_UNIT(unit), 1);
	NVDmaNext (pNv, (1 << 20) /* depth */ |
			(uint32_t)exaGetPixmapPitch(pPix));

	state->unit[unit].width		= (float)pPix->drawable.width;
	state->unit[unit].height	= (float)pPix->drawable.height;
	state->unit[unit].transform	= pPict->transform;

	return TRUE;
}

static Bool
NV30_SetupSurface(ScrnInfoPtr pScrn, PixmapPtr pPix, PictFormatShort format)
{
	NVPtr pNv = NVPTR(pScrn);
	nv_pict_surface_format_t *fmt;

	fmt = NV30_GetPictSurfaceFormat(format);
	if (!fmt) {
		ErrorF("AIII no format\n");
		return FALSE;
	}

	NVDmaStart(pNv, Nv3D, 0x208, 3);
	NVDmaNext (pNv, fmt->card_fmt);
	NVDmaNext (pNv, (uint32_t)exaGetPixmapPitch(pPix));
	NVDmaNext (pNv, NVAccelGetPixmapOffset(pPix));

	return TRUE;
}

static Bool
NV30EXACheckCompositeTexture(PicturePtr pPict)
{
	nv_pict_texture_format_t *fmt;
	int w = pPict->pDrawable->width;
	int h = pPict->pDrawable->height;

	if ((w > 4096) || (h>4096))
		FALLBACK("picture too large, %dx%d\n", w, h);

	fmt = NV30_GetPictTextureFormat(pPict->format);
	if (!fmt)
		FALLBACK("picture format 0x%08x not supported\n",
				pPict->format);

	if (pPict->filter != PictFilterNearest &&
			pPict->filter != PictFilterBilinear)
		FALLBACK("filter 0x%x not supported\n", pPict->filter);

	if (pPict->repeat && (pPict->repeat != RepeatNormal &&
				pPict->repeatType != RepeatNone))
		FALLBACK("repeat 0x%x not supported\n", pPict->repeatType);

	return TRUE;
}

Bool
NV30EXACheckComposite(int op, PicturePtr psPict,
			      PicturePtr pmPict,
			      PicturePtr pdPict)
{
	nv_pict_surface_format_t *fmt;
	nv_pict_op_t *opr;

	opr = NV30_GetPictOpRec(op);
	if (!opr)
		FALLBACK("unsupported blend op 0x%x\n", op);

	fmt = NV30_GetPictSurfaceFormat(pdPict->format);
	if (!fmt)
		FALLBACK("dst picture format 0x%08x not supported\n",
				pdPict->format);

	if (!NV30EXACheckCompositeTexture(psPict))
		FALLBACK("src picture\n");
	if (pmPict) {
		if (pmPict->componentAlpha &&
				PICT_FORMAT_RGB(pmPict->format) &&
				opr->src_alpha && opr->src_card_op != BF(ZERO))
			FALLBACK("mask CA + SA\n");
		if (!NV30EXACheckCompositeTexture(pmPict))
			FALLBACK("mask picture\n");
	}
	
	return TRUE;
}

Bool
NV30EXAPrepareComposite(int op, PicturePtr psPict,
				PicturePtr pmPict,
				PicturePtr pdPict,
				PixmapPtr  psPix,
				PixmapPtr  pmPix,
				PixmapPtr  pdPix)
{
	ScrnInfoPtr pScrn = xf86Screens[psPix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	nv_pict_op_t *blend;
	NV30EXA_STATE;

	blend = NV30_GetPictOpRec(op);

	NV30_SetupBlend(pScrn, blend, PICT_FORMAT_A(pdPict->format),
			(pmPict && pmPict->componentAlpha &&
			 PICT_FORMAT_RGB(pmPict->format)));

	NV30_SetupSurface(pScrn, pdPix, pdPict->format);
	NV30EXATexture(pScrn, psPix, psPict, 0);

	NV30_LoadVtxProg(pScrn, &nv40_vp_exa_render);
	if (pmPict) {
		NV30EXATexture(pScrn, pmPix, pmPict, 1);

		if (pmPict->componentAlpha && PICT_FORMAT_RGB(pmPict->format)) {
			if (blend->src_alpha)
			NV30_LoadFragProg(pScrn, &nv30_fp_composite_mask_sa_ca);
			else
			NV30_LoadFragProg(pScrn, &nv30_fp_composite_mask_ca);
		} else {
			NV30_LoadFragProg(pScrn, &nv30_fp_composite_mask);
		}

		state->have_mask = TRUE;
	} else {
		NV30_LoadFragProg(pScrn, &nv30_fp_pass_tex0);

		state->have_mask = FALSE;
	}

	/* Appears to be some kind of cache flush, needed here at least
	 * sometimes.. funky text rendering otherwise :)
	 */
	NVDmaStart(pNv, Nv3D, 0x1fd8, 1);
	NVDmaNext (pNv, 2);
	NVDmaStart(pNv, Nv3D, 0x1fd8, 1);
	NVDmaNext (pNv, 1);

	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_BEGIN_END, 1);
	NVDmaNext (pNv, 8); /* GL_QUADS */

	return TRUE;
}

#define xFixedToFloat(v) \
	((float)xFixedToInt((v)) + ((float)xFixedFrac(v) / 65536.0))

static void
NV30EXATransformCoord(PictTransformPtr t, int x, int y, float sx, float sy,
					  float *x_ret, float *y_ret)
{
	PictVector v;

	if (t) {
		v.vector[0] = IntToxFixed(x);
		v.vector[1] = IntToxFixed(y);
		v.vector[2] = xFixed1;
		PictureTransformPoint(t, &v);
		*x_ret = xFixedToFloat(v.vector[0]) / sx;
		*y_ret = xFixedToFloat(v.vector[1]) / sy;
	} else {
		*x_ret = (float)x / sx;
		*y_ret = (float)y / sy;
	}
}

#define CV_OUTm(sx,sy,mx,my,dx,dy) do {                                        \
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VTX_ATTR_2F_X(8), 4);   \
	NVDmaFloat(pNv, (sx)); NVDmaFloat(pNv, (sy));                          \
	NVDmaFloat(pNv, (mx)); NVDmaFloat(pNv, (my));                          \
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VTX_ATTR_2I(0), 1);     \
	NVDmaNext (pNv, ((dy)<<16)|(dx));                                      \
} while(0)
#define CV_OUT(sx,sy,dx,dy) do {                                               \
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VTX_ATTR_2F_X(8), 2);   \
	NVDmaFloat(pNv, (sx)); NVDmaFloat(pNv, (sy));                          \
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VTX_ATTR_2I(0), 1);     \
	NVDmaNext (pNv, ((dy)<<16)|(dx));                                      \
} while(0)

void
NV30EXAComposite(PixmapPtr pdPix, int srcX , int srcY,
				  int maskX, int maskY,
				  int dstX , int dstY,
				  int width, int height)
{
	ScrnInfoPtr pScrn = xf86Screens[pdPix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	float sX0, sX1, sY0, sY1;
	float mX0, mX1, mY0, mY1;
	NV30EXA_STATE;

	NV30EXATransformCoord(state->unit[0].transform, srcX, srcY,
			      state->unit[0].width,
			      state->unit[0].height, &sX0, &sY0);
	NV30EXATransformCoord(state->unit[0].transform,
			      srcX + width, srcY + height,
			      state->unit[0].width,
			      state->unit[0].height, &sX1, &sY1);

	if (state->have_mask) {
		NV30EXATransformCoord(state->unit[1].transform, maskX, maskY,
				      state->unit[1].width,
				      state->unit[1].height, &mX0, &mY0);
		NV30EXATransformCoord(state->unit[1].transform,
				      maskX + width, maskY + height,
				      state->unit[1].width,
				      state->unit[1].height, &mX1, &mY1);
		CV_OUTm(sX0 , sY0 , mX0, mY0, dstX        ,          dstY);
		CV_OUTm(sX1 , sY0 , mX1, mY0, dstX + width,          dstY);
		CV_OUTm(sX1 , sY1 , mX1, mY1, dstX + width, dstY + height);
		CV_OUTm(sX0 , sY1 , mX0, mY1, dstX        , dstY + height);
	} else {
		CV_OUT(sX0 , sY0 , dstX        ,          dstY);
		CV_OUT(sX1 , sY0 , dstX + width,          dstY);
		CV_OUT(sX1 , sY1 , dstX + width, dstY + height);
		CV_OUT(sX0 , sY1 , dstX        , dstY + height);
	}

	NVDmaKickoff(pNv);
}

void
NV30EXADoneComposite(PixmapPtr pdPix)
{
	ScrnInfoPtr pScrn = xf86Screens[pdPix->drawable.pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);

	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_BEGIN_END, 1);
	NVDmaNext (pNv, 0);
}

Bool
NVAccelInitNV40TCL(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	static int have_object = FALSE;
	uint32_t class = 0, chipset;
	int i;

#undef  NV40_TCL_PRIMITIVE_3D
#define NV40_TCL_PRIMITIVE_3D                 0x4097
#define NV40_TCL_PRIMITIVE_3D_CHIPSET_4X_MASK 0x00000baf
#define NV44_TCL_PRIMITIVE_3D                 0x4497
#define NV44_TCL_PRIMITIVE_3D_CHIPSET_4X_MASK 0x00005450

	chipset = (nvReadMC(pNv, 0) >> 20) & 0xff;
	if ((chipset & 0xf0) != 0x40)
		return TRUE;
	chipset &= 0xf;

	if (NV40_TCL_PRIMITIVE_3D_CHIPSET_4X_MASK & (1<<chipset))
		class = NV40_TCL_PRIMITIVE_3D;
	else
	if (NV44_TCL_PRIMITIVE_3D_CHIPSET_4X_MASK & (1<<chipset))
		class = NV44_TCL_PRIMITIVE_3D;
	else {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "NV30EXA: Unknown chipset NV%02x\n", chipset);
		return FALSE;
	}

	if (!have_object) {
		if (!NVDmaCreateContextObject(pNv, Nv3D, class))
			return FALSE;
		have_object = TRUE;
	}

	NVDmaStart(pNv, Nv3D, 0x180, 1);
	NVDmaNext (pNv, NvDmaNotifier0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_SET_OBJECT1, 2);
	NVDmaNext (pNv, NvDmaFB);
	NVDmaNext (pNv, NvDmaFB);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_SET_OBJECT8, 1);
	NVDmaNext (pNv, NvDmaFB);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_SET_OBJECT4, 2);
	NVDmaNext (pNv, NvDmaFB);
	NVDmaNext (pNv, NvDmaFB);

	/* voodoo */
	NVDmaStart(pNv, Nv3D, 0x1ea4, 3);
	NVDmaNext(pNv, 0x00000010);
	NVDmaNext(pNv, 0x01000100);
	NVDmaNext(pNv, 0xff800006);
	NVDmaStart(pNv, Nv3D, 0x1fc4, 1);
	NVDmaNext(pNv, 0x06144321);
	NVDmaStart(pNv, Nv3D, 0x1fc8, 2);
	NVDmaNext(pNv, 0xedcba987);
	NVDmaNext(pNv, 0x00000021);
	NVDmaStart(pNv, Nv3D, 0x1fd0, 1);
	NVDmaNext(pNv, 0x00171615);
	NVDmaStart(pNv, Nv3D, 0x1fd4, 1);
	NVDmaNext(pNv, 0x001b1a19);
	NVDmaStart(pNv, Nv3D, 0x1ef8, 1);
	NVDmaNext(pNv, 0x0020ffff);
	NVDmaStart(pNv, Nv3D, 0x1d64, 1);
	NVDmaNext(pNv, 0x00d30000);
	NVDmaStart(pNv, Nv3D, 0x1e94, 1);
	NVDmaNext(pNv, 0x00000001);

	/* identity viewport transform */
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VIEWPORT_XFRM_OX, 8);
	NVDmaFloat(pNv, 0.0);
	NVDmaFloat(pNv, 0.0);
	NVDmaFloat(pNv, 0.0);
	NVDmaFloat(pNv, 0.0);
	NVDmaFloat(pNv, 1.0);
	NVDmaFloat(pNv, 1.0);
	NVDmaFloat(pNv, 1.0);
	NVDmaFloat(pNv, 0.0);

	/* default 3D state */
	/*XXX: replace with the same state that the DRI emits on startup */
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_STENCIL_FRONT_ENABLE, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_STENCIL_BACK_ENABLE, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_ALPHA_FUNC_ENABLE, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_DEPTH_WRITE_ENABLE, 2);
	NVDmaNext (pNv, 0); /* wr disable */
	NVDmaNext (pNv, 0); /* test disable */
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_COLOR_MASK, 1);
	NVDmaNext (pNv, 0x01010101); /* TR,TR,TR,TR */
	NVDmaStart(pNv, Nv3D, NV40_TCL_PRIMITIVE_3D_COLOR_MASK_BUFFER123, 1);
	NVDmaNext (pNv, 0x0000fff0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_CULL_FACE_ENABLE, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_BLEND_FUNC_ENABLE, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, Nv3D,
			NV30_TCL_PRIMITIVE_3D_LOGIC_OP_ENABLE, 2);
	NVDmaNext (pNv, 0);
	NVDmaNext (pNv, 0x1503);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_DITHER_ENABLE, 1);
	NVDmaNext (pNv, 0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_SHADE_MODEL, 1);
	NVDmaNext (pNv, 0x1d01); /* GL_SMOOTH */
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_POLYGON_OFFSET_FACTOR,2);
	NVDmaFloat(pNv, 0.0);
	NVDmaFloat(pNv, 0.0);
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_POLYGON_MODE_FRONT, 2);
	NVDmaNext (pNv, 0x1b02); /* FRONT = GL_FILL */
	NVDmaNext (pNv, 0x1b02); /* BACK  = GL_FILL */
	NVDmaStart(pNv, Nv3D,
			NV30_TCL_PRIMITIVE_3D_POLYGON_STIPPLE_PATTERN(0), 0x20);
	for (i=0;i<0x20;i++)
		NVDmaNext(pNv, 0xFFFFFFFF);
	for (i=0;i<16;i++) {
		NVDmaStart(pNv, Nv3D,
				NV30_TCL_PRIMITIVE_3D_TX_ENABLE_UNIT(i), 1);
		NVDmaNext(pNv, 0);
	}

	NVDmaStart(pNv, Nv3D, 0x1d78, 1);
	NVDmaNext (pNv, 0x110);

	NVDmaStart(pNv, Nv3D, 0x0220, 1);
	NVDmaNext (pNv, 1);
	NVDmaStart(pNv, Nv3D,
			NV30_TCL_PRIMITIVE_3D_VIEWPORT_COLOR_BUFFER_DIM0, 2);
	NVDmaNext (pNv, (4096 << 16));
	NVDmaNext (pNv, (4096 << 16));
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_SCISSOR_WIDTH_XPOS, 2);
	NVDmaNext (pNv, (4096 << 16));
	NVDmaNext (pNv, (4096 << 16));
	NVDmaStart(pNv, Nv3D, NV30_TCL_PRIMITIVE_3D_VIEWPORT_DIMS_0, 2);
	NVDmaNext (pNv, (4096 << 16));
	NVDmaNext (pNv, (4096 << 16));
	NVDmaStart(pNv, Nv3D,
			NV30_TCL_PRIMITIVE_3D_VIEWPORT_COLOR_BUFFER_OFS0, 2);
	NVDmaNext (pNv, (4095 << 16));
	NVDmaNext (pNv, (4095 << 16));

	return TRUE;
}

