/*
 * Copyright 2005-2006 Erik Waling
 * Copyright 2006 Stephane Marchesin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nv_include.h"
#include "nvreg.h"

#define DEBUGLEVEL 6
/*#define PERFORM_WRITE*/

/* TODO: 
 *       * PLL algorithms.
 */

#include <byteswap.h>
typedef struct {
	Bool execute;
	Bool repeat;
} init_exec_t;

typedef struct {
	unsigned char *data;
	unsigned int  length;

	CARD16      init_tbls_offset;
	CARD16      macro_index_offset;    
	CARD16      macro_offset; 
	CARD16      condition_offset;
	CARD16      io_flag_condition_offset;
} bios_t;

static Bool NVValidBios(ScrnInfoPtr pScrn, const unsigned char *data)
{
	/* check for BIOS signature */
	if (!(data[0] == 0x55 && data[1] == 0xAA)) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "...BIOS signature not found\n");
		return FALSE;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "...appears to be valid\n");
	return TRUE;
}

static void NVDownloadBiosPROM(ScrnInfoPtr pScrn, unsigned char *data)
{
	NVPtr pNv = NVPTR(pScrn);
	int i;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Attempting to locate BIOS image in PROM\n");

	/* enable ROM access */
	nvWriteMC(pNv, 0x1850, 0x0);
	for (i=0; i<NV_PROM_SIZE; i++) {
		/* according to nvclock, we need that to work around a 6600GT/6800LE bug */
		data[i] = pNv->PROM[i];
		data[i] = pNv->PROM[i];
		data[i] = pNv->PROM[i];
		data[i] = pNv->PROM[i];
		data[i] = pNv->PROM[i];
	}
	/* disable ROM access */
	nvWriteMC(pNv, 0x1850, 0x1);
}

static void NVDownloadBiosPRAMIN(ScrnInfoPtr pScrn, unsigned char *data)
{
	NVPtr pNv = NVPTR(pScrn);
	const unsigned char *pramin = (void*)&pNv->REGS[0x00700000/4];

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Attempting to locate BIOS image in PRAMIN\n");
	memcpy(data, pramin, 65536);
}

static Bool NVDownloadBios(ScrnInfoPtr pScrn, bios_t *bios)
{
	NVPtr pNv = NVPTR(pScrn);

	bios->data = xcalloc(1, 65536);

	NVDownloadBiosPROM(pScrn, bios->data);
	if (NVValidBios(pScrn, bios->data))
		return TRUE;

	NVDownloadBiosPRAMIN(pScrn, bios->data);
	if (NVValidBios(pScrn, bios->data))
		return TRUE;

	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Failed to locate BIOS image\n");
	xfree(bios->data);
	return FALSE;
}

typedef struct {
	char* name;
	unsigned char id;
	int length;
	int length_offset;
	int length_multiplier;
	Bool (*handler)(ScrnInfoPtr pScrn, bios_t *, CARD16, init_exec_t *);
} init_tbl_entry_t;

typedef struct {
	unsigned char id[2];
	unsigned short length;
	unsigned short offset;
} bit_entry_t;

static void parse_init_table(ScrnInfoPtr pScrn, bios_t *bios, unsigned int offset, init_exec_t *iexec);

/* #define MACRO_SIZE              8 */
#define CONDITION_SIZE          12
#define IO_FLAG_CONDITION_SIZE  9 

void still_alive()
{
	sync();
//	usleep(200000);
}

static int nv_valid_reg(CARD32 reg)
{
	#define WITHIN(x,y,z) ((x>=y)&&(x<y+z))
	if (WITHIN(reg,NV_PRAMIN_OFFSET,NV_PRAMIN_SIZE))
		return 1;
	if (WITHIN(reg,NV_PCRTC0_OFFSET,NV_PCRTC0_SIZE))
		return 1;
	if (WITHIN(reg,NV_PRAMDAC0_OFFSET,NV_PRAMDAC0_SIZE))
		return 1;
	if (WITHIN(reg,NV_PFB_OFFSET,NV_PFB_SIZE))
		return 1;
	if (WITHIN(reg,NV_PFIFO_OFFSET,NV_PFIFO_SIZE))
		return 1;
	if (WITHIN(reg,NV_PGRAPH_OFFSET,NV_PGRAPH_SIZE))
		return 1;
	if (WITHIN(reg,NV_PEXTDEV_OFFSET,NV_PEXTDEV_SIZE))
		return 1;
	if (WITHIN(reg,NV_PTIMER_OFFSET,NV_PTIMER_SIZE))
		return 1;
	if (WITHIN(reg,NV_PVIDEO_OFFSET,NV_PVIDEO_SIZE))
		return 1;
	if (WITHIN(reg,NV_PMC_OFFSET,NV_PMC_SIZE))
		return 1;
	if (WITHIN(reg,NV_FIFO_OFFSET,NV_FIFO_SIZE))
		return 1;
	if (WITHIN(reg,NV_PCIO0_OFFSET,NV_PCIO0_SIZE))
		return 1;
	if (WITHIN(reg,NV_PDIO0_OFFSET,NV_PDIO0_SIZE))
		return 1;
	if (WITHIN(reg,NV_PVIO_OFFSET,NV_PVIO_SIZE))
		return 1;
	if (WITHIN(reg,NV_PROM_OFFSET,NV_PROM_SIZE))
		return 1;
	#undef WITHIN
	return 0;
}

static int nv32_rd(ScrnInfoPtr pScrn, CARD32 reg, CARD32 *data)
{
	NVPtr pNv = NVPTR(pScrn);
	*data=pNv->REGS[reg/4];
	return 1;
}

static int nv32_wr(ScrnInfoPtr pScrn, CARD32 reg, CARD32 data)
{
#ifdef PERFORM_WRITE
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "nv32_wr reg 0x%X value 0x%X\n",reg,data);
	still_alive();
	if (!nv_valid_reg(reg))
	{
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "========= unknown reg 0x%X ==========\n",reg);
		return 0;
	}

	NVPtr pNv = NVPTR(pScrn);
	pNv->REGS[reg/4]=data;
#endif
	return 1;
}

void nv_set_crtc_index(ScrnInfoPtr pScrn, CARD8 index)
{
#ifdef PERFORM_WRITE
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "nv_set_crtc_index index 0x%X\n",index);
	still_alive();
	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	VGA_WR08(ptr, 0x3D4, index);
#endif
}

CARD8 nv_rd_crtc_data(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	return VGA_RD08(ptr, 0x3D5);
}

void nv_wr_crtc_data(ScrnInfoPtr pScrn, CARD8 val)
{
#ifdef PERFORM_WRITE
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "nv_wr_crtc_data value 0x%X\n",val);
	still_alive();
	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	VGA_WR08(ptr, 0x3D5, val);
#endif
}

static Bool init_prog(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_PROG   opcode: 0x31
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): reg
	 * offset + 5  (32 bit): and mask
	 * offset + 9  (8  bit): shift right
	 * offset + 10 (8  bit): number of configurations
	 * offset + 11 (32 bit): register
	 * offset + 15 (32 bit): configuration 1
	 * ...
	 * 
	 * Starting at offset + 15 there are "number of configurations"
	 * 32 bit values. To find out which configuration value to use
	 * read "CRTC reg" on the CRTC controller with index "CRTC index"
	 * and bitwise AND this value with "and mask" and then bit shift the
	 * result "shift right" bits to the right.
	 * Assign "register" with appropriate configuration value.
	 */

	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 and = *((CARD32 *) (&bios->data[offset + 5]));
	CARD8 shiftr = *((CARD8 *) (&bios->data[offset + 9]));
	CARD8 nr = *((CARD8 *) (&bios->data[offset + 10]));
	CARD32 reg2 = *((CARD32 *) (&bios->data[offset + 11]));
	CARD8 configuration;
	CARD32 configval, tmp;

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%04X\n", offset, 
				reg);

		nv32_rd(pScrn, reg, &tmp);
		configuration = (tmp & and) >> shiftr;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CONFIGURATION TO USE: 0x%02X\n", 
				offset, configuration);

		if (configuration <= nr) {

			configval = 
				*((CARD32 *) (&bios->data[offset + 15 + configuration * 4]));

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, 
					reg2, configval);
			
			if (nv32_rd(pScrn, reg2, &tmp)) {
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", 
						offset, tmp);
			}
			nv32_wr(pScrn, reg2, configval);
		}
	}
	return TRUE;
}

static Bool init_io_restrict_prog(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_IO_RESTRICT_PROG   opcode: 0x32
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC reg
	 * offset + 3  (8  bit): CRTC index
	 * offset + 4  (8  bit): and mask
	 * offset + 5  (8  bit): shift right
	 * offset + 6  (8  bit): number of configurations
	 * offset + 7  (32 bit): register
	 * offset + 11 (32 bit): configuration 1
	 * ...
	 * 
	 * Starting at offset + 11 there are "number of configurations"
	 * 32 bit values. To find out which configuration value to use
	 * read "CRTC reg" on the CRTC controller with index "CRTC index"
	 * and bitwise AND this value with "and mask" and then bit shift the
	 * result "shift right" bits to the right.
	 * Assign "register" with appropriate configuration value.
	 */

	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	CARD16 crtcreg = *((CARD16 *) (&bios->data[offset + 1]));
	CARD8  index = *((CARD8 *) (&bios->data[offset + 3]));
	CARD8 and = *((CARD8 *) (&bios->data[offset + 4]));
	CARD8 shiftr = *((CARD8 *) (&bios->data[offset + 5]));
	CARD8 nr = *((CARD8 *) (&bios->data[offset + 6]));
	CARD32 reg = *((CARD32 *) (&bios->data[offset + 7]));
	CARD8 configuration;
	CARD32 configval, tmp;

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CRTC REG: 0x%04X, INDEX: 0x%02X\n", offset, 
				crtcreg, index);

		VGA_WR08(ptr,crtcreg, index);
		configuration = (VGA_RD08(ptr, crtcreg + 1) & and) >> shiftr;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CONFIGURATION TO USE: 0x%02X\n", 
				offset, configuration);

		if (configuration <= nr) {
			configval = 
				*((CARD32 *) (&bios->data[offset + 11 + configuration * 4]));

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, 
					reg, configval);
			
			if (nv32_rd(pScrn, reg, &tmp)) {
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", 
						offset, tmp);
			}
			nv32_wr(pScrn, reg, configval);
		}
	}
	return TRUE;
}

static Bool init_repeat(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	CARD8 repeats = *((CARD8 *) (&bios->data[offset + 1]));
	CARD8 i;

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REPEATING FOLLOWING SEGMENT %d TIMES.\n", 
				offset, repeats);

		iexec->repeat = TRUE;

		for (i = 0; i < repeats - 1; i++)
			parse_init_table(pScrn, bios, offset + 2, iexec);

		iexec->repeat = FALSE;
	}
	return TRUE;
}

static Bool init_end_repeat(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	if (iexec->repeat)
		return FALSE;

	return TRUE;
}

static Bool init_copy(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* XXX: double check this... */
	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD8 shift = *((CARD8 *) (&bios->data[offset + 5]));
	CARD8 and1 = *((CARD8 *) (&bios->data[offset + 6]));
	CARD16 crtcreg = *((CARD16 *) (&bios->data[offset + 7]));
	CARD8 index = *((CARD8 *) (&bios->data[offset + 9]));
	CARD8 and2 = *((CARD8 *) (&bios->data[offset + 10]));
	CARD32 data;
	CARD8 crtcdata;

	if (iexec->execute) {
		if (nv32_rd(pScrn, reg, &data)) {
			if (shift < 0x80) 
				data >>= shift;
			else
				data <<= (0x100 - shift);

			data &= and1;
			VGA_WR08(ptr,crtcreg, index);
			crtcdata = (VGA_RD08(ptr, crtcreg + 1) & and2) | (CARD8) data;

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: CRTC REG: 0x%04X, INDEX: 0x%04X, VALUE: 0x%02X\n"
					, offset, crtcreg, index, crtcdata);

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
					VGA_RD08(ptr, crtcreg + 1));
#ifdef PERFORM_WRITE 
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "init_copy crtcreg 0x%X value 0x%X\n",crtcreg+1,crtcdata);
			still_alive();
			printf("WRITE IS PERFORMED\n");
			VGA_WR08(ptr,crtcreg + 1, crtcdata);
#endif
		}
	}
	return TRUE;
}

static Bool init_not(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	if (iexec->execute) { 
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: ------ SKIPPING FOLLOWING COMMANDS  ------\n",
			offset);
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: ------ EXECUTING FOLLOWING COMMANDS ------\n",
			offset);
	}

	iexec->execute = !iexec->execute;
	return TRUE;
}

static Bool init_io_flag_condition(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	CARD8 cond = *((CARD8 *) (&bios->data[offset + 1]));
	CARD16 crtcreg = *((CARD16 *) 
		(&bios->data[bios->io_flag_condition_offset + 
		cond * IO_FLAG_CONDITION_SIZE]));
	CARD8 index = *((CARD8 *) 
		(&bios->data[bios->io_flag_condition_offset + 
		cond * IO_FLAG_CONDITION_SIZE + 2]));
	CARD8 and1 = *((CARD8 *) 
		(&bios->data[bios->io_flag_condition_offset + 
		cond * IO_FLAG_CONDITION_SIZE + 3]));
	CARD8 shift = *((CARD8 *) 
		(&bios->data[bios->io_flag_condition_offset + 
		cond * IO_FLAG_CONDITION_SIZE + 4]));
	CARD16 offs = *((CARD16 *) 
		(&bios->data[bios->io_flag_condition_offset + 
		cond * IO_FLAG_CONDITION_SIZE + 5]));
	CARD8 and2 = *((CARD8 *) 
		(&bios->data[bios->io_flag_condition_offset + 
		cond * IO_FLAG_CONDITION_SIZE + 7]));
	CARD8 cmpval = *((CARD8 *) 
		(&bios->data[bios->io_flag_condition_offset + 
		cond * IO_FLAG_CONDITION_SIZE + 8]));

	CARD8 data;

	if (iexec->execute) {
		VGA_WR08(ptr,crtcreg, index);
		data = VGA_RD08(ptr, crtcreg + 1);
		data &= and1;
		offs += (data >> shift);
		data = *((CARD8 *) (&bios->data[offs]));
		data &= and2;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: CHECKING IF DATA: %02X equals COND: %02X\n", offset, 
				data, cmpval);

		if (data == cmpval) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: CONDITION FULFILLED - CONTINUING TO EXECUTE\n", 
					offset);
		} else {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CONDITION IS NOT FULFILLED.\n", offset);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: ------ SKIPPING FOLLOWING COMMANDS  ------\n", offset);
			iexec->execute = FALSE;     
		}
	}
	return TRUE;
}

static Bool init_io_restrict_pll(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: [ NOT YET IMPLEMENTED ]\n", offset);
	/* XXX: this needs to be confirmed... NOT CORRECT */
	/*init_io_restrict_prog(bios, offset, iexec);*/

	CARD16 crtcreg = *((CARD16 *) (&bios->data[offset + 1]));
	CARD8  index = *((CARD8 *) (&bios->data[offset + 3]));
	CARD8 and = *((CARD8 *) (&bios->data[offset + 4]));
	CARD8 shiftr = *((CARD8 *) (&bios->data[offset + 5]));
	CARD8 nr = *((CARD8 *) (&bios->data[offset + 6]));
	CARD32 reg = *((CARD32 *) (&bios->data[offset + 7]));
	CARD8 configuration;
	CARD32 configval, tmp;
#if 0
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CRTC REG: 0x%04X, INDEX: 0x%02X\n", offset, 
		crtcreg, index, reg);

	VGA_WR08(ptr,crtcreg, index);
	configuration = (VGA_RD08(ptr, crtcreg + 1) & and) >> shiftr;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CONFIGURATION TO USE: 0x%02X\n", 
		offset, configuration);

	if (configuration <= nr) {

		if (DEBUGLEVEL >= 6 && nv32_rd(pScrn, reg, &configval)) 
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", 
				offset, configval);

		configval = 
			*((CARD32 *) (&bios->data[offset + 11 + configuration * 4]));

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, 
			reg, configval);
	}
#endif

#if 0
	if (iexec->execute) {    
		switch (reg) {
		case 0x00004004:
			configval = 0x01014E07;
			break;
		case 0x00004024:
			configval = 0x13030E02;
			break;
		}

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, 
				reg, configval);

		if (DEBUGLEVEL >= 6 && nv32_rd(pScrn, reg, &tmp))
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", 
					offset, tmp);

		nv32_wr(pScrn, reg, configval);
	}
#endif
	return TRUE;
}

static Bool init_pll(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
 	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 val = *((CARD32 *) (&bios->data[offset + 5]));
	CARD32 configval, tmp;
#if 0
	if (iexec->execute) {
		switch (reg) {
		case 0x00680508:
			configval = 0x00011F05;
			break;
		}

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, reg,
				configval);

		if (DEBUGLEVEL >= 6 && nv32_rd(pScrn, reg, &tmp)) 
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", 
					offset, tmp);

		nv32_wr(pScrn, reg, configval);

		/*xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: [ NOT YET IMPLEMENTED ]\n", offset);*/
	}
#endif
	return TRUE;
}

Bool init_cr_idx_adr_latch(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	CARD8 crtcindex = *((CARD8 *) (&bios->data[offset + 1]));
	CARD8 crtcdata = *((CARD8 *) (&bios->data[offset + 2]));
	CARD8 initial_index = *((CARD8 *) (&bios->data[offset + 3]));
	CARD8 entries = *((CARD8 *) (&bios->data[offset + 4]));
	CARD8 data;
	int i;

	if (iexec->execute) {
		for (i = 0; i < entries; i++) {
			nv_set_crtc_index(pScrn, crtcindex);

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CRTC INDEX: %02X    DATA: %02X\n", offset,
					crtcindex, initial_index + i);

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
					nv_rd_crtc_data(pScrn));

			nv_wr_crtc_data(pScrn, initial_index + i);

			nv_set_crtc_index(pScrn, crtcdata);

			data = *((CARD8 *) (&bios->data[offset + 5 + i]));

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CRTC INDEX: %02X    DATA: %02X\n", offset,
					crtcdata, data);

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
					nv_rd_crtc_data(pScrn));

			nv_wr_crtc_data(pScrn, data);
		}
	}
	return TRUE;
}

Bool init_cr(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* XXX: IS THIS CORRECT? check the typecast .. probably wrong */

	NVPtr pNv = NVPTR(pScrn);
	CARD8 index = *((CARD32 *) (&bios->data[offset + 1])); 
	CARD8 and = *((CARD8 *) (&bios->data[offset + 2]));
	CARD8 or = *((CARD8 *) (&bios->data[offset + 3]));
	CARD8 data;

	if (iexec->execute) {
		nv_set_crtc_index(pScrn, index);
		data = (nv_rd_crtc_data(pScrn) & and) | or;
		/*printf("and: 0x%02x    or: 0x%02x\n", and, or);*/
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CRTC INDEX: 0x%02X, VALUE: 0x%02X\n", offset, 
				index, data);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
				nv_rd_crtc_data(pScrn));

		nv_wr_crtc_data(pScrn, data);
	}
	return TRUE;
}

static Bool init_zm_cr(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_ZM_CR   opcode: 0x53
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (8  bit): CRTC index
	 * offset + 2  (8  bit): value
	 * 
	 * Assign "value" to CRTC register with index "CRTC index".
	 */

	NVPtr pNv = NVPTR(pScrn);
	CARD8 index = *((CARD32 *) (&bios->data[offset + 1]));
	CARD8 value = *((CARD8 *) (&bios->data[offset + 2]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CRTC INDEX: 0x%02X, VALUE: 0x%02X\n", offset, 
				index, value);

		nv_set_crtc_index(pScrn, index);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
				nv_rd_crtc_data(pScrn));

		nv_wr_crtc_data(pScrn, value);
	}
	return TRUE;
}

static Bool init_zm_cr_group(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_ZM_CR   opcode: 0x54
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (8  bit): number of groups (index, value)
	 * offset + 2  (8  bit): index 1
	 * offset + 3  (8  bit): value 1
	 * ...
	 * 
	 * Assign "value n" to CRTC register with index "index n".
	 */
    
	CARD8 nr = *((CARD8 *) (&bios->data[offset + 1]));
	CARD8 index, value;
	int i;
	
	if (iexec->execute) {
		for (i = 0; i < nr; i++) {
			index = *((CARD8 *) (&bios->data[offset + 2 + 2 * i]));
			value = *((CARD8 *) (&bios->data[offset + 2 + 2 * i + 1]));

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CRTC INDEX: 0x%02X, VALUE: 0x%02X\n", offset,
					index, value);

			nv_set_crtc_index(pScrn, index);

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset,
					nv_rd_crtc_data(pScrn));
			nv_wr_crtc_data(pScrn, value);
		}
	}
	return TRUE;
}

static Bool init_condition_time(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* My BIOS does not use this command. */
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: [ NOT YET IMPLEMENTED ]\n", offset);

	return FALSE;
}

static Bool init_zm_reg_sequence(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_ZM_REG_SEQUENCE   opcode: 0x58
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register base
	 * offset + 5  (8  bit): nr
	 * offset + 6  (32 bit): value to assign "register base" + 4
	 * ...
	 * 
	 * Initialzies a sequence of "nr" registers starting at "register base".
	 */

	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 nr = *((CARD8 *) (&bios->data[offset + 5]));
	CARD32 data;
	CARD32 tmp;
	int i;

   	if (iexec->execute) { 
		for (i = 0; i < nr; i++) {
			data = *((CARD32 *) (&bios->data[offset + 6 + i * 4]));
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset,
					reg + i * 4, data);

			if (nv32_rd(pScrn, reg + i * 4, &tmp)) { 
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", 
						offset, tmp);
			}

			nv32_wr(pScrn, reg + i * 4, data);
		}
	}
	return TRUE;
}

static Bool init_indirect_reg(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_INDIRECT_REG opcode: 0x5A
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (16 bit): adress offset (in bios)
	 *
	 * Lookup value at offset data in the bios and write it to reg
	 */
	NVPtr pNv = NVPTR(pScrn);
	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 data = *((CARD16 *) (&bios->data[offset + 5]));
	CARD32 data2 = bios->data[data];

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: REG: 0x%04X, DATA AT: 0x%04X, VALUE IS: 0x%08X\n", 
				offset, reg, data, data2);

		if (DEBUGLEVEL >= 6) {
			CARD32 tmpval;
			nv32_rd(pScrn, reg, &tmpval);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", offset, tmpval);
		}

		nv32_wr(pScrn, reg, data2);
	}
	return TRUE;
}

static Bool init_sub_direct(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_SUB_DIRECT   opcode: 0x5B
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): subroutine offset (in bios)
	 *
	 * Calls a subroutine that will execute commands until INIT_DONE
	 * is found. 
	 */

	CARD16 sub_offset = *((CARD16 *) (&bios->data[offset + 1]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: EXECUTING SUB-ROUTINE AT: 0x%04X\n", 
				offset, sub_offset);

		parse_init_table(pScrn, bios, sub_offset, iexec);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: END OF SUB-ROUTINE\n", offset);
	}
	return TRUE;
}

static Bool init_copy_nv_reg(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{   
 	CARD32 srcreg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD8 shift = *((CARD8 *) (&bios->data[offset + 5]));
	CARD32 and1 = *((CARD32 *) (&bios->data[offset + 6]));
	CARD32 xor = *((CARD32 *) (&bios->data[offset + 10]));
	CARD32 dstreg = *((CARD32 *) (&bios->data[offset + 14]));
	CARD32 and2 = *((CARD32 *) (&bios->data[offset + 18]));
	CARD32 srcdata;
	CARD32 dstdata;
	
	if (iexec->execute) {
		nv32_rd(pScrn, srcreg, &srcdata);
		
		if (shift > 0)
			srcdata >>= shift;
		else
			srcdata <<= shift;

		srcdata = (srcdata & and1) ^ xor;

		nv32_rd(pScrn, dstreg, &dstdata);
		dstdata &= and2;

		dstdata |= srcdata;

		CARD32 tmp;		
		nv32_rd(pScrn, dstreg, &tmp);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, dstreg, 
				dstdata);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", offset, tmp);

		nv32_wr(pScrn, dstreg, dstdata);
	}
	return TRUE;
}

static Bool init_zm_index_io(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	CARD16 crtcreg = *((CARD16 *) (&bios->data[offset + 1]));
	CARD8 index = *((CARD8 *) (&bios->data[offset + 3]));
	CARD8 value = *((CARD8 *) (&bios->data[offset + 4]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: CRTC REG: 0x%04X, INDEX: 0x%04X, VALUE: 0x%02X\n", 
				offset, crtcreg, index, value);

		VGA_WR08(ptr,crtcreg, index);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
				VGA_RD08(ptr, crtcreg + 1));
	
#ifdef PERFORM_WRITE
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "init_zm_index_io crtcreg 0x%X value 0x%X\n",crtcreg+1,value);
		still_alive();
		VGA_WR08(ptr,crtcreg + 1, value);
#endif
	}
	return TRUE;
}

static Bool init_compute_mem(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	// FIXME replace with a suitable implementation
#if 0
	CARD16 ramcfg = *((CARD16 *) (&bios->data[bios->ram_table_offset]));
	CARD32 pfb_debug;
	CARD32 strapinfo;
	CARD32 ramcfg2;

	if (iexec->execute) {
		nv32_rd(pScrn, 0x00101000, &strapinfo);
		nv32_rd(pScrn, 0x00100080, &pfb_debug);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "STRAPINFO: 0x%08X\n", strapinfo);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "PFB_DEBUG: 0x%08X\n", pfb_debug);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "RAM CFG: 0x%04X\n", ramcfg);

		pfb_debug &= 0xffffffef;
		strapinfo >>= 2;
		strapinfo &= 0x0000000f;
		ramcfg2 = *((CARD16 *) 
				(&bios->data[bios->ram_table_offset + (2 * strapinfo)])); 

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "AFTER MANIPULATION\n");
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "STRAPINFO: 0x%08X\n", strapinfo);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "PFB_DEBUG: 0x%08X\n", pfb_debug);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "RAM CFG2: 0x%08X\n", ramcfg2);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: [ NOT YET IMPLEMENTED ]\n", offset);

		CARD32 reg1;
		CARD32 reg2;

		nv32_rd(pScrn, 0x00100200, &reg1);
		nv32_rd(pScrn, 0x0010020C, &reg2);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x00100200: 0x%08X\n", reg1);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x0010020C: 0x%08X\n", reg2);
	}
#endif
	return TRUE;
}

static Bool init_reset(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 value1 = *((CARD32 *) (&bios->data[offset + 5]));
	CARD32 value2 = *((CARD32 *) (&bios->data[offset + 9]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", 
				offset, reg, value1);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", 
				offset, reg, value2);

		if (DEBUGLEVEL >= 6) {
			CARD32 tmpval;
			nv32_rd(pScrn, reg, &tmpval);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", offset, tmpval);
#if 0
			nv32_rd(pScrn, PCICFG(PCICFG_ROMSHADOW), &tmpval);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: PCICFG_ROMSHADOW: 0x%02X\n", offset, tmpval);
#endif
		}
		nv32_wr(pScrn, reg, value1);
		nv32_wr(pScrn, reg, value2);
	}
	/* PCI Config space init needs to be added here. */
	/* if (nv32_rd(pScrn, PCICFG(PCICFG_ROMSHADOW), value1)) */
	/*     nv32_wr(pScrn, PCICFG(PCICFG_ROMSHADOW), value1 & 0xfffffffe) */

	return TRUE;
}

static Bool init_index_io8(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_INDEX_IO8   opcode: 0x69
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC reg
	 * offset + 3  (8  bit): and mask
	 * offset + 4  (8  bit): or with
	 * 
	 * 
	 */

	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	CARD16 reg = *((CARD16 *) (&bios->data[offset + 1]));
	CARD8 and  = *((CARD8 *) (&bios->data[offset + 3]));
	CARD8 or = *((CARD8 *) (&bios->data[offset + 4]));
	CARD8 data;

	if (iexec->execute) {
		data = (VGA_RD08(ptr, reg) & and) | or;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: CRTC REG: 0x%04X, VALUE: 0x%02X\n", 
				offset, reg, data);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
				VGA_RD08(ptr, reg));

#ifdef PERFORM_WRITE
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "init_index_io8 crtcreg 0x%X value 0x%X\n",reg,data);
		still_alive();
		VGA_WR08(ptr, reg, data);
#endif
	}
	return TRUE;
}

static Bool init_sub(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	CARD8 sub = *((CARD8 *) (&bios->data[offset + 1]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: EXECUTING SUB-SCRIPT: %d\n", offset, sub);

		parse_init_table(pScrn, bios, 
				*((CARD16 *) (&bios->data[bios->init_tbls_offset + sub * 2])),
				iexec);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: END OF SUB-SCRIPT\n", offset);
	}
	return TRUE;
}

static Bool init_ram_condition(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_RAM_CONDITION   opcode: 0x6D
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (8  bit): and mask
	 * offset + 2  (8  bit): cmpval
	 *
	 * Test if (NV_PFB_BOOT & and mask) matches cmpval
	 */
	NVPtr pNv = NVPTR(pScrn);
	CARD8 and = *((CARD8 *) (&bios->data[offset + 1]));
	CARD8 cmpval = *((CARD8 *) (&bios->data[offset + 2]));
	CARD32 data;

	if (iexec->execute) {
		data=(pNv->PFB[NV_PFB_BOOT/4])&and;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: CHECKING IF REGVAL: 0x%08X equals COND: 0x%08X\n",
				offset, data, cmpval);

		if (data == cmpval) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: CONDITION FULFILLED - CONTINUING TO EXECUTE\n",
					offset);
		} else {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CONDITION IS NOT FULFILLED.\n", offset);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: ------ SKIPPING FOLLOWING COMMANDS  ------\n", offset);
			iexec->execute = FALSE;     
		}
	}
	return TRUE;
}

static Bool init_nv_reg(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_NV_REG   opcode: 0x6E
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (32 bit): and mask
	 * offset + 9  (32 bit): or with
	 *
	 * Assign "register" to (REGVAL(register) & "and mask") | "or with";
	 */

	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 and = *((CARD32 *) (&bios->data[offset + 5]));
	CARD32 or = *((CARD32 *) (&bios->data[offset + 9]));
	CARD32 data;
	unsigned int status;

	if (iexec->execute) {
		/* end temp test */
		if ((status = nv32_rd(pScrn, reg, &data))) {
			data = (data & and) | or;
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, 
					reg, data);

			if (DEBUGLEVEL >= 6 && status) {
				CARD32 tmpval;
				nv32_rd(pScrn, reg, &tmpval);
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n",
						offset, tmpval);
			}

			nv32_wr(pScrn, reg, data);
			/* Assign: reg = data */
		}
	}
	return TRUE;
}

#if 0
static Bool init_macro(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	// FIXME replace with the haiku version
	/* XXX: Not sure this is correct... */

	CARD8 macro = *((CARD8 *) (&bios->data[offset + 1]));
	CARD32 reg = 
		*((CARD32 *) (&bios->data[bios->macro_offset + macro * MACRO_SIZE]));
	CARD32 value =
		*((CARD32 *) (&bios->data[bios->macro_offset + macro * MACRO_SIZE + 4]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: EXECUTING MACRO: 0x%02X\n", offset, macro);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset, reg,
				value);

		if (DEBUGLEVEL >= 6) {
			CARD32 tmpval;
			nv32_rd(pScrn, reg, &tmpval);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n",
					offset, tmpval);
		}

		nv32_wr(pScrn, reg, value);
	}
	return TRUE;
}
#endif

static Bool init_macro(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	CARD8 index = *((CARD8 *) (&bios->data[offset + 1]));
	CARD32 tmp = bios->macro_index_offset + (index << 1);
	CARD32 offs =  *((CARD8 *) (&bios->data[tmp]))  << 3;
	CARD32 nr = *((CARD8 *) (&bios->data[tmp + 1]));
	CARD32 reg, data;

	int i;
	
    if (iexec->execute) {
	    offs += bios->macro_offset;
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "0x%04X: WRITE %d 32-BIT REGS:\n", offset, nr);

	    for (i = 0; i < nr; i++) {
		    reg = *((CARD32 *) (&bios->data[offs + (i << 3)]));
		    data = *((CARD32 *) (&bios->data[offs + (i << 3) + 4]));
		    
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", offset,
			reg, data);

		    if (DEBUGLEVEL >= 6) {
			    CARD32 tmpval;
			    nv32_rd(pScrn, reg, &tmpval);
			    xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n",
				offset, tmpval);
		    }

		    nv32_wr(pScrn, reg, data);
	    }
    }
    return TRUE;
}

static Bool init_done(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	return TRUE;
}

static Bool init_resume(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	if (!iexec->execute) {
		iexec->execute = TRUE;;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: ---- EXECUTING FOLLOWING COMMANDS ----\n",
				offset);
	}
	return TRUE;
}

static Bool init_ram_condition2(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_RAM_CONDITION2   opcode: 0x73
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (8  bit): and mask
	 * offset + 2  (8  bit): cmpval
	 *
	 * Test if (NV_EXTDEV_BOOT & and mask) matches cmpval
	 */
	NVPtr pNv = NVPTR(pScrn);
	CARD32 and = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 cmpval = *((CARD32 *) (&bios->data[offset + 5]));
	CARD32 data;

	if (iexec->execute) {
		data=(nvReadEXTDEV(pNv, NV_PEXTDEV_BOOT))&and;
		
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: CHECKING IF REGVAL: 0x%08X equals COND: 0x%08X\n",
				offset, data, cmpval);

		if (data == cmpval) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: CONDITION FULFILLED - CONTINUING TO EXECUTE\n",
					offset);
		} else {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CONDITION IS NOT FULFILLED.\n", offset);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: ------ SKIPPING FOLLOWING COMMANDS  ------\n", offset);
			iexec->execute = FALSE;     
		}
	}
	return TRUE;
}

static Bool init_time(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_TIME   opcode: 0x74
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): time
	 * 
	 * Sleep for "time" microseconds.
	 */

	CARD16 time = *((CARD16 *) (&bios->data[offset + 1]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: Sleeping for 0x%04X microseconds.\n", 
				offset, time);

		usleep(time);
	}
	return TRUE;
}

static Bool init_condition(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	CARD8 cond = *((CARD8 *) (&bios->data[offset + 1]));
	CARD32 reg = 
		*((CARD32 *) 
				(&bios->data[bios->condition_offset + cond * CONDITION_SIZE]));
	CARD32 and = 
		*((CARD32 *) 
				(&bios->data[bios->condition_offset + cond * CONDITION_SIZE + 4]));
	CARD32 cmpval = 
		*((CARD32 *) 
				(&bios->data[bios->condition_offset + cond * CONDITION_SIZE + 8]));
	CARD32 data;

	if (iexec->execute) {
		if (nv32_rd(pScrn, reg, &data)) {
			data &= and;

			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
					"0x%04X: CHECKING IF REGVAL: 0x%08X equals COND: 0x%08X\n",
					offset, data, cmpval);

			if (data == cmpval) {
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
						"0x%04X: CONDITION FULFILLED - CONTINUING TO EXECUTE\n",
						offset);
			} else {
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CONDITION IS NOT FULFILLED.\n", offset);
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
						"0x%04X: ------ SKIPPING FOLLOWING COMMANDS  ------\n", offset);
				iexec->execute = FALSE;     
			}
		}
	}
	return TRUE;
}

static Bool init_index_io(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_INDEX_IO   opcode: 0x78
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC reg
	 * offset + 3  (8  bit): CRTC index
	 * offset + 4  (8  bit): and mask
	 * offset + 5  (8  bit): or with
	 * 
	 * 
	 */

	NVPtr pNv = NVPTR(pScrn);
	volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
	CARD16 crtcreg = *((CARD16 *) (&bios->data[offset + 1]));
	CARD8 index = *((CARD8 *) (&bios->data[offset + 3]));
	CARD8 and  = *((CARD8 *) (&bios->data[offset + 4]));
	CARD8 or = *((CARD8 *) (&bios->data[offset + 5]));
	CARD8 data;
	
	if (iexec->execute) {
		VGA_WR08(ptr,crtcreg, index);
		/* data at reg + 1 */
		data = (VGA_RD08(ptr, crtcreg + 1) & and) | or;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: CRTC REG: 0x%04X, INDEX: 0x%04X, VALUE: 0x%02X\n", 
				offset, crtcreg, index, data);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%02X\n", offset, 
				VGA_RD08(ptr, crtcreg + 1));

#ifdef PERFORM_WRITE
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "init_index_io crtcreg 0x%X value 0x%X\n",crtcreg+1,data);
		still_alive();
		VGA_WR08(ptr,crtcreg + 1, data);
#endif
	}
	return TRUE;
}

static Bool init_zm_reg(ScrnInfoPtr pScrn, bios_t *bios, CARD16 offset, init_exec_t *iexec)
{
	/* INIT_ZM_REG   opcode: 0x7A
	 * 
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (32 bit): value
	 * 
	 * Assign "register" to "value";
	 */

	CARD32 reg = *((CARD32 *) (&bios->data[offset + 1]));
	CARD32 value = *((CARD32 *) (&bios->data[offset + 5]));

	if (iexec->execute) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: REG: 0x%08X, VALUE: 0x%08X\n", 
				offset, reg, value);

		if (DEBUGLEVEL >= 6) {
			CARD32 tmpval;
			nv32_rd(pScrn, reg, &tmpval);
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: CURRENT VALUE IS: 0x%08X\n", offset, tmpval);
		}

		nv32_wr(pScrn, reg, value);

		/* Assign: reg = value */
	}
	return TRUE;
}

static init_tbl_entry_t itbl_entry[] = {
	/* command name                       , id  , length  , offset  , mult    , command handler                 */
	{ "INIT_PROG"                         , 0x31, 15      , 10      , 4       , init_prog                       },
	{ "INIT_IO_RESTRICT_PROG"             , 0x32, 11      , 6       , 4       , init_io_restrict_prog           },
	{ "INIT_REPEAT"                       , 0x33, 2       , 0       , 0       , init_repeat                     },
	{ "INIT_END_REPEAT"                   , 0x36, 1       , 0       , 0       , init_end_repeat                 },
	{ "INIT_COPY"                         , 0x37, 11      , 0       , 0       , init_copy                       },
	{ "INIT_NOT"                          , 0x38, 1       , 0       , 0       , init_not                        },
	{ "INIT_IO_FLAG_CONDITION"            , 0x39, 2       , 0       , 0       , init_io_flag_condition          },
/*	{ "INIT_INDEX_ADDRESS_LATCHED"        , 0x49, x       , x       , x,      , init_idx_addr_latched           }, */	
	{ "INIT_IO_RESTRICT_PLL"              , 0x4A, 43      , 0       , 0       , init_io_restrict_pll            },
	{ "INIT_PLL"                          , 0x4B, 9       , 0       , 0       , init_pll                        },
/*	{ "INIT_I2C_BYTE"                     , 0x4C, x       , x       , x       , init_i2c_byte                   }, */
/*	{ "INIT_ZM_I2C_BYTE"                  , 0x4D, x       , x       , x       , init_zm_i2c_byte                }, */
/*	{ "INIT_ZM_I2C"                       , 0x4E, x       , x       , x       , init_zm_i2c                     }, */
	{ "INIT_CR_INDEX_ADDRESS_LATCHED"     , 0x51, 5       , 4       , 1       , init_cr_idx_adr_latch           },
	{ "INIT_CR"                           , 0x52, 4       , 0       , 0       , init_cr                         },
	{ "INIT_ZM_CR"                        , 0x53, 3       , 0       , 0       , init_zm_cr                      },
	{ "INIT_ZM_CR_GROUP"                  , 0x54, 2       , 1       , 2       , init_zm_cr_group                },
	{ "INIT_CONDITION_TIME"               , 0x56, 3       , 0       , 0       , init_condition_time             },
	{ "INIT_ZM_REG_SEQUENCE"              , 0x58, 6       , 5       , 4       , init_zm_reg_sequence            },
	{ "INIT_INDIRECT_REG"                 , 0x5A, 7       , 0       , 0       , init_indirect_reg               },
	{ "INIT_SUB_DIRECT"                   , 0x5B, 3       , 0       , 0       , init_sub_direct                 },
	{ "INIT_COPY_NV_REG"                  , 0x5F, 22      , 0       , 0       , init_copy_nv_reg                },
	{ "INIT_ZM_INDEX_IO"                  , 0x62, 5       , 0       , 0       , init_zm_index_io                },
	{ "INIT_COMPUTE_MEM"                  , 0x63, 1       , 0       , 0       , init_compute_mem                },
	{ "INIT_RESET"                        , 0x65, 13      , 0       , 0       , init_reset                      },
/*	{ "INIT_NEXT"                         , 0x66, x       , x       , x       , init_next                       }, */	
/*	{ "INIT_NEXT"                         , 0x67, x       , x       , x       , init_next                       }, */	
/*	{ "INIT_NEXT"                         , 0x68, x       , x       , x       , init_next                       }, */	
	{ "INIT_INDEX_IO8"                    , 0x69, 5       , 0       , 0       , init_index_io8                  },
	{ "INIT_SUB"                          , 0x6B, 2       , 0       , 0       , init_sub                        },
	{ "INIT_RAM_CONDITION"                , 0x6D, 3       , 0       , 0       , init_ram_condition              },
	{ "INIT_NV_REG"                       , 0x6E, 13      , 0       , 0       , init_nv_reg                     },
	{ "INIT_MACRO"                        , 0x6F, 2       , 0       , 0       , init_macro                      },
	{ "INIT_DONE"                         , 0x71, 1       , 0       , 0       , init_done                       },
	{ "INIT_RESUME"                       , 0x72, 1       , 0       , 0       , init_resume                     },
	{ "INIT_RAM_CONDITION2"               , 0x73, 9       , 0       , 0       , init_ram_condition2             },
	{ "INIT_TIME"                         , 0x74, 3       , 0       , 0       , init_time                       },
	{ "INIT_CONDITION"                    , 0x75, 2       , 0       , 0       , init_condition                  },
/*	{ "INIT_IO_CONDITION",                , 0x76, x       , x,      , x       , init_io_condition               }, */
	{ "INIT_INDEX_IO"                     , 0x78, 6       , 0       , 0       , init_index_io                   },
/*	{ "INIT_PLL2"                         , 0x79, x       , x       , x       , init_pll2                       }, */
	{ "INIT_ZM_REG"                       , 0x7A, 9       , 0       , 0       , init_zm_reg                     },
/*	{ "INIT_RAM_RESTRICT_ZM_REG_GROUP"    , 0x8F, x       , x       , x       , init_ram_restrict_zm_reg_group  }, */
/*	{ "INIT_COPY_ZM_REG"                  , 0x90, x       , x       , x       , init_copy_zm_reg                }, */
/*	{ "INIT_ZM_REG_GROUP_ADDRESS_LATCHED" , 0x91, x       , x       , x       , init_zm_reg_group_addr_latched  }, */
/*	{ "INIT_RESERVED"                     , 0x92, x       , x       , x       , init_reserved                   }, */
	{ 0                                   , 0   , 0       , 0       , 0       , 0                               }
};

static unsigned int get_init_table_entry_length(bios_t *bios, unsigned int offset, int i)
{
	/* Calculates the length of a given init table entry. */
	return itbl_entry[i].length + bios->data[offset + itbl_entry[i].length_offset]*itbl_entry[i].length_multiplier;
}

static void parse_init_table(ScrnInfoPtr pScrn, bios_t *bios, unsigned int offset, init_exec_t *iexec)
{
	/* Parses all commands in a init table. */

	/* We start out executing all commands found in the
	 * init table. Some op codes may change the status
	 * of this variable to SKIP, which will cause
	 * the following op codes to perform no operation until
	 * the value is changed back to EXECUTE.
	 */
	unsigned char id;
	int i;

	int count=0;
	/* Loop as long as INIT_DONE (command id 0x71) has not been found
	 * (and offset < bios length just in case... )
	 * (and no more than 10000 iterations just in case... ) */
	while (((id = bios->data[offset]) != 0x71) && (offset < bios->length) && (count++<10000)) {
		/* Find matching id in itbl_entry */
		for (i = 0; itbl_entry[i].name && (itbl_entry[i].id != id); i++)
			;

		if (itbl_entry[i].name) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X:  [ (0x%02X) -  %s ]\n", offset, 
				itbl_entry[i].id, itbl_entry[i].name);

			/* execute eventual command handler */
			if (itbl_entry[i].handler)
				if (!(*itbl_entry[i].handler)(pScrn, bios, offset, iexec))
					break;
		} else {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: Init table command not found: 0x%02X\n", 
				offset, id);
		}

		/* Add the offset of the current command including all data
		 * of that command. The offset will then be pointing on the
		 * next op code.
		 */
		offset += get_init_table_entry_length(bios, offset, i);
	}
}

void parse_init_tables(ScrnInfoPtr pScrn, bios_t *bios)
{
	/* Loops and calls parse_init_table() for each present table. */

	int i = 0;
	CARD16 table;
	init_exec_t iexec = {TRUE, FALSE};

	while (table = *((CARD16 *) (&bios->data[bios->init_tbls_offset + i]))) {
        
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: Parsing init table %d\n", 
			table, i / 2);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: ------ EXECUTING FOLLOWING COMMANDS ------\n",table);
		still_alive();
		parse_init_table(pScrn, bios, table, &iexec);         
		i += 2;
	}
}

static unsigned int parse_bit_init_tbl_entry(ScrnInfoPtr pScrn, bios_t *bios, bit_entry_t *bitentry)
{
	/* Parses the init table segment that the bit entry points to.
	 * Starting at bitentry->offset: 
	 * 
	 * offset + 0  (16 bits): offset of init tables
	 * offset + 2  (16 bits): macro index table offset
	 * offset + 4  (16 bits): macro offset
	 * offset + 6  (16 bits): condition offset
	 * offset + 8  (16 bits): io flag condition offset (?)
	 * offset + 10 (16 bits): io flag condition offset (?)
	 * offset + 12 (16 bits): unknown
	 *
	 * offset + 8 and offset + 10 seems to contain the same
	 * offsets on all bioses i have checked. Don't know which
	 * one is the correct, therefore this code will bail out
	 * if the two values are not the same.
	 *
	 * TODO:
	 * * In addition to "conditions" and "io flag conditions" there seems to be
	 *   "io conditions". These are probably located at offset + (8, 10 or 12).
	 *    We need more BIOS dumps to figure this out...
	 * 
	 * * Are 'I' bit entries always of length 0xE?
	 * 
	 */

	if (bitentry->length < 12) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "Unable to regocnize BIT init table entry.\n");
		return 0;
	}

	bios->init_tbls_offset = *((CARD16 *) (&bios->data[bitentry->offset]));
	bios->macro_index_offset = *((CARD16 *) (&bios->data[bitentry->offset + 2]));
	bios->macro_offset = *((CARD16 *) (&bios->data[bitentry->offset + 4]));
	bios->condition_offset = 
		*((CARD16 *) (&bios->data[bitentry->offset + 6]));
	if (*((CARD16 *) (&bios->data[bitentry->offset + 8])) != 
		*((CARD16 *) (&bios->data[bitentry->offset + 10]))) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "Unable to find IO flag condition offset.\n");
		return 0;
	}

	bios->io_flag_condition_offset =
		*((CARD16 *) (&bios->data[bitentry->offset + 8]));

	parse_init_tables(pScrn, bios);

	return 1;
}

static void parse_bit_structure(ScrnInfoPtr pScrn, bios_t *bios, unsigned int offset)
{
	bit_entry_t *bitentry;
	char done = 0;

	while (!done) {
		bitentry = (bit_entry_t *) &bios->data[offset];

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "0x%04X: Found BIT command with id 0x%02X\n", 
				offset, bitentry->id[0]); 

		switch (bitentry->id[0]) {
		case 0:
			/* id[0] = 0 and id[1] = 0  ==> end of BIT struture */
			if (bitentry->id[1] == 0)
				done = 1;
			break;
		case 'I':
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,  
				"0x%04X: Found init table entry in BIT structure.\n", 
				offset);

			parse_bit_init_tbl_entry(pScrn, bios, bitentry);
			/*parse_init_tables(pScrn, bios);*/
			break;
			
			/* TODO: What kind of information does the other BIT entrys point to?
			 *       'P' entry is probably performance tables, but there are
			 *       quite a few others...
			 */
		}

		offset += sizeof(bit_entry_t);
	}
}

static unsigned short brs(unsigned char *data, int offset)
{
  unsigned short ret;

  ret = (data[offset]) | ((data[offset+1]) << 8);
  return ret;
}

static void parse_pins_structure(ScrnInfoPtr pScrn, bios_t *bios, unsigned int offset)
{
	int pins_version_major=bios->data[offset+5];
	int pins_version_minor=bios->data[offset+6];
	int init1 = bios->data[offset + 18] + (bios->data[offset + 19] * 256);     
	int init2 = bios->data[offset + 20] + (bios->data[offset + 21] * 256);     
	int init_size = bios->data[offset + 22] + (bios->data[offset + 23] * 256) + 1;                                                    
	int ram_tab;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "PINS version %d.%d\n",pins_version_major,pins_version_minor);

#if 0
	if (pins_version_major==2)
		ram_tab = init1-0x0010;
	else
		ram_tab = bios->data[offset + 24] + (bios->data[offset + 25] * 256);

	if ((pins_version_major==5)&&(pins_version_minor>=6))
	{
		/* VCO range info */
	}

	if ((pins_version_major==5)&&(pins_version_minor>=16))
	{

	}

	parse_bit_init_tbl_entry(pScrn, bios, bitentry);

#endif
}

static unsigned int findstr(bios_t* bios, unsigned char *str, int len)
{
	int i;

	for (i = 2; i < bios->length; i++)
		if (strncmp(&bios->data[i], str, len) == 0)
			return i;

	return 0;
}

#define G5_FIXED_LOC 0xe2f8


static unsigned int nv_find_dcb_table(ScrnInfoPtr pScrn, bios_t *bios)
{
        NVPtr pNv = NVPTR(pScrn);
	CARD16 bufloc;
	int is_g5 = 0;
	CARD32 sig;
	char *table2;
	unsigned char headerSize, entries;
        CARD32 header_word;
	int i;
	int sig_offsets[2] = { 0x4, 0x6 };
	int offset = -1;

	/* get the offset from 0x36 */
	
	bufloc = *(CARD16 *)&bios->data[0x36];

	if (bufloc == 0x0) {
		if ((pNv->Chipset & 0x0ff0) == CHIPSET_NV43) {
			is_g5 = 1;
			bufloc = G5_FIXED_LOC;
		} else {
			return 0;
		}
	}
	
	table2 = &bios->data[bufloc];

	/* lets play hunt the signature */
	for (i = 0; i < sizeof(sig_offsets) / sizeof(int); i++) {
	  sig = *(uint32_t*)(table2 + sig_offsets[i]);
	  if ((sig == 0x4edcbdcb) || (sig == 0xcbbddc4e)) {
	    offset = sig_offsets[i];
	    break;
	  }
	}
	if (offset == -1)
	  return 0;

	if (offset == 6) {
	  header_word = *(uint32_t *)table2;
	  if (is_g5) {
	    headerSize = 0x3c;
	    entries = 0xa;
	  } else {
	    headerSize = (header_word >> 8) & 0xff;
	    entries = (header_word >> 16) & 0xff;
	  }
	} else {
	  entries = 0xa;
	  headerSize = 0x8;
	}

	ErrorF("DCB size is %02X, entries is %02X\n", headerSize, entries);
	if (entries >= NV40_NUM_DCB_ENTRIES)
		entries = NV40_NUM_DCB_ENTRIES;

	for (i = 0; i < entries; i++) {
		if (is_g5)
			pNv->dcb_table[i] = __bswap_32(*(uint32_t *)&table2[headerSize + 8 * i]);
		else
			pNv->dcb_table[i] = *(uint32_t *)&table2[headerSize + 8 * i];
	}

	return entries;
}


unsigned int NVParseBios(ScrnInfoPtr pScrn)
{
	unsigned int bit_offset;
	bios_t bios;
	bios.data=NULL;
	bios.length=NV_PROM_SIZE;
	unsigned char nv_signature[]={0xff,0x7f,'N','V',0x0};
	unsigned char bit_signature[]={'B','I','T'};
	NVPtr pNv;
	int i, ret;
	pNv = NVPTR(pScrn);

	if (!NVDownloadBios(pScrn, &bios)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "No valid BIOS image found.\n");
		return 0;
	}

	/* check for known signatures */
	if ((bit_offset = findstr(&bios, bit_signature, sizeof(bit_signature)))) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "BIT signature found.\n");
		parse_bit_structure(pScrn, &bios, bit_offset + 4);
	} else if ((bit_offset = findstr(&bios, nv_signature, sizeof(nv_signature)))) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "NV signature found.\n");
		parse_pins_structure(pScrn, &bios, bit_offset);
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "No known script signature found.\n");
	}

	/* look for NV40+ DCB table - and make a copy somewhere for output setup code */
	ret = nv_find_dcb_table(pScrn, &bios);
	if (ret)
	{
		pNv->dcb_entries = ret;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "DCB found %d entries.\n", ret);
	}
	else
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,  "No DCB table found\n");
	xfree(bios.data);
	return 1;
}
