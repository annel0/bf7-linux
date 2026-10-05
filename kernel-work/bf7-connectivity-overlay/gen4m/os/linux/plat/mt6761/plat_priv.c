/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See http://www.gnu.org/licenses/gpl-2.0.html for more details.
*/

/*
 * Платформенный слой gen4m для MT6761.  В донорском дереве Motorola каталога
 * plat/mt6761 нет, поэтому оставались __weak-заглушки из gl_kal.c, и
 * зарезервированная область wifi-reserve-memory (0x300000 @ 0xbe000000)
 * никем не открывалась.  На живом устройстве это выглядело так:
 *
 *   [MPU] violation master is MT6761_M7_AXI_MST_CONNSYS_WIFI_PDMA, from domain 0x2
 *   [STP] <ASSERT> ../patch/sys_patch_mcu.c #2780
 *
 * То есть DMA-мастер Wi-Fi получал отказ EMI MPU, и прошивка чипа падала в
 * ассерт ещё до появления wlan0.  Что заводской модуль делает это сам, видно
 * по строке "emiPhyBase: 0x%x, offset: %u, size: %u" в /opt/bf7/wifi/
 * wlan_drv_gen4m.ko - она принадлежит сильной реализации, а не заглушке.
 *
 * Содержимое повторяет plat/mt6765/plat_priv.c: MT6765 - прямой брат MT6761
 * (та же ConnSys 1.0, тот же EMI MPU v1 с 16 доменами и 32 регионами).
 * Домен 2 - как раз тот, из которого пришло нарушение.
 *
 * kalBoostCpu намеренно не переопределяем: заглушка из gl_kal.c нас устраивает,
 * а её зависимости (cpu_ctrl/topo_ctrl/pm_qos) тянут лишний риск нерешённых
 * символов.
 */

#include "gl_os.h"
#include "precomp.h"

#ifdef CONFIG_MTK_EMI
#include <mt_emi_api.h>

/* Регион 29 - как у MT6765; 31 занят AP_REGION_ID, 25 использует connsys
 * (wmt_drv, platform/mt6761.c REGION_CONN).
 */
#define BF7_WIFI_EMI_MPU_REGION 29

void kalSetEmiMpuProtection(phys_addr_t emiPhyBase, bool enable)
{
	/* Область общей памяти connsys открывает wmt_drv
	 * (consys_emi_mpu_set_region_protection, регион 25), здесь делать
	 * нечего - как и в plat/mt6765.
	 */
}

void kalSetDrvEmiMpuProtection(phys_addr_t emiPhyBase, uint32_t offset,
			       uint32_t size)
{
	struct emi_region_info_t region_info;

	DBGLOG(INIT, INFO, "emiPhyBase: 0x%x, offset: %u, size: %u\n",
			emiPhyBase, offset, size);

	/* set MPU for EMI share Memory */
	region_info.start = emiPhyBase + offset;
	region_info.end = emiPhyBase + offset + size - 1;
	region_info.region = BF7_WIFI_EMI_MPU_REGION;
	SET_ACCESS_PERMISSION(region_info.apc, LOCK, FORBIDDEN, FORBIDDEN,
			      FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN,
			      FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN,
			      FORBIDDEN, FORBIDDEN, FORBIDDEN, NO_PROTECTION,
			      FORBIDDEN, NO_PROTECTION);
	emi_mpu_set_protection(&region_info);
}
#endif
