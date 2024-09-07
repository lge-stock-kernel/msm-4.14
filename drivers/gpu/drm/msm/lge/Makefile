ccflags-y += -Iinclude/drm -I$(src)/../ -I$(src)/../dsi-staging/
ccflags-y += -Idrivers/gpu/drm/msm/sde
ccflags-y += -Idrivers/gpu/drm/msm/dp

lge_display_common-objs := lge_dsi_panel.o
lge_display_common-objs += lge_panel_reg.o
lge_display_common-objs += lge_ddic_ops.o
lge_display_common-objs += lge_ddic_ops_sw49106.o
lge_display_common-objs += lge_ddic_ops_sw49109.o
lge_display_common-objs += cm/lge_color_manager.o
lge_display_common-objs += brightness/lge_brightness.o
lge_display_common-objs += notifier/lge_panel_notifier.o

obj-$(CONFIG_LGE_DISPLAY_NOT_SUPPORT_DISPLAYPORT) += notifier/lge_dp_notifier.o
obj-$(CONFIG_LGE_DISPLAY_COMMON) += lge_display_common.o
