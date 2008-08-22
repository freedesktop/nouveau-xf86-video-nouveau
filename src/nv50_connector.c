/*
 * Copyright 2008 Maarten Maathuis
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

#include "nouveau_modeset.h"
#include "nouveau_crtc.h"
#include "nouveau_output.h"
#include "nouveau_connector.h"

static xf86MonPtr
NV50ConnectorGetEDID(nouveauConnectorPtr connector)
{
	ScrnInfoPtr pScrn = connector->scrn;

	return xf86DoEDID_DDC2(pScrn->scrnIndex, connector->pDDCBus);
}

static xf86MonPtr
NV50ConnectorDDCDetect(nouveauConnectorPtr connector)
{
	ScrnInfoPtr pScrn = connector->scrn;
	xf86MonPtr ddc_mon;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50ConnectorDDCDetect is called.\n");

	if (!connector->pDDCBus)
		return FALSE;

	ddc_mon = NV50ConnectorGetEDID(connector);

	return ddc_mon;
}

static DisplayModePtr
NV50ConnectorGetDDCModes(nouveauConnectorPtr connector)
{
	ScrnInfoPtr pScrn = connector->scrn;
	xf86MonPtr ddc_mon;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50ConnectorGetDDCModes is called.\n");

	ddc_mon = NV50ConnectorGetEDID(connector);

	if (!ddc_mon)
		return NULL;

	return xf86DDCGetModes(pScrn->scrnIndex, ddc_mon);
}

void
NV50ConnectorInit(ScrnInfoPtr pScrn)
{
	int i;
	NVPtr pNv = NVPTR(pScrn);

	/* Maybe a bit overdone, because often only 3 or 4 connectors are present. */
	for (i = 0; i < MAX_NUM_DCB_ENTRIES; i++) {
		nouveauConnectorPtr connector = xnfcalloc(sizeof(nouveauConnectorRec), 1);
		connector->scrn = pScrn;
		connector->index = i;

		char connector_name[20];
		sprintf(connector_name, "Connector-%d", i);
		connector->name = xstrdup(connector_name);

		/* Function pointers. */
		connector->DDCDetect = NV50ConnectorDDCDetect;
		connector->GetDDCModes = NV50ConnectorGetDDCModes;
		connector->HotplugDetect = NULL;

		pNv->connector[i] = connector;
	}
}

void
NV50ConnectorDestroy(ScrnInfoPtr pScrn)
{
	int i;
	NVPtr pNv = NVPTR(pScrn);

	/* Maybe a bit overdone, because often only 3 or 4 connectors are present. */
	for (i = 0; i < MAX_NUM_DCB_ENTRIES; i++) {
		nouveauConnectorPtr connector = pNv->connector[i];

		xfree(connector->name);
		xfree(connector);
		pNv->connector[i] = NULL;
	}
}

