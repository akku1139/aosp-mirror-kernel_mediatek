#include $(srctree)/drivers/misc/mediatek/Makefile.custom
flags-y  += -I$(srctree)/drivers/misc/mediatek/thermal/fakeHeader/
obj-  := dummy.o

ifeq ($(CONFIG_THERMAL), y)
else
    obj-y += mtk_thermal_dummy.o
endif

obj-$(CONFIG_THERMAL) += mtk_thermal_platform.o

# Only included in non-EVB projects
obj-$(CONFIG_THERMAL) += mtk_ts_cpu.o
obj-$(CONFIG_THERMAL) += mtk_ts_abb.o
#obj-$(CONFIG_THERMAL) += mtk_ts_all_ts.o
#obj-$(CONFIG_THERMAL) += mtk_ts_battery.o
#obj-$(CONFIG_THERMAL) += mtk_ts_pmic.o
#obj-$(CONFIG_THERMAL) += mtk_ts_bts.o
#obj-$(CONFIG_THERMAL) += tmp103_thermal.o
#obj-$(CONFIG_THERMAL) += mtk_ts_bts_tmp103.o
#obj-$(CONFIG_THERMAL) += tm_nth.o
