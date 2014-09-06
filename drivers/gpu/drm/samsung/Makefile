#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/samsung -Idrivers/gpu/drm/samsung/ump/include
samsungdrm-y := samsung_drm_drv.o samsung_drm_encoder.o samsung_drm_connector.o \
		samsung_drm_crtc.o samsung_drm_fbdev.o samsung_drm_fb.o \
		samsung_drm_buf.o samsung_drm_fimd.o samsung_drm_gem.o

obj-$(CONFIG_DRM_SAMSUNG) += samsungdrm.o
