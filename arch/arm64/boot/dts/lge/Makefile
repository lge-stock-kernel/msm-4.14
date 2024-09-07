ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
DTS_DEVICE := $(subst $\",,$(CONFIG_LGE_TARGET_DEVICE))
DTS_TARGET := $(subst $\",,$(CONFIG_LGE_TARGET_PRODUCT))
ALL_DTS := $(notdir $(wildcard echo $(srctree)/$(src)/$(DTS_DEVICE)/$(DTS_TARGET)-overlay_rev*.dts))
ALL_DTBO := $(patsubst %.dts,%.dtbo,$(ALL_DTS))
dtbo-y += $(ALL_DTBO)

define set_dtbo-base
  $(foreach v,$(1), \
      $(eval $(v)-base := $(2) ))
endef

$(call set_dtbo-base, $(ALL_DTBO),../qcom/sm6150.dtb)

DTB_LIST := $(dtbo-y)

always  := $(dtbo-y)
$(obj)/%.dtbo: $(src)/$(DTS_DEVICE)/%.dts FORCE
	$(call if_changed_dep,dtc)
	$(call if_changed,dtbo_verify)

clean-files := *.dtb
endif
