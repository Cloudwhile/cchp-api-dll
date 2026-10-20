#ifndef CCHP_H
#define CCHP_H

/* 所有字符串均为 UTF-8、NUL 结尾，输入仅在调用期间借用。
 * 配置与所有 JSON 对象均允许未知字段。
 * 本 DLL 使用静态 CRT。返回的 char* 必须由 cchp_string_free 释放，
 * 禁止在宿主中使用 free/delete。句柄必须由 cchp_client_destroy 释放。
 */
#include <stdint.h>
#if defined(_WIN32)
#define CCHP_CALL __stdcall
#if defined(CCHP_BUILD)
#define CCHP_API
#else
#define CCHP_API __declspec(dllimport)
#endif
#else
#define CCHP_CALL
#define CCHP_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef void* cchp_client;
CCHP_API cchp_client CCHP_CALL cchp_client_create(const char* config_json);
CCHP_API void CCHP_CALL cchp_client_destroy(cchp_client client);
/* query_json: 对象，标量参数将自动 URL 编码；NULL 表示无参数。
 * body_json: 任意有效 JSON，NULL 表示无请求体。失败返回 NULL。
 * path 必须为 /api/ 下的相对路径，不能带查询串。GET 最多重试 3 次。
 * spec-only: /api/account/orgs 及其组织管理子路由，实站可能返回 404。 */
CCHP_API char* CCHP_CALL cchp_request(cchp_client client, const char* method,
    const char* path, const char* query_json, const char* body_json);
CCHP_API void CCHP_CALL cchp_string_free(char* string);
/* query_json 为 UTF-8 JSON 对象；NULL 表示默认参数。
 * quota_effective 必须提供 scopeType=user|org|key 与 scopeId。
 * logs 的原始 page 包裹与未知字段保持不变，limit 为 1..200。
 */
CCHP_API char* CCHP_CALL cchp_get_me(cchp_client client);
CCHP_API char* CCHP_CALL cchp_usage_logs(cchp_client client, const char* query_json);
CCHP_API char* CCHP_CALL cchp_usage_summary(cchp_client client);
CCHP_API char* CCHP_CALL cchp_usage_by_model(cchp_client client, const char* query_json);
CCHP_API char* CCHP_CALL cchp_usage_timeseries(cchp_client client, const char* query_json);
CCHP_API char* CCHP_CALL cchp_wallet_overview(cchp_client client);
CCHP_API char* CCHP_CALL cchp_quota_effective(cchp_client client, const char* query_json);
CCHP_API char* CCHP_CALL cchp_status_snapshot(cchp_client client);
/* stream: "realtime" 或 "metrics"；types: 逗号分隔的类型，NULL 为默认。
 * 返回订阅 ID，失败为 0。事件 JSON: {event,id,data}；sdk_error 的 data
 * 含 status/code/message/terminal。工作线程调用 callback，宿主自行切回 UI。
 * event_json 仅在回调期间有效，需保留时请复制。user_data 由宿主管理。
 * subscribe/unsubscribe/destroy 禁止在任何 SDK 回调内调用，返回错误 -8；
 * 请通知宿主线程执行。unsubscribe/destroy 返回时所有相关回调均已结束。
 * 销毁前宿主应停止其他线程继续使用该句柄，卸载 DLL 前必须销毁全部句柄。
 */
typedef void (CCHP_CALL *cchp_event_callback)(const char* event_json, void* user_data);
CCHP_API uint32_t CCHP_CALL cchp_subscribe(cchp_client client, const char* stream,
    const char* types, cchp_event_callback callback, void* user_data);
CCHP_API void CCHP_CALL cchp_unsubscribe(cchp_client client, uint32_t sub_id);
/* 线程本地错误 JSON：{status,code,message}。无需释放；有效至同一线程
 * 下一次 SDK 调用（重复调用 last_error 除外）。成功调用清除旧错误。 */
CCHP_API const char* CCHP_CALL cchp_last_error(void);

#ifdef __cplusplus
}
#endif
#endif
