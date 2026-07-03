/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  esp_eventの最小shim（ASP3用）
 *
 *  Wi-Fi blob／wifi_init.cはesp_event_post()でイベント（SCAN_DONE・
 *  STA_START等）を通知する．本shimはイベントループを持たず，登録
 *  されたハンドラをshimタイマタスク相当の文脈…ではなく**呼出し元
 *  タスクの文脈で直接呼び出す**最小実装とする（デモ用途．ハンドラ内
 *  でのブロッキングは呼出し元＝Wi-Fiタスクを止めるため注意）．
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "esp_shim.h"

#define MAX_HANDLERS	8

typedef struct {
	const char	*event_base;	/* NULLなら未使用スロット */
	int32_t		event_id;		/* -1（ESP_EVENT_ANY_ID）なら全ID */
	void		(*handler)(void *arg, const char *base, int32_t id,
						   void *data);
	void		*arg;
} EVT_HANDLER;

static EVT_HANDLER evt_handlers[MAX_HANDLERS];

/*
 *  ハンドラ登録（esp_event_handler_registerの最小版）
 */
int
esp_event_handler_register(const char *event_base, int32_t event_id,
						   void *event_handler, void *event_handler_arg)
{
	uint_t		i;
	uint32_t	lock = esp_shim_int_disable();

	for (i = 0U; i < MAX_HANDLERS; i++) {
		if (evt_handlers[i].event_base == NULL) {
			evt_handlers[i].event_base = event_base;
			evt_handlers[i].event_id = event_id;
			evt_handlers[i].handler =
				(void (*)(void *, const char *, int32_t, void *))
					event_handler;
			evt_handlers[i].arg = event_handler_arg;
			esp_shim_int_restore(lock);
			return(0);
		}
	}
	esp_shim_int_restore(lock);
	return(-1);
}

/*
 *  イベント通知（osiテーブルの_event_postから呼ばれる）
 */
int
esp_event_post(const char *event_base, int32_t event_id, void *event_data,
			   size_t event_data_size, uint32_t ticks_to_wait)
{
	uint_t	i;

	(void) event_data_size;
	(void) ticks_to_wait;
	syslog(LOG_NOTICE, "esp_event: %s id=%d",
		   event_base != NULL ? event_base : "(null)", (int_t)event_id);

	for (i = 0U; i < MAX_HANDLERS; i++) {
		if (evt_handlers[i].event_base != NULL
			&& (evt_handlers[i].event_base == event_base
				|| strcmp(evt_handlers[i].event_base, event_base) == 0)
			&& (evt_handlers[i].event_id == -1
				|| evt_handlers[i].event_id == event_id)) {
			evt_handlers[i].handler(evt_handlers[i].arg, event_base,
									event_id, event_data);
		}
	}
	return(0);
}
