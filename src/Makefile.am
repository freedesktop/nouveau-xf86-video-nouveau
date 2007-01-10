#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# this is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.
AM_CFLAGS = @XORG_CFLAGS@
nouveau_drv_la_LTLIBRARIES = nouveau_drv.la
nouveau_drv_la_LDFLAGS = -module -avoid-version
nouveau_drv_ladir = @moduledir@/drivers

nouveau_drv_la_SOURCES = \
         nv_bios.c \
         nv_const.h \
         nv_cursor.c \
         nv_dma.c \
         nv_dma.h \
         nv_dri.c \
         nv_dri.h \
         nv_exa.c \
         nv_driver.c \
         nv_hw.c \
         nv_include.h \
         nv_local.h \
         nv_proto.h \
         nvreg.h \
         nv_setup.c \
         nv_shadow.c \
         nv_type.h \
         nv_video.c \
         nv_xaa.c \
	 nv30_exa.c \
	 nv30_fpinst.h \
	 nv40_vpinst.h \
	 nv_xf86Modes.c \
	 nv_xf86Modes.h \
	 nv_xf86Crtc.c \
	 nv_xf86Crtc.h \
	 nv_randr.h \
	 nv_randr.c \
	 nv_output.c \
	 nv_crtc.c \
	 nv_i2c.c \
	 nv_edid_modes.c \
	 nv_xf86cvt.c

#riva128_la_LTLIBRARIES = riva128.la
#riva128_la_LDFLAGS = -module -avoid-version
#riva128_ladir = @moduledir@/drivers

#riva128_la_SOURCES = \
#         riva_const.h \
#         riva_cursor.c \
#         riva_dac.c \
#         riva_dga.c \
#         riva_driver.c \
#         riva_hw.c \
#         riva_hw.h \
#         riva_include.h \
#         riva_local.h \
#         riva_proto.h \
#         riva_setup.c \
#         riva_shadow.c \
#         riva_tbl.h \
#         riva_type.h \
#         riva_xaa.c
