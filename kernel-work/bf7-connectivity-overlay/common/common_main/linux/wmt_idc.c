/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include "osal.h"
#include "wmt_idc.h"
#include "wmt_lib.h"

#if CFG_WMT_LTE_COEX_HANDLING

#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

MTK_WCN_WMT_IDC_INFO gWmtIdcInfo;

/*
 * BF7: keep the LTE coex handshake from waking the system right after suspend.
 *
 * When Wi-Fi goes to sleep (cfg80211 suspend), the connsys firmware sends a
 * coex update to the modem (EL1), and EL1 answers 0.1-0.9 s later. If the
 * answer comes after suspend_device_irqs(), CCIF_AP_DATA is a wakeup IRQ and
 * the phone is awake again within a second: with Wi-Fi on, 9 of 10 sleep
 * attempts ended that way. When the answer came in time, the modem stayed
 * quiet for the whole sleep, so the update itself is wanted - only its answer
 * has to be received before the IRQs go off.
 *
 * So: every message to EL1 marks an answer as pending; the consys
 * .suspend_late waits for it (up to 2 s; answers were seen as late as
 * 0.99 s). While the system is suspending, EL1 messages are not forwarded
 * to connsys at once - BTIF is already suspended then, and sending would
 * bring it back up in the middle of suspend. The latest one is kept and
 * forwarded after resume.
 */
static DECLARE_COMPLETION(bf7_idc_answer);
static atomic_t bf7_idc_pending = ATOMIC_INIT(0);
static atomic_t bf7_idc_suspending = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(bf7_idc_held_lock);
static conn_md_ipc_ilm_t bf7_idc_held_ilm;
static struct local_para *bf7_idc_held_lps;

static VOID bf7_idc_forward_held(struct work_struct *work)
{
	conn_md_ipc_ilm_t ilm;
	struct local_para *lps;
	ULONG flags;

	spin_lock_irqsave(&bf7_idc_held_lock, flags);
	lps = bf7_idc_held_lps;
	ilm = bf7_idc_held_ilm;
	bf7_idc_held_lps = NULL;
	spin_unlock_irqrestore(&bf7_idc_held_lock, flags);

	if (lps == NULL)
		return;
	if (mtk_wcn_stp_is_ready()) {
		if (wmt_lib_handle_idc_msg(&ilm) == MTK_WCN_BOOL_FALSE)
			pr_warn("[WMT-IDC] BF7: held LTE msg 0x%x not handled\n", ilm.msg_id);
		else
			pr_info("[WMT-IDC] BF7: held LTE msg 0x%x forwarded after resume\n", ilm.msg_id);
	}
	kfree(lps);
}

static DECLARE_WORK(bf7_idc_forward_work, bf7_idc_forward_held);

static VOID bf7_idc_hold(conn_md_ipc_ilm_t *ilm)
{
	struct local_para *lps = NULL, *old;
	UINT16 len = ilm->local_para_ptr->msg_len;
	ULONG flags;

	if (len < osal_sizeof(struct local_para) || len > LTE_IDC_BUFFER_MAX_SIZE) {
		WMT_ERR_FUNC("BF7: LTE msg len %d, not held\n", len);
		return;
	}
	lps = kmemdup(ilm->local_para_ptr, len, GFP_KERNEL);
	if (lps == NULL)
		return;

	spin_lock_irqsave(&bf7_idc_held_lock, flags);
	old = bf7_idc_held_lps;
	bf7_idc_held_ilm = *ilm;
	bf7_idc_held_ilm.local_para_ptr = lps;
	bf7_idc_held_lps = lps;
	spin_unlock_irqrestore(&bf7_idc_held_lock, flags);
	kfree(old);
}

static VOID bf7_idc_send_to_lte(VOID)
{
	reinit_completion(&bf7_idc_answer);
	atomic_set(&bf7_idc_pending, 1);
	mtk_conn_md_bridge_send_msg(&gWmtIdcInfo.iit);
}

/* consys .suspend: from here on EL1 messages are held, not forwarded */
VOID wmt_idc_bf7_suspend(VOID)
{
	atomic_set(&bf7_idc_suspending, 1);
}

/* consys .suspend_late: the answer to the last update must be in before noirq */
VOID wmt_idc_bf7_suspend_late(VOID)
{
	if (!atomic_read(&bf7_idc_pending))
		return;
	if (wait_for_completion_timeout(&bf7_idc_answer, msecs_to_jiffies(2000)))
		pr_info("[WMT-IDC] BF7: LTE coex answer received before suspend\n");
	else
		pr_warn("[WMT-IDC] BF7: no LTE coex answer in 2 s, suspending anyway\n");
}

/* consys .resume: BTIF is up again, forward what was held */
VOID wmt_idc_bf7_resume(VOID)
{
	atomic_set(&bf7_idc_suspending, 0);
	schedule_work(&bf7_idc_forward_work);
}

INT32 wmt_idc_init(VOID)
{
	INT32 iRet;

	osal_memset(&gWmtIdcInfo, 0, osal_sizeof(gWmtIdcInfo));
	gWmtIdcInfo.iit.src_mod_id = AP_MOD_WMT;
	gWmtIdcInfo.iit.dest_mod_id = MD_MOD_EL1;
	gWmtIdcInfo.iit.sap_id = 0;
	gWmtIdcInfo.ops.rx_cb = wmt_idc_msg_from_lte_handing;

	iRet = mtk_conn_md_bridge_reg(gWmtIdcInfo.iit.src_mod_id, &gWmtIdcInfo.ops);
	if (iRet) {
		WMT_ERR_FUNC("mtk_conn_md_bridge_reg fail(%d)\n", iRet);
		return -1;
	}
	/* mtk_wcn_stp_flush_rx_queue(COEX_TASK_INDX); */
	return 0;
}

INT32 wmt_idc_deinit(VOID)
{
	INT32 iRet;

	iRet = mtk_conn_md_bridge_unreg(gWmtIdcInfo.iit.src_mod_id);
	if (iRet)
		WMT_ERR_FUNC("mtk_conn_md_bridge_unreg fail(%d)\n", iRet);

	osal_memset(&gWmtIdcInfo, 0, osal_sizeof(gWmtIdcInfo));

	return 0;
}

INT32 wmt_idc_msg_from_lte_handing(conn_md_ipc_ilm_t *ilm)
{
	MTK_WCN_BOOL bRet;

	if (ilm == NULL) {
		WMT_ERR_FUNC("NULL pointer\n");
		return -1;
	}
	if (atomic_xchg(&bf7_idc_pending, 0))
		complete(&bf7_idc_answer);
	if (atomic_read(&bf7_idc_suspending) && ilm->local_para_ptr != NULL) {
		bf7_idc_hold(ilm);
		return 0;
	}
	if (mtk_wcn_stp_is_ready()) {
		bRet = wmt_lib_handle_idc_msg(ilm);
		if (bRet == MTK_WCN_BOOL_FALSE) {
			WMT_ERR_FUNC("wmt handing idc msg fail\n");
			return -2;
		}
	} else
		WMT_INFO_FUNC("Received LTE msg,but STP is not ready,drop it!\n");

	return 0;
}

VOID wmt_idc_dump_debug_msg(PUINT8 str, PUINT8 p_buf, UINT32 buf_len)
{
	UINT32 idx = 0;

	WMT_DBG_FUNC("%s:, length:%d\n", str, buf_len);

	WMT_DBG_FUNC("ASCII output:\n");

	for (idx = 0; idx < buf_len;) {
		WMT_DBG_FUNC("%c", p_buf[idx]);
		idx++;
		if (idx % 16 == 0)
			WMT_DBG_FUNC("\n");
	}

	WMT_DBG_FUNC("HEX output:\n");

	for (idx = 0; idx < buf_len;) {
		WMT_DBG_FUNC("%02x ", p_buf[idx]);
		idx++;
		if (idx % 16 == 0)
			WMT_DBG_FUNC("\n");
	}
}

INT32 wmt_idc_msg_to_lte_handing(VOID)
{
	UINT32 readlen = 0;
	struct local_para *p_lps = NULL;
	PUINT8 p_data = NULL;
	UINT8 opcode = 0;
	UINT16 msg_len = 0;
#if	CFG_WMT_LTE_ENABLE_MSGID_MAPPING
	MTK_WCN_BOOL unknown_msgid = MTK_WCN_BOOL_FALSE;
#endif

	readlen = mtk_wcn_stp_receive_data(&gWmtIdcInfo.buffer[0], 4, COEX_TASK_INDX);
	if (readlen == 0) {
		osal_sleep_ms(5);
		readlen = mtk_wcn_stp_receive_data(&gWmtIdcInfo.buffer[0], 4, COEX_TASK_INDX);
	}

	if (readlen > 0) {
		WMT_DBG_FUNC("read data len from fw(%d)\n", readlen);
		wmt_idc_dump_debug_msg("WMT->LTE from STP buffer", &gWmtIdcInfo.buffer[0], readlen);

		while (readlen) {
			p_data = &gWmtIdcInfo.buffer[0];
			p_data += 2;	/*omit direction & opcode 2 bytes */
			osal_memcpy(&msg_len, p_data, 2);
			WMT_DBG_FUNC("current raw data len(%d) from connsys firmware\n", msg_len);

			msg_len = msg_len > LTE_IDC_BUFFER_MAX_SIZE ? LTE_IDC_BUFFER_MAX_SIZE : msg_len;
			osal_memset(&gWmtIdcInfo.buffer[0], 0, LTE_IDC_BUFFER_MAX_SIZE);
			readlen = mtk_wcn_stp_receive_data(&gWmtIdcInfo.buffer[0], msg_len, COEX_TASK_INDX);
			p_data = &gWmtIdcInfo.buffer[0];

			if (msg_len > 0)
				msg_len -= 1;	/*flag byte */
			else
				WMT_ERR_FUNC("msg_len is ERROR!");

			/*how to handle flag(msg type) need to Scott comment */
			/************************************************/

			if (*p_data == WMT_IDC_RX_OPCODE_DEBUG_MONITOR)
				/*do not need transfer to LTE */
			{
				p_data += 1;	/*flag : 1 byte */
				/*need to handle these debug message */
				wmt_idc_dump_debug_msg("WIFI DEBUG MONITOR", p_data, msg_len);
			} else
				/*need to transfer to LTE */
			{
				p_lps =
				    (struct local_para *) osal_malloc(osal_sizeof(struct local_para) +
								      osal_sizeof(UINT8) * msg_len);
				if (p_lps == NULL) {
					WMT_ERR_FUNC("allocate struct local_para memory fail\n");
					return -1;
				}

				p_lps->msg_len = msg_len + osal_sizeof(struct local_para);

				opcode = *p_data;
				WMT_DBG_FUNC("current opcode(%d) to LTE\n", opcode);

				p_data += 1;	/*flag : 1 byte */
				osal_memcpy(p_lps->data, p_data, msg_len);

				gWmtIdcInfo.iit.local_para_ptr = p_lps;

#if	CFG_WMT_LTE_ENABLE_MSGID_MAPPING
				switch (opcode) {
				case WMT_IDC_RX_OPCODE_BTWF_DEF_PARA:
					gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_OPER_DEFAULT_PARAM_IND;
					break;
				case WMT_IDC_RX_OPCODE_BTWF_CHAN_RAN:
					gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_OPER_FREQ_IND;
					break;
				case WMT_IDC_RX_OPCODE_LTE_FREQ_IDX_TABLE:
					gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_FREQ_IDX_TABLE_IND;
					break;
				case WMT_IDC_RX_OPCODE_BTWF_PROFILE_IND:
					gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_PROFILE_IND;
					break;
				case WMT_IDC_RX_OPCODE_UART_PIN_SEL:
					gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_PIN_TYPE_IND;
					break;
					/* case WMT_IDC_RX_OPCODE_TDM_REQ: */
					/* gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_OPER_FREQ_IND; */
					/* break; */
				default:
					unknown_msgid = MTK_WCN_BOOL_TRUE;
					WMT_ERR_FUNC("unknown opcode(%d) from connsys firmware\n", opcode);
					break;
				}
				if (unknown_msgid == MTK_WCN_BOOL_FALSE) {
					/*handling flag value in wmt cmd */
					bf7_idc_send_to_lte();
				}
#else
				if (opcode >= LTE_MSG_ID_OFFSET) {
					gWmtIdcInfo.iit.msg_id =
						opcode + IPC_EL1_MSG_ID_BEGIN - LTE_MSG_ID_OFFSET + 1;
					/*handling flag value in wmt cmd */
					if (gWmtIdcInfo.iit.msg_id == IPC_MSG_ID_MD_CONSYS_VERIFICATION_REQ)
						gWmtIdcInfo.iit.dest_mod_id = MD_MOD_GMMGR;
					else
						gWmtIdcInfo.iit.dest_mod_id = MD_MOD_EL1;

					bf7_idc_send_to_lte();
					WMT_DBG_FUNC("CONN->LTE: (0x%x->0x%x)\n", opcode,
							gWmtIdcInfo.iit.msg_id);
				} else
					WMT_ERR_FUNC("opcode(%d)from connsys fw is out of range,drop it!\n",
							opcode);
#endif
				osal_free(p_lps);
			}

			osal_memset(&gWmtIdcInfo.buffer[0], 0, LTE_IDC_BUFFER_MAX_SIZE);
			readlen = mtk_wcn_stp_receive_data(&gWmtIdcInfo.buffer[0], 4, COEX_TASK_INDX);
			WMT_DBG_FUNC("read data len from fw(%d)\n", readlen);
		}

	} else
		WMT_ERR_FUNC("there is no coex data in stp buffer\n");

	osal_memset(&gWmtIdcInfo.buffer[0], 0, LTE_IDC_BUFFER_MAX_SIZE);

	return 0;
}

UINT32 wmt_idc_msg_to_lte_handing_for_test(PUINT8 p_buf, UINT32 len)
{
	UINT32 readlen = len;
	struct local_para *p_lps = NULL;
	PUINT8 p_data = NULL;
	UINT8 opcode = 0;
	UINT16 msg_len = 0;
	MTK_WCN_BOOL unknown_msgid = MTK_WCN_BOOL_FALSE;

	osal_memcpy(&gWmtIdcInfo.buffer[0], p_buf, len);

	if (readlen > 0) {
		WMT_DBG_FUNC("read data len from fw(%d)\n", readlen);

		while (readlen) {
			p_data = &gWmtIdcInfo.buffer[0];
			p_data += 2;	/*omit direction & opcode 2 bytes */
			osal_memcpy(&msg_len, p_data, 2);

			msg_len = msg_len > LTE_IDC_BUFFER_MAX_SIZE ? LTE_IDC_BUFFER_MAX_SIZE : msg_len;
			osal_memset(&gWmtIdcInfo.buffer[0], 0, LTE_IDC_BUFFER_MAX_SIZE);
			readlen = mtk_wcn_stp_receive_data(&gWmtIdcInfo.buffer[0], msg_len, COEX_TASK_INDX);
			p_data = &gWmtIdcInfo.buffer[0];

			if (msg_len > 0)
				msg_len -= 1;	/*flag byte */
			else
				WMT_ERR_FUNC("msg_len is ERROR!");
			WMT_DBG_FUNC("current raw data len(%d) from connsys firmware\n", msg_len);

			/*how to handle flag(msg type) need to Scott comment */
			/************************************************/

			if (*p_data == WMT_IDC_RX_OPCODE_DEBUG_MONITOR)
				/*do not need transfer to LTE */
			{
				p_data += 1;	/*flag : 1 byte */
				/*need to handle these debug message */
				wmt_idc_dump_debug_msg("WIFI DEBUG MONITOR", p_data, msg_len);
			} else {
				/*need to transfer to LTE */
				p_lps = (struct local_para *) osal_malloc(osal_sizeof(struct local_para) +
						osal_sizeof(UINT8) * msg_len);
				if (p_lps == NULL) {
					WMT_ERR_FUNC("allocate struct local_para memory fail\n");
					return -1;
				}

				p_lps->msg_len = msg_len + osal_sizeof(struct local_para);

				opcode = *p_data;
				WMT_DBG_FUNC("current opcode(%d) to LTE\n", opcode);

				p_data += 1;	/*flag : 1 byte */
				osal_memcpy(p_lps->data, p_data, msg_len);

				gWmtIdcInfo.iit.local_para_ptr = p_lps;

				switch (opcode) {
				case WMT_IDC_RX_OPCODE_BTWF_DEF_PARA:
					gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_OPER_DEFAULT_PARAM_IND;
					break;
				case WMT_IDC_RX_OPCODE_BTWF_CHAN_RAN:
					gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_OPER_FREQ_IND;
					break;
					/* case WMT_IDC_RX_OPCODE_TDM_REQ: */
					/* gWmtIdcInfo.iit.msg_id = IPC_MSG_ID_EL1_WIFIBT_OPER_FREQ_IND; */
					/* break; */
				default:
					unknown_msgid = MTK_WCN_BOOL_TRUE;
					WMT_ERR_FUNC("unknown opcode(%d) from connsys firmware\n", opcode);
				}
				if (unknown_msgid == MTK_WCN_BOOL_FALSE) {
					/*handling flag value in wmt cmd */
					bf7_idc_send_to_lte();
				}
				osal_free(p_lps);
			}

			osal_memset(&gWmtIdcInfo.buffer[0], 0, LTE_IDC_BUFFER_MAX_SIZE);
			readlen = mtk_wcn_stp_receive_data(&gWmtIdcInfo.buffer[0], 4, COEX_TASK_INDX);
		}

	} else
		WMT_ERR_FUNC("there is no coex data in stp buffer\n");

	osal_memset(&gWmtIdcInfo.buffer[0], 0, LTE_IDC_BUFFER_MAX_SIZE);

	return 0;
}
#endif /* CFG_WMT_LTE_COEX_HANDLING */
