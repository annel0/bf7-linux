# BF7: драйвер GPS поверх ядра связности MTK (узлы /dev/stpgps, /dev/gps_emi,
# /dev/fw_log_gps) — для mnld из /vendor.
#
# Исходник — донор ubports mt6781 (connectivity/gps/gps_stp), вариант для
# комбо-чипов через STP. Состав как у стокового gps_drv.ko (проверено по его
# символам): stp_chrdev_gps + gps_emi + fw_log_gps, без LNA, без L5
# (второго узла stpgps2) и без MTK_GENERIC_HAL. Заголовки связности — из того
# же донора, из которого собран wmt_drv (как у bf7-bluetooth).
#
# Две правки донора (см. комментарии BF7 в файлах): нет helio-dvfsrc.h, и
# gps_emi не ставит регион MPU 29 — его держит Wi-Fi.
obj-m += gps_drv.o
gps_drv-objs := stp_chrdev_gps.o gps_emi.o fw_log_gps.o

ccflags-y += -I$(BF7_CONN)/common/common_main/include
ccflags-y += -I$(BF7_CONN)/common/common_main/linux/include
ccflags-y += -I$(BF7_CONN)/common/common_main/core/include
ccflags-y += -I$(BF7_CONN)/common/common_main/platform/include
ccflags-y += -I$(BF7_CONN)/common/debug_utility
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/connectivity/common
ccflags-y += -I$(srctree)/drivers/misc/mediatek/emi/$(CONFIG_MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/emi/submodule
ccflags-y += -I$(src)
# см. bf7-bluetooth/Makefile: без него stp_exp.h идёт по ветке #else
ccflags-y += -DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
ccflags-y += -DCONFIG_MTK_WCN_ARM64 -DWMT_IDC_SUPPORT=0
ccflags-y += -DSOC_CO_CLOCK_FLAG=1 -DWMT_CREATE_NODE_DYNAMIC=1 -DREMOVE_MK_NODE=0
ccflags-y += -DCONFIG_MTK_GPS_EMI -DCONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH

ccflags-y += -Wno-error
