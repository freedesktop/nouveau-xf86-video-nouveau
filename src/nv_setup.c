 /***************************************************************************\
|*                                                                           *|
|*       Copyright 2003 NVIDIA, Corporation.  All rights reserved.           *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 2003 NVIDIA, Corporation.  All rights reserved.           *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_setup.c,v 1.48 2005/09/14 02:28:03 mvojkovi Exp $ */

#include "nv_include.h"
#include "nvreg.h"

CARD32 debug_offset;

/*
 * Override VGA I/O routines.
 */
static void NVWriteCrtc(vgaHWPtr pVga, CARD8 index, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
    VGA_WR08(ptr, pVga->IOBase + VGA_CRTC_INDEX_OFFSET, index);
    VGA_WR08(ptr, pVga->IOBase + VGA_CRTC_DATA_OFFSET,  value);
}
static CARD8 NVReadCrtc(vgaHWPtr pVga, CARD8 index)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
    VGA_WR08(ptr, pVga->IOBase + VGA_CRTC_INDEX_OFFSET, index);
    return (VGA_RD08(ptr, pVga->IOBase + VGA_CRTC_DATA_OFFSET));
}
static void NVWriteGr(vgaHWPtr pVga, CARD8 index, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    VGA_WR08(pNv->PVIO, VGA_GRAPH_INDEX, index);
    VGA_WR08(pNv->PVIO, VGA_GRAPH_DATA,  value);
}
static CARD8 NVReadGr(vgaHWPtr pVga, CARD8 index)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    VGA_WR08(pNv->PVIO, VGA_GRAPH_INDEX, index);
    return (VGA_RD08(pNv->PVIO, VGA_GRAPH_DATA));
}
static void NVWriteSeq(vgaHWPtr pVga, CARD8 index, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    VGA_WR08(pNv->PVIO, VGA_SEQ_INDEX, index);
    VGA_WR08(pNv->PVIO, VGA_SEQ_DATA,  value);
}
static CARD8 NVReadSeq(vgaHWPtr pVga, CARD8 index)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    VGA_WR08(pNv->PVIO, VGA_SEQ_INDEX, index);
    return (VGA_RD08(pNv->PVIO, VGA_SEQ_DATA));
}
static void NVWriteAttr(vgaHWPtr pVga, CARD8 index, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
    volatile CARD8 tmp;

    tmp = VGA_RD08(ptr, pVga->IOBase + VGA_IN_STAT_1_OFFSET);
    if (pVga->paletteEnabled)
        index &= ~0x20;
    else
        index |= 0x20;
    VGA_WR08(ptr, VGA_ATTR_INDEX,  index);
    VGA_WR08(ptr, VGA_ATTR_DATA_W, value);
}
static CARD8 NVReadAttr(vgaHWPtr pVga, CARD8 index)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
    volatile CARD8 tmp;

    tmp = VGA_RD08(ptr, pVga->IOBase + VGA_IN_STAT_1_OFFSET);
    if (pVga->paletteEnabled)
        index &= ~0x20;
    else
        index |= 0x20;
    VGA_WR08(ptr, VGA_ATTR_INDEX, index);
    return (VGA_RD08(ptr, VGA_ATTR_DATA_R));
}
static void NVWriteMiscOut(vgaHWPtr pVga, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    VGA_WR08(pNv->PVIO, VGA_MISC_OUT_W, value);
}
static CARD8 NVReadMiscOut(vgaHWPtr pVga)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    return (VGA_RD08(pNv->PVIO, VGA_MISC_OUT_R));
}
static void NVEnablePalette(vgaHWPtr pVga)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
    volatile CARD8 tmp;

    tmp = VGA_RD08(ptr, pVga->IOBase + VGA_IN_STAT_1_OFFSET);
    VGA_WR08(ptr, VGA_ATTR_INDEX, 0x00);
    pVga->paletteEnabled = TRUE;
}
static void NVDisablePalette(vgaHWPtr pVga)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PCIO1 : pNv->PCIO0;
    volatile CARD8 tmp;

    tmp = VGA_RD08(ptr, pVga->IOBase + VGA_IN_STAT_1_OFFSET);
    VGA_WR08(ptr, VGA_ATTR_INDEX, 0x20);
    pVga->paletteEnabled = FALSE;
}
static void NVWriteDacMask(vgaHWPtr pVga, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PDIO1 : pNv->PDIO0;
    VGA_WR08(ptr, VGA_DAC_MASK, value);
}
static CARD8 NVReadDacMask(vgaHWPtr pVga)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PDIO1 : pNv->PDIO0;
    return (VGA_RD08(ptr, VGA_DAC_MASK));
}
static void NVWriteDacReadAddr(vgaHWPtr pVga, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PDIO1 : pNv->PDIO0;
    VGA_WR08(ptr, VGA_DAC_READ_ADDR, value);
}
static void NVWriteDacWriteAddr(vgaHWPtr pVga, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PDIO1 : pNv->PDIO0;
    VGA_WR08(ptr, VGA_DAC_WRITE_ADDR, value);
}
static void NVWriteDacData(vgaHWPtr pVga, CARD8 value)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PDIO1 : pNv->PDIO0;
    VGA_WR08(ptr, VGA_DAC_DATA, value);
}
static CARD8 NVReadDacData(vgaHWPtr pVga)
{
    NVPtr pNv = (NVPtr)pVga->MMIOBase;
    volatile CARD8 *ptr = pNv->cur_head ? pNv->PDIO1 : pNv->PDIO0;
    return (VGA_RD08(ptr, VGA_DAC_DATA));
}

static Bool 
NVIsConnected (ScrnInfoPtr pScrn, int output)
{
    NVPtr pNv = NVPTR(pScrn);
    CARD32 reg52C, reg608, temp;
    Bool present;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Probing for analog device on output %s...\n", 
                output ? "B" : "A");

    reg52C = nvReadRAMDAC(pNv, output, NV_RAMDAC_052C);
    reg608 = nvReadRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL);

    nvWriteRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL, (reg608 & ~0x00010000));

    nvWriteRAMDAC(pNv, output, NV_RAMDAC_052C, (reg52C & 0x0000FEEE));
    usleep(1000);
    
    temp = nvReadRAMDAC(pNv, output, NV_RAMDAC_052C);
    nvWriteRAMDAC(pNv, output, NV_RAMDAC_052C, temp | 1);

    nvWriteRAMDAC(pNv, output, NV_RAMDAC_TEST_DATA, 0x94050140);
    temp = nvReadRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL);
    nvWriteRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL, temp | 0x1000);

    usleep(1000);

    present = (nvReadRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL) & (1 << 28)) ? TRUE : FALSE;

    if(present)
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "  ...found one\n");
    else
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "  ...can't find one\n");

    temp = nvReadRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL);
    nvWriteRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL, temp & 0x000EFFF);

    nvWriteRAMDAC(pNv, output, NV_RAMDAC_052C, reg52C);
    nvWriteRAMDAC(pNv, output, NV_RAMDAC_TEST_CONTROL, reg608);

    return present;
}

static void
NVSelectHeadRegisters(ScrnInfoPtr pScrn, int head)
{
    NVPtr pNv = NVPTR(pScrn);

    pNv->cur_head = head;
}

static xf86MonPtr 
NVProbeDDC (ScrnInfoPtr pScrn, int bus)
{
    NVPtr pNv = NVPTR(pScrn);
    xf86MonPtr MonInfo = NULL;

    if(!pNv->I2C) return NULL;

    pNv->DDCBase = bus ? 0x36 : 0x3e;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, 
               "Probing for EDID on I2C bus %s...\n", bus ? "B" : "A");

    if ((MonInfo = xf86DoEDID_DDC2(pScrn->scrnIndex, pNv->I2C))) {
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                  "DDC detected a %s:\n", MonInfo->features.input_type ?
                  "DFP" : "CRT");
       xf86PrintEDID( MonInfo );
    } else {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, 
                  "  ... none found\n");
    }

    return MonInfo;
}

static void nv3GetConfig (NVPtr pNv)
{
    CARD32 reg_FB0 = nvReadFB(pNv, 0x0);
    switch (reg_FB0 & 0x00000003) {
    case 0:
        pNv->RamAmountKBytes = 1024 * 8;
        break;
    case 1:
        pNv->RamAmountKBytes = 1024 * 2;
        break;
    case 2:
        pNv->RamAmountKBytes = 1024 * 4;
        break;
    default:
        pNv->RamAmountKBytes = 1024 * 8;
        break;
    }
    pNv->CrystalFreqKHz   = (nvReadPEXTDEV(pNv, 0x0000) & 0x00000040) ? 14318 : 13500;
    pNv->CURSOR           = &(pNv->PRAMIN[0x00008000/4 - 0x0800/4]);
    pNv->MinVClockFreqKHz = 12000;
    pNv->MaxVClockFreqKHz = 256000;
}

static void nv4GetConfig (NVPtr pNv)
{
    CARD32 reg_FB0 = nvReadFB(pNv, 0x0);
    if (reg_FB0 & 0x00000100) {
        pNv->RamAmountKBytes = ((reg_FB0 >> 12) & 0x0F) * 1024 * 2
                              + 1024 * 2;
    } else {
        switch (reg_FB0 & 0x00000003) {
        case 0:
            pNv->RamAmountKBytes = 1024 * 32;
            break;
        case 1:
            pNv->RamAmountKBytes = 1024 * 4;
            break;
        case 2:
            pNv->RamAmountKBytes = 1024 * 8;
            break;
        case 3:
        default:
            pNv->RamAmountKBytes = 1024 * 16;
            break;
        }
    }
    pNv->CrystalFreqKHz = (nvReadEXTDEV(pNv, 0x0000) & 0x00000040) ? 14318 : 13500;
    pNv->CURSOR         = &(pNv->PRAMIN[0x1E00]);
    pNv->MinVClockFreqKHz = 12000;
    pNv->MaxVClockFreqKHz = 350000;
}

static void nv10GetConfig (NVPtr pNv)
{
    CARD32 implementation = pNv->Chipset & 0x0ff0;

#if X_BYTE_ORDER == X_BIG_ENDIAN
    /* turn on big endian register access */
    if(!(nvReadMC(pNv, 0x0004) & 0x01000001)) {
       nvWriteMC(pNv, 0x0004, 0x01000001);
       mem_barrier();
    }
#endif

    if(implementation == CHIPSET_NFORCE) {
        int amt = pciReadLong(pciTag(0, 0, 1), 0x7C);
        pNv->RamAmountKBytes = (((amt >> 6) & 31) + 1) * 1024;
    } else if(implementation == CHIPSET_NFORCE2) {
        int amt = pciReadLong(pciTag(0, 0, 1), 0x84);
        pNv->RamAmountKBytes = (((amt >> 4) & 127) + 1) * 1024;
    } else {
        pNv->RamAmountKBytes = (nvReadFB(pNv, 0x020C) & 0xFFF00000) >> 10;
    }

    if(pNv->RamAmountKBytes > 256*1024)
        pNv->RamAmountKBytes = 256*1024;

    pNv->CrystalFreqKHz = (nvReadEXTDEV(pNv, 0x0000) & (1 << 6)) ? 14318 : 13500;
    
    if(pNv->twoHeads && (implementation != CHIPSET_NV11))
    {
       if(nvReadEXTDEV(pNv, 0x0000) & (1 << 22))
           pNv->CrystalFreqKHz = 27000;
    }

    pNv->CURSOR           = NULL;  /* can't set this here */
    pNv->MinVClockFreqKHz = 12000;
    pNv->MaxVClockFreqKHz = pNv->twoStagePLL ? 400000 : 350000;
}

void
NVCommonSetup(ScrnInfoPtr pScrn)
{
    NVPtr pNv = NVPTR(pScrn);
    vgaHWPtr pVga = VGAHWPTR(pScrn);
    CARD16 implementation = pNv->Chipset & 0x0ff0;
    
    /*
     * Override VGA I/O routines.
     */
    pVga->writeCrtc         = NVWriteCrtc;
    pVga->readCrtc          = NVReadCrtc;
    pVga->writeGr           = NVWriteGr;
    pVga->readGr            = NVReadGr;
    pVga->writeAttr         = NVWriteAttr;
    pVga->readAttr          = NVReadAttr;
    pVga->writeSeq          = NVWriteSeq;
    pVga->readSeq           = NVReadSeq;
    pVga->writeMiscOut      = NVWriteMiscOut;
    pVga->readMiscOut       = NVReadMiscOut;
    pVga->enablePalette     = NVEnablePalette;
    pVga->disablePalette    = NVDisablePalette;
    pVga->writeDacMask      = NVWriteDacMask;
    pVga->readDacMask       = NVReadDacMask;
    pVga->writeDacWriteAddr = NVWriteDacWriteAddr;
    pVga->writeDacReadAddr  = NVWriteDacReadAddr;
    pVga->writeDacData      = NVWriteDacData;
    pVga->readDacData       = NVReadDacData;
    /*
     * Note: There are different pointers to the CRTC/AR and GR/SEQ registers.
     * Bastardize the intended uses of these to make it work.
     */
    pVga->MMIOBase   = (CARD8 *)pNv;
    pVga->MMIOOffset = 0;
    
    pNv->REGS = xf86MapPciMem(pScrn->scrnIndex, 
                              VIDMEM_MMIO | VIDMEM_READSIDEEFFECT, 
                              pNv->PciTag, pNv->IOAddress, 0x01000000);
    
    debug_offset = pNv->REGS;
    pNv->PRAMIN   = pNv->REGS + (NV_PRAMIN_OFFSET/4);
    pNv->PCRTC0   = pNv->REGS + (NV_PCRTC0_OFFSET/4);
    pNv->PRAMDAC0 = pNv->REGS + (NV_PRAMDAC0_OFFSET/4);
    pNv->PFB      = pNv->REGS + (NV_PFB_OFFSET/4);
    pNv->PFIFO    = pNv->REGS + (NV_PFIFO_OFFSET/4);
    pNv->PGRAPH   = pNv->REGS + (NV_PGRAPH_OFFSET/4);
    pNv->PEXTDEV  = pNv->REGS + (NV_PEXTDEV_OFFSET/4);
    pNv->PTIMER   = pNv->REGS + (NV_PTIMER_OFFSET/4);
    pNv->PVIDEO   = pNv->REGS + (NV_PVIDEO_OFFSET/4);
    pNv->PMC      = pNv->REGS + (NV_PMC_OFFSET/4);

    /* 8 bit registers */
    pNv->PCIO0    = (CARD8*)pNv->REGS + NV_PCIO0_OFFSET;
    pNv->PDIO0    = (CARD8*)pNv->REGS + NV_PDIO0_OFFSET;
    pNv->PVIO     = (CARD8*)pNv->REGS + NV_PVIO_OFFSET;
    pNv->PROM     = (CARD8*)pNv->REGS + NV_PROM_OFFSET;

    pNv->PCRTC1   = pNv->PCRTC0 + 0x800;
    pNv->PRAMDAC1 = pNv->PRAMDAC0 + 0x800;
    pNv->PCIO1    = pNv->PCIO0 + 0x2000;
    pNv->PDIO1    = pNv->PDIO0 + 0x2000;

    pNv->twoHeads =  (pNv->Architecture >= NV_ARCH_10) &&
                     (implementation != CHIPSET_NV10) &&
                     (implementation != CHIPSET_NV15) &&
                     (implementation != CHIPSET_NFORCE) &&
                     (implementation != CHIPSET_NV20);

    pNv->fpScaler = (pNv->FpScale && pNv->twoHeads && (implementation!=CHIPSET_NV11));

    pNv->twoStagePLL = (implementation == CHIPSET_NV31) ||
                       (implementation == CHIPSET_NV36) ||
                       (pNv->Architecture >= NV_ARCH_40);

    pNv->WaitVSyncPossible = (pNv->Architecture >= NV_ARCH_10) &&
                             (implementation != CHIPSET_NV10);

    pNv->BlendingPossible = ((pNv->Chipset & 0xffff) > CHIPSET_NV04);



    /* Parse the bios to initialize the card */
    NVSelectHeadRegisters(pScrn, 0);
    NVParseBios(pScrn);
    /* reset PFIFO and PGRAPH, then power up all the card units */
    nvWriteMC(pNv, 0x200, 0x17110013);
    usleep(1000);
    nvWriteMC(pNv, 0x200, 0x17111113);

    if(pNv->Architecture == NV_ARCH_03)
        nv3GetConfig(pNv);
    else if(pNv->Architecture == NV_ARCH_04)
        nv4GetConfig(pNv);
    else
        nv10GetConfig(pNv);

    NVSelectHeadRegisters(pScrn, 0);

    NVLockUnlock(pNv, 0);
      
    pNv->vtOWNER = nvReadVGA(pNv, NV_VGA_CRTCX_OWNER);
}


void NVPreInitOldCode(ScrnInfoPtr pScrn)
{
  NVPtr pNv = NVPTR(pScrn);
    xf86MonPtr monitorA, monitorB;
    Bool mobile = FALSE;
    Bool tvA = FALSE;
    Bool tvB = FALSE;
    int FlatPanel = -1;   /* really means the CRTC is slaved */
    Bool Television = FALSE;
    CARD16 implementation = pNv->Chipset & 0x0ff0;

    /* look for known laptop chips */
    /* FIXME we could add some ids here (0x0164,0x0167,0x0168,0x01D6,0x01D7,0x01D8,0x0298,0x0299,0x0398) */
    switch(pNv->Chipset & 0xffff) {
    case 0x0112:
    case 0x0174:
    case 0x0175:
    case 0x0176:
    case 0x0177:
    case 0x0179:
    case 0x017C:
    case 0x017D:
    case 0x0186:
    case 0x0187:
    case 0x018D:
    case 0x0228:
    case 0x0286:
    case 0x028C:
    case 0x0316:
    case 0x0317:
    case 0x031A:
    case 0x031B:
    case 0x031C:
    case 0x031D:
    case 0x031E:
    case 0x031F:
    case 0x0324:
    case 0x0325:
    case 0x0328:
    case 0x0329:
    case 0x032C:
    case 0x032D:
    case 0x0347:
    case 0x0348:
    case 0x0349:
    case 0x034B:
    case 0x034C:
    case 0x0160:
    case 0x0166:
    case 0x0169:
    case 0x016B:
    case 0x016C:
    case 0x016D:
    case 0x00C8:
    case 0x00CC:
    case 0x0144:
    case 0x0146:
    case 0x0148:
    case 0x0098:
    case 0x0099:
        mobile = TRUE;
        break;
    default:
        break;
    }
    pNv->Television = FALSE;

    if(!pNv->twoHeads) {
       pNv->CRTCnumber = 0;
       if((monitorA = NVProbeDDC(pScrn, 0))) {
           FlatPanel = monitorA->features.input_type ? 1 : 0;

           /* NV4 doesn't support FlatPanels */
           if((pNv->Chipset & 0x0fff) <= CHIPSET_NV04)
              FlatPanel = 0;
       } else {
           if(nvReadVGA(pNv, NV_VGA_CRTCX_PIXEL) & 0x80) {
              if(!(nvReadVGA(pNv, NV_VGA_CRTCX_LCD) & 0x01)) 
                 Television = TRUE;
              FlatPanel = 1;
           } else {
              FlatPanel = 0;
           }
           xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                         "HW is currently programmed for %s\n",
                          FlatPanel ? (Television ? "TV" : "DFP") : "CRT");
       } 

       if(pNv->FlatPanel == -1) {
           pNv->FlatPanel = FlatPanel;
           pNv->Television = Television;
       } else {
           xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                      "Forcing display type to %s as specified\n", 
                       pNv->FlatPanel ? "DFP" : "CRT");
       }
    } else {
       CARD8 outputAfromCRTC, outputBfromCRTC;
       int CRTCnumber = -1;
       CARD8 slaved_on_A, slaved_on_B;
       Bool analog_on_A, analog_on_B;
       CARD32 oldhead;
       CARD8 cr44;
      
       if(implementation != CHIPSET_NV11) {
           if(nvReadRAMDAC0(pNv, NV_RAMDAC_052C) & 0x100)
               outputAfromCRTC = 1;
           else            
               outputAfromCRTC = 0;
           if(nvReadRAMDAC(pNv, 1, NV_RAMDAC_052C) & 0x100)
               outputBfromCRTC = 1;
           else
               outputBfromCRTC = 0;
          analog_on_A = NVIsConnected(pScrn, 0);
          analog_on_B = NVIsConnected(pScrn, 1);
       } else {
          outputAfromCRTC = 0;
          outputBfromCRTC = 1;
          analog_on_A = FALSE;
          analog_on_B = FALSE;
       }

       cr44 = nvReadVGA(pNv, NV_VGA_CRTCX_OWNER);
       pNv->vtOWNER = cr44;

       nvWriteVGA(pNv, NV_VGA_CRTCX_OWNER, 3);
       NVSelectHeadRegisters(pScrn, 1);
       NVLockUnlock(pNv, 0);

       slaved_on_B = nvReadVGA(pNv, NV_VGA_CRTCX_PIXEL) & 0x80;
       if(slaved_on_B) {
           tvB = !(nvReadVGA(pNv, NV_VGA_CRTCX_LCD) & 0x01);
       }

       nvWriteVGA(pNv, NV_VGA_CRTCX_OWNER, 0);
       NVSelectHeadRegisters(pScrn, 0);
       NVLockUnlock(pNv, 0);

       slaved_on_A = nvReadVGA(pNv, NV_VGA_CRTCX_PIXEL) & 0x80; 
       if(slaved_on_A) {
           tvA = !(nvReadVGA(pNv, NV_VGA_CRTCX_LCD) & 0x01);
       }

       oldhead = nvReadCRTC0(pNv, NV_CRTC_HEAD_CONFIG);
       nvWriteCRTC0(pNv, NV_CRTC_HEAD_CONFIG, oldhead | 0x00000010);

       monitorA = NVProbeDDC(pScrn, 0);
       monitorB = NVProbeDDC(pScrn, 1);

       if(slaved_on_A && !tvA) {
          CRTCnumber = 0;
          FlatPanel = 1;
          xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "CRTC 0 is currently programmed for DFP\n");
       } else 
       if(slaved_on_B && !tvB) {
          CRTCnumber = 1;
          FlatPanel = 1;
          xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "CRTC 1 is currently programmed for DFP\n");
       } else
       if(analog_on_A) {
          CRTCnumber = outputAfromCRTC;
          FlatPanel = 0;
          xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "CRTC %i appears to have a CRT attached\n", CRTCnumber);
       } else
       if(analog_on_B) {
           CRTCnumber = outputBfromCRTC;
           FlatPanel = 0;
           xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "CRTC %i appears to have a CRT attached\n", CRTCnumber);
       } else
       if(slaved_on_A) {
          CRTCnumber = 0;
          FlatPanel = 1;
          Television = 1;
          xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "CRTC 0 is currently programmed for TV\n");
       } else
       if(slaved_on_B) {
          CRTCnumber = 1;
          FlatPanel = 1;
          Television = 1;
          xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "CRTC 1 is currently programmed for TV\n");
       } else
       if(monitorA) {
           FlatPanel = monitorA->features.input_type ? 1 : 0;
       } else 
       if(monitorB) {
           FlatPanel = monitorB->features.input_type ? 1 : 0;
       }

       if(pNv->FlatPanel == -1) {
          if(FlatPanel != -1) {
             pNv->FlatPanel = FlatPanel;
             pNv->Television = Television;
          } else {
             xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Unable to detect display type...\n");
             if(mobile) {
                 xf86DrvMsg(pScrn->scrnIndex, X_DEFAULT,
                            "...On a laptop, assuming DFP\n");
                 pNv->FlatPanel = 1;
             } else {
                 xf86DrvMsg(pScrn->scrnIndex, X_DEFAULT,
                            "...Using default of CRT\n");
                 pNv->FlatPanel = 0;
             }
          }
       } else {
           xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                      "Forcing display type to %s as specified\n", 
                       pNv->FlatPanel ? "DFP" : "CRT");
       }

       if(pNv->CRTCnumber == -1) {
          if(CRTCnumber != -1) pNv->CRTCnumber = CRTCnumber;
          else {
             xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Unable to detect which CRTCNumber...\n");
             if(pNv->FlatPanel) pNv->CRTCnumber = 1;
             else pNv->CRTCnumber = 0;
             xf86DrvMsg(pScrn->scrnIndex, X_DEFAULT,
                        "...Defaulting to CRTCNumber %i\n", pNv->CRTCnumber);
          }
       } else {
           xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                      "Forcing CRTCNumber %i as specified\n", pNv->CRTCnumber);
       }
     
       if(monitorA) {
           if((monitorA->features.input_type && pNv->FlatPanel) ||
              (!monitorA->features.input_type && !pNv->FlatPanel))
           {
               if(monitorB) { 
                  xfree(monitorB);
                  monitorB = NULL;
               }
           } else {
              xfree(monitorA);
              monitorA = NULL;
           }
       }

       if(monitorB) {
           if((monitorB->features.input_type && !pNv->FlatPanel) ||
              (!monitorB->features.input_type && pNv->FlatPanel)) 
           {
              xfree(monitorB);
           } else {
              monitorA = monitorB;
           }
           monitorB = NULL;
       }

       if(implementation == CHIPSET_NV11)
           cr44 = pNv->CRTCnumber * 0x3;

       nvWriteCRTC0(pNv, NV_CRTC_HEAD_CONFIG,  oldhead);

       nvWriteVGA(pNv, NV_VGA_CRTCX_OWNER, cr44);
       NVSelectHeadRegisters(pScrn, pNv->CRTCnumber);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
              "Using %s on CRTC %i\n",
              pNv->FlatPanel ? (pNv->Television ? "TV" : "DFP") : "CRT", 
              pNv->CRTCnumber);

    if(pNv->FlatPanel && !pNv->Television) {
       pNv->fpWidth = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_HDISP_END) + 1;
       pNv->fpHeight = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_VDISP_END) + 1;
       pNv->fpSyncs = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_CONTROL) & 0x30000033;
       xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Panel size is %i x %i\n",
                  pNv->fpWidth, pNv->fpHeight);
    }

    if(monitorA)
      xf86SetDDCproperties(pScrn, monitorA);

    if(!pNv->FlatPanel || (pScrn->depth != 24) || !pNv->twoHeads)
        pNv->FPDither = FALSE;

    pNv->LVDS = FALSE;
    if(pNv->FlatPanel && pNv->twoHeads) {
        nvWriteRAMDAC0(pNv, NV_RAMDAC_FP_TMDS_DATA, 0x00010004);
        if(nvReadRAMDAC0(pNv, NV_RAMDAC_FP_TMDS_LVDS) & 1)
           pNv->LVDS = TRUE;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Panel is %s\n", 
                   pNv->LVDS ? "LVDS" : "TMDS");
    }
}

