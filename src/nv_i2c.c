/*
 * Copyright 2006 Stephane Marchesin
 * Copyright 2007 Maarten Maathuis
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

/*
 * DDC1 support only requires DDC_SDA_MASK,
 * DDC2 support requires DDC_SDA_MASK and DDC_SCL_MASK
 */
#define DDC_SDA_READ_MASK  (1 << 3)
#define DDC_SCL_READ_MASK  (1 << 2)
#define DDC_SDA_WRITE_MASK (1 << 4)
#define DDC_SCL_WRITE_MASK (1 << 5)

static void
NVI2CGetBits(I2CBusPtr b, int *clock, int *data)
{
	NVPtr pNv = NVPTR(xf86Screens[b->scrnIndex]);
	unsigned char val;

	/* Get the result. */
	/* Doing this on head 0 seems fine. */
	if (pNv->VBIOS.chip_version == 0x51)
		val = NVReadCRTC(pNv, 0, b->DriverPrivate.uval) >> 16;
	else
		val = NVReadVgaCrtc(pNv, 0, b->DriverPrivate.uval);

	*clock = (val & DDC_SCL_READ_MASK) != 0;
	*data  = (val & DDC_SDA_READ_MASK) != 0;
}

static void
NVI2CPutBits(I2CBusPtr b, int clock, int data)
{
	NVPtr pNv = NVPTR(xf86Screens[b->scrnIndex]);
	uint32_t val;

	/* Doing this on head 0 seems fine. */
	if (pNv->VBIOS.chip_version == 0x51)
		val = NVReadCRTC(pNv, 0, b->DriverPrivate.uval);
	else
		val = NVReadVgaCrtc(pNv, 0, b->DriverPrivate.uval + 1);

	val = (val & ~0xf) | 1;

	if (clock)
		val |= DDC_SCL_WRITE_MASK;
	else
		val &= ~DDC_SCL_WRITE_MASK;

	if (data)
		val |= DDC_SDA_WRITE_MASK;
	else
		val &= ~DDC_SDA_WRITE_MASK;

	/* Doing this on head 0 seems fine. */
	if (pNv->VBIOS.chip_version == 0x51)
		NVWriteCRTC(pNv, 0, b->DriverPrivate.uval, val);
	else
		NVWriteVgaCrtc(pNv, 0, b->DriverPrivate.uval + 1, val);
}

static uint32_t NV50_GetI2CPort(ScrnInfoPtr pScrn, int index)
{
	uint32_t reg;

	switch (index) {
		case 0:
			reg = NV50_PCONNECTOR_I2C_PORT_0;
			break;
		case 1:
			reg = NV50_PCONNECTOR_I2C_PORT_1;
			break;
		case 2:
			reg = NV50_PCONNECTOR_I2C_PORT_2;
			break;
		case 3:
			reg = NV50_PCONNECTOR_I2C_PORT_3;
			break;
		case 4:
			reg = NV50_PCONNECTOR_I2C_PORT_4;
			break;
		case 5:
			reg = NV50_PCONNECTOR_I2C_PORT_5;
			break;
		default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Invalid i2c port number %d, defaulting to 0.\n", index);
			reg = NV50_PCONNECTOR_I2C_PORT_0;
			break;
	}

	return reg;
}

static void NV50_I2CPutBits(I2CBusPtr b, int clock, int data)
{
	NVPtr pNv = NVPTR(xf86Screens[b->scrnIndex]);

	NVWrite(pNv, NV50_GetI2CPort(xf86Screens[b->scrnIndex], b->DriverPrivate.val), (4 | clock | data << 1));
}

static void NV50_I2CGetBits(I2CBusPtr b, int *clock, int *data)
{
	NVPtr pNv = NVPTR(xf86Screens[b->scrnIndex]);
	unsigned char val;

	val = NVRead(pNv, NV50_GetI2CPort(xf86Screens[b->scrnIndex], b->DriverPrivate.val));
	*clock = !!(val & 1);
	*data = !!(val & 2);
}

Bool
NV_I2CInit(ScrnInfoPtr pScrn, I2CBusPtr *bus_ptr, int i2c_reg, char *name)
{
	I2CBusPtr pI2CBus;
	NVPtr pNv = NVPTR(pScrn);

	pI2CBus = xf86CreateI2CBusRec();
	if(!pI2CBus)
		return FALSE;

	pI2CBus->BusName    = name;
	pI2CBus->scrnIndex  = pScrn->scrnIndex;
	if (pNv->Architecture == NV_ARCH_50) {
		pI2CBus->I2CPutBits = NV50_I2CPutBits;
		pI2CBus->I2CGetBits = NV50_I2CGetBits;
		/* Could this be used for the rest as well? */
		pI2CBus->ByteTimeout = 2200; /* VESA DDC spec 3 p. 43 (+10 %) */
		pI2CBus->StartTimeout = 550;
		pI2CBus->BitTimeout = 40;
		pI2CBus->ByteTimeout = 40;
		pI2CBus->AcknTimeout = 40;
	} else {
		pI2CBus->I2CPutBits = NVI2CPutBits;
		pI2CBus->I2CGetBits = NVI2CGetBits;
		pI2CBus->AcknTimeout = 5;
	}

	if (pNv->VBIOS.chip_version == 0x51)
		pI2CBus->DriverPrivate.uval = 0x600800 + i2c_reg;
	else
		pI2CBus->DriverPrivate.uval = i2c_reg;

	if (!xf86I2CBusInit(pI2CBus))
		return FALSE;

	*bus_ptr = pI2CBus;
	return TRUE;
}

