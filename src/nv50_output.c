/*
 * Copyright (c) 2007 NVIDIA, Corporation
 * Copyright (c) 2008 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>

#include "nv_include.h"

#include <xf86DDC.h>

#include "nouveau_crtc.h"
#include "nouveau_output.h"
#include "nouveau_connector.h"

int
NV50OrOffset(nouveauOutputPtr output)
{
	return ffs(output->dcb->or) - 1;
}

static void
NV50OutputInit(ScrnInfoPtr pScrn, int dcb_entry, char *outputname, int bus_count)
{
	NVPtr pNv = NVPTR(pScrn);
	int i;

	int i2c_index = pNv->dcb_table.entry[dcb_entry].i2c_index;
	int bus = pNv->dcb_table.entry[dcb_entry].bus;

	char connector_name[20];

	/* I2C buses belong to the connector, but can only inited once we know the outputs. */
	sprintf(connector_name, "Connector-%d", bus);

	/* Give the connectors better names if possible. */
	switch (pNv->dcb_table.entry[dcb_entry].type) {
		case OUTPUT_LVDS:
			sprintf(connector_name, "LVDS-%d", bus);
			break;
		case OUTPUT_TMDS:
			sprintf(connector_name, "DVI-%d", bus);
			break;
		case OUTPUT_ANALOG:
			if (bus_count > 1) /* DVI-I */
				sprintf(connector_name, "DVI-%d", bus);
			else
				sprintf(connector_name, "VGA-%d", bus);
			break;
		case OUTPUT_TV:
			sprintf(connector_name, "TV-%d", bus);
			break;
		default:
			break;
	}

	xfree(pNv->connector[bus]->name);
	pNv->connector[bus]->name = xstrdup(connector_name);

	if (i2c_index < 0x10 && pNv->pI2CBus[i2c_index] == NULL)
		NV_I2CInit(pScrn, &pNv->pI2CBus[i2c_index], pNv->dcb_table.i2c_read[i2c_index], xstrdup(connector_name));

	pNv->connector[bus]->i2c_index = i2c_index;
	pNv->connector[bus]->pDDCBus = pNv->pI2CBus[i2c_index];

	if (pNv->dcb_table.entry[dcb_entry].type == OUTPUT_TV)
		return; /* unsupported */

	/* Create output. */
	nouveauOutputPtr output = xnfcalloc(sizeof(nouveauOutputRec), 1);
	output->name = xstrdup(outputname);
	output->dcb = &pNv->dcb_table.entry[dcb_entry];
	output->type = pNv->dcb_table.entry[dcb_entry].type;
	output->scrn = pScrn;

	/* Put the output in the connector's list of outputs. */
	for (i = 0; i < MAX_OUTPUTS_PER_CONNECTOR; i++) {
		if (pNv->connector[bus]->outputs[i]) /* filled */
			continue;
		pNv->connector[bus]->outputs[i] = output;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s attached with index %d to %s\n", outputname, i, connector_name);
		break;
	}

	/* Put ourselves in the main output list. */
	if (!pNv->output) {
		pNv->output = output;
	} else {
		nouveauOutputPtr output_link = pNv->output;
		if (output_link->next) {
			do {
				output_link = output_link->next;
			} while (output_link->next);
		}
		output_link->next = output;
	}

	/* Output property for tmds and lvds. */
	output->dithering = (pNv->FPDither || (output->type == OUTPUT_LVDS && !pNv->VBIOS.fp.if_is_24bit));

	if (output->type == OUTPUT_LVDS || output->type == OUTPUT_TMDS) {
		if (pNv->fpScaler) /* GPU Scaling */
			output->scale_mode = SCALE_ASPECT;
		else if (output->type == OUTPUT_LVDS)
			output->scale_mode = SCALE_NOSCALE;
		else
			output->scale_mode = SCALE_PANEL;

		if (xf86GetOptValString(pNv->Options, OPTION_SCALING_MODE)) {
			output->scale_mode = nv_scaling_mode_lookup(xf86GetOptValString(pNv->Options, OPTION_SCALING_MODE), -1);
			if (output->scale_mode == SCALE_INVALID)
				output->scale_mode = SCALE_ASPECT; /* default */
		}
	}

	/* NV5x scaling hardware seems to work fine for analog too. */
	if (output->type == OUTPUT_ANALOG) {
		output->scale_mode = SCALE_PANEL;

		if (xf86GetOptValString(pNv->Options, OPTION_SCALING_MODE)) {
			output->scale_mode = nv_scaling_mode_lookup(xf86GetOptValString(pNv->Options, OPTION_SCALING_MODE), -1);
			if (output->scale_mode == SCALE_INVALID)
				output->scale_mode = SCALE_PANEL; /* default */
		}
	}

	/* Usually 3, which means both crtc's. */
	output->allowed_crtc = output->dcb->heads;

	if (output->type == OUTPUT_TMDS) {
		NVWrite(pNv, NV50_SOR0_UNK00C + NV50OrOffset(output) * 0x800, 0x03010700);
		NVWrite(pNv, NV50_SOR0_UNK010 + NV50OrOffset(output) * 0x800, 0x0000152f);
		NVWrite(pNv, NV50_SOR0_UNK014 + NV50OrOffset(output) * 0x800, 0x00000000);
		NVWrite(pNv, NV50_SOR0_UNK018 + NV50OrOffset(output) * 0x800, 0x00245af8);
	}

	if (output->type == OUTPUT_LVDS) {
		/* now fpindex should be known, so reread the table here. */
		/* not that we do a lot with the information. */
		parse_lvds_manufacturer_table(pScrn, &pNv->VBIOS, 0);
	}

	/* This needs to be handled in the same way as pre-NV5x on the long run. */
	//if (output->type == OUTPUT_LVDS)
	//	pNv->VBIOS.fp.native_mode = GetLVDSNativeMode(pScrn);

	/* Function pointers. */
	if (output->type == OUTPUT_TMDS || output->type == OUTPUT_LVDS) {
		NV50SorSetFunctionPointers(output);
	} else if (output->type == OUTPUT_ANALOG || output->type == OUTPUT_TV) {
		NV50DacSetFunctionPointers(output);
	}
}

void
NV50OutputSetup(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	int i, type, i2c_index, bus, bus_count[0xf];
	char outputname[20];
	uint32_t index;

	memset(pNv->pI2CBus, 0, sizeof(pNv->pI2CBus));
	memset(bus_count, 0, sizeof(bus_count));

	for (i = 0 ; i < pNv->dcb_table.entries; i++)
		bus_count[pNv->dcb_table.entry[i].bus]++;

	/* we setup the outputs up from the BIOS table */
	for (i = 0 ; i < pNv->dcb_table.entries; i++) {
		type = pNv->dcb_table.entry[i].type;
		i2c_index = pNv->dcb_table.entry[i].i2c_index;
		bus = pNv->dcb_table.entry[i].bus;

		xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "DCB entry %d: type: %d, i2c_index: %d, heads: %d, bus: %d, or: %d\n", i, type, pNv->dcb_table.entry[i].i2c_index, pNv->dcb_table.entry[i].heads, pNv->dcb_table.entry[i].bus, pNv->dcb_table.entry[i].or);

		/* SOR-0, SOR-1, DAC-0, DAC-1 or DAC-2. */
		index = ffs(pNv->dcb_table.entry[i].or) - 1;

		switch (type) {
		case OUTPUT_ANALOG:
			sprintf(outputname, "DAC-%d", index);
			break;
		case OUTPUT_TMDS:
			sprintf(outputname, "SOR-%d", index);
			break;
		case OUTPUT_TV: /* this does not handle shared dac's yet. */
			sprintf(outputname, "DAC-%d", index);
			break;
		case OUTPUT_LVDS:
			sprintf(outputname, "SOR-%d", index);
			break;
		default:
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "DCB type %d not known\n", type);
			break;
		}

		if (type < OUTPUT_NONE)
			NV50OutputInit(pScrn, i, outputname, bus_count[bus]);
	}
}

void
NV50OutputDestroy(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	nouveauOutputPtr output, next;

	for (output = pNv->output; output != NULL; output = next) {
		next = output->next;
		xfree(output->name);
		xfree(output);
	}

	pNv->output = NULL;
}
