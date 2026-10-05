# BF7: драйвер Bluetooth поверх ядра связности MTK.
#
# Исходник взят из донора (connectivity/bt/mt66xx/legacy) — это вариант для
# старых комбо-чипов, работающих через STP, а не BTIF. Он определяет
# mtk_wcn_stpbt_drv_init, который наш wmt_drv объявляет слабым символом и зовёт
# при включении Bluetooth, и создаёт узел /dev/stpbt.
#
# Заголовки связности берутся из того же донора, из которого собран wmt_drv.
obj-m += bt_drv.o
bt_drv-objs := stp_chrdev_bt.o dbg_bt.o fw_log_bt.o

ccflags-y += -I$(BF7_CONN)/common/common_main/include
ccflags-y += -I$(BF7_CONN)/common/common_main/linux/include
ccflags-y += -I$(BF7_CONN)/common/common_main/core/include
ccflags-y += -I$(BF7_CONN)/common/debug_utility
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
ccflags-y += -I$(src)
# Без этого макроса заголовок stp_exp.h идёт по ветке #else, где пользуется
# типами, объявленными только в ветке #ifdef, — и сыплет два десятка «unknown
# type name». Раньше я решил, что в нашем дереве нет ENUM_WMTMSG_TYPE_T; это
# было неверно, тип есть, просто спрятан условной сборкой. Драйвер связности
# определяет тот же макрос у себя (src/common/Kbuild и src/adaptor/Makefile).
ccflags-y += -DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT

ccflags-y += -Wno-error
