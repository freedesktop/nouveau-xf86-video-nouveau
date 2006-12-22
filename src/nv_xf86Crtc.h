/*
 * Copyright © 2006 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#ifndef _XF86CRTC_H_
#define _XF86CRTC_H_

#include <edid.h>
#include "randrstr.h"
#include "nv_xf86Modes.h"

typedef struct _xf86Crtc xf86CrtcRec, *xf86CrtcPtr;
typedef struct _xf86Output xf86OutputRec, *xf86OutputPtr;

typedef enum _xf86OutputStatus {
   XF86OutputStatusConnected,
   XF86OutputStatusDisconnected,
   XF86OutputStatusUnknown,
} xf86OutputStatus;

typedef struct _xf86CrtcFuncs {
   /**
    * Turns the crtc on/off, or sets intermediate power levels if available.
    *
    * Unsupported intermediate modes drop to the lower power setting.  If the
    * mode is DPMSModeOff, the crtc must be disabled sufficiently for it to
    * be safe to call mode_set.
    */
   void
    (*dpms)(xf86CrtcPtr		crtc,
	    int		    	mode);

   /**
    * Saves the crtc's state for restoration on VT switch.
    */
   void
    (*save)(xf86CrtcPtr		crtc);

   /**
    * Restore's the crtc's state at VT switch.
    */
   void
    (*restore)(xf86CrtcPtr	crtc);


    /**
     * Callback to adjust the mode to be set in the CRTC.
     *
     * This allows a CRTC to adjust the clock or even the entire set of
     * timings, which is used for panels with fixed timings or for
     * buses with clock limitations.
     */
    Bool
    (*mode_fixup)(xf86CrtcPtr crtc,
		  DisplayModePtr mode,
		  DisplayModePtr adjusted_mode);

    /**
     * Callback for setting up a video mode after fixups have been made.
     */
    void
    (*mode_set)(xf86CrtcPtr crtc,
		DisplayModePtr mode,
		DisplayModePtr adjusted_mode);

    /**
     * Clean up driver-specific bits of the crtc
     */
    void
    (*destroy) (xf86CrtcPtr	crtc);
} xf86CrtcFuncsRec, *xf86CrtcFuncsPtr;

struct _xf86Crtc {
    /**
     * Associated ScrnInfo
     */
    ScrnInfoPtr	    scrn;
    
    /**
     * Active state of this CRTC
     *
     * Set when this CRTC is driving one or more outputs 
     */
    Bool	    enabled;
    
    /**
     * Position on screen
     *
     * Locates this CRTC within the frame buffer
     */
    int		    x, y;
    
    /** Track whether cursor is within CRTC range  */
    Bool	    cursorInRange;
    
    /** Track state of cursor associated with this CRTC */
    Bool	    cursorShown;
    
    /**
     * Active mode
     *
     * This reflects the mode as set in the CRTC currently
     * It will be cleared when the VT is not active or
     * during server startup
     */
    DisplayModeRec  curMode;
    
    /**
     * Desired mode
     *
     * This is set to the requested mode, independent of
     * whether the VT is active. In particular, it receives
     * the startup configured mode and saves the active mode
     * on VT switch.
     */
    DisplayModeRec  desiredMode;
    
    /** crtc-specific functions */
    const xf86CrtcFuncsRec *funcs;

    /**
     * Driver private
     *
     * Holds driver-private information
     */
    void	    *driver_private;

#ifdef RANDR_12_INTERFACE
    /**
     * RandR crtc
     *
     * When RandR 1.2 is available, this
     * points at the associated crtc object
     */
    RRCrtcPtr	    randr_crtc;
#else
    void	    *randr_crtc;
#endif
};

typedef struct _xf86OutputFuncs {
    /**
     * Turns the output on/off, or sets intermediate power levels if available.
     *
     * Unsupported intermediate modes drop to the lower power setting.  If the
     * mode is DPMSModeOff, the output must be disabled, as the DPLL may be
     * disabled afterwards.
     */
    void
    (*dpms)(xf86OutputPtr	output,
	    int			mode);

    /**
     * Saves the output's state for restoration on VT switch.
     */
    void
    (*save)(xf86OutputPtr	output);

    /**
     * Restore's the output's state at VT switch.
     */
    void
    (*restore)(xf86OutputPtr	output);

    /**
     * Callback for testing a video mode for a given output.
     *
     * This function should only check for cases where a mode can't be supported
     * on the output specifically, and not represent generic CRTC limitations.
     *
     * \return MODE_OK if the mode is valid, or another MODE_* otherwise.
     */
    int
    (*mode_valid)(xf86OutputPtr	    output,
		  DisplayModePtr    pMode);

    /**
     * Callback to adjust the mode to be set in the CRTC.
     *
     * This allows an output to adjust the clock or even the entire set of
     * timings, which is used for panels with fixed timings or for
     * buses with clock limitations.
     */
    Bool
    (*mode_fixup)(xf86OutputPtr output,
		  DisplayModePtr mode,
		  DisplayModePtr adjusted_mode);

    /**
     * Callback for setting up a video mode after fixups have been made.
     *
     * This is only called while the output is disabled.  The dpms callback
     * must be all that's necessary for the output, to turn the output on
     * after this function is called.
     */
    void
    (*mode_set)(xf86OutputPtr  output,
		DisplayModePtr mode,
		DisplayModePtr adjusted_mode);

    /**
     * Probe for a connected output, and return detect_status.
     */
    xf86OutputStatus
    (*detect)(xf86OutputPtr	    output);

    /**
     * Query the device for the modes it provides.
     *
     * This function may also update MonInfo, mm_width, and mm_height.
     *
     * \return singly-linked list of modes or NULL if no modes found.
     */
    DisplayModePtr
    (*get_modes)(xf86OutputPtr	    output);

    /**
     * Clean up driver-specific bits of the output
     */
    void
    (*destroy) (xf86OutputPtr	    output);
} xf86OutputFuncsRec, *xf86OutputFuncsPtr;

struct _xf86Output {
    /**
     * Associated ScrnInfo
     */
    ScrnInfoPtr		scrn;

    /**
     * Currently connected crtc (if any)
     *
     * If this output is not in use, this field will be NULL.
     */
    xf86CrtcPtr		crtc;

    /**
     * Possible CRTCs for this output as a mask of crtc indices
     */
    CARD32		possible_crtcs;

    /**
     * Possible outputs to share the same CRTC as a mask of output indices
     */
    CARD32		possible_clones;
    /**
     * List of available modes on this output.
     *
     * This should be the list from get_modes(), plus perhaps additional
     * compatible modes added later.
     */
    DisplayModePtr	probed_modes;

    /**
     * Current connection status
     *
     * This indicates whether a monitor is known to be connected
     * to this output or not, or whether there is no way to tell
     */
    xf86OutputStatus	status;

    /** EDID monitor information */
    xf86MonPtr		MonInfo;

    /** subpixel order */
    int			subpixel_order;

    /** Physical size of the currently attached output device. */
    int			mm_width, mm_height;

    /** Output name */
    char		*name;

    /** output-specific functions */
    const xf86OutputFuncsRec *funcs;

    /** driver private information */
    void		*driver_private;
    
#ifdef RANDR_12_INTERFACE
    /**
     * RandR 1.2 output structure.
     *
     * When RandR 1.2 is available, this points at the associated
     * RandR output structure and is created when this output is created
     */
    RROutputPtr		randr_output;
#else
    void		*randr_output;
#endif
};

typedef struct _xf86CrtcConfig {
    int			num_output;
    xf86OutputPtr	*output;
    /**
     * compat_output is used whenever we deal
     * with legacy code that only understands a single
     * output. pScrn->modes will be loaded from this output,
     * adjust frame will whack this output, etc.
     */
    int			compat_output;

    int			num_crtc;
    xf86CrtcPtr		*crtc;

    int			minWidth, minHeight;
    int			maxWidth, maxHeight;
} xf86CrtcConfigRec, *xf86CrtcConfigPtr;

extern int xf86CrtcConfigPrivateIndex;

#define XF86_CRTC_CONFIG_PTR(p)	((xf86CrtcConfigPtr) ((p)->privates[xf86CrtcConfigPrivateIndex].ptr))

/*
 * Initialize xf86CrtcConfig structure
 */

void
xf86CrtcConfigInit (ScrnInfoPtr		scrn);

void
xf86CrtcSetSizeRange (ScrnInfoPtr scrn,
		      int minWidth, int minHeight,
		      int maxWidth, int maxHeight);

/*
 * Crtc functions
 */
xf86CrtcPtr
xf86CrtcCreate (ScrnInfoPtr		scrn,
		const xf86CrtcFuncsRec	*funcs);

void
xf86CrtcDestroy (xf86CrtcPtr		crtc);


/**
 * Allocate a crtc for the specified output
 *
 * Find a currently unused CRTC which is suitable for
 * the specified output
 */

xf86CrtcPtr 
xf86AllocCrtc (xf86OutputPtr		output);

/**
 * Free a crtc
 *
 * Mark the crtc as unused by any outputs
 */

void
xf86FreeCrtc (xf86CrtcPtr		crtc);

/*
 * Output functions
 */
xf86OutputPtr
xf86OutputCreate (ScrnInfoPtr		scrn,
		      const xf86OutputFuncsRec *funcs,
		      const char	*name);

void
xf86OutputRename (xf86OutputPtr output, const char *name);

void
xf86OutputDestroy (xf86OutputPtr	output);

void
xf86ProbeOutputModes (ScrnInfoPtr pScrn);

void
xf86SetScrnInfoModes (ScrnInfoPtr pScrn);

Bool
xf86InitialConfiguration (ScrnInfoPtr pScrn);

void
xf86DPMSSet(ScrnInfoPtr pScrn, int PowerManagementMode, int flags);

#endif /* _XF86CRTC_H_ */
