#include "cchp.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s; %s\n", __LINE__, #x, cchp_last_error()); return 1; } } while (0)
static void CCHP_CALL on_event(const char* event_json, void* user_data) {
    if (strstr(event_json, "usage_log_created")) SetEvent((HANDLE)user_data);
}
static DWORD WINAPI check_tls(void* ignored) {
    (void)ignored;
    cchp_get_me(NULL);
    return strstr(cchp_last_error(), "Invalid client") ? 0 : 1;
}
int run_abi_tests(const char* config) {
    cchp_client client;
    char* text;
    uint32_t sub;
    HANDLE event, thread;
    DWORD thread_code;
    CHECK(cchp_client_create("{") == NULL);
    CHECK(strstr(cchp_last_error(), "\"code\":-1") != NULL);
    client = cchp_client_create(config);
    CHECK(client != NULL);
    CHECK(strcmp(cchp_last_error(), "{}") == 0);
    thread = CreateThread(NULL, 0, check_tls, NULL, 0, NULL);
    CHECK(thread != NULL);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(thread, &thread_code) && thread_code == 0);
    CloseHandle(thread);
    CHECK(strcmp(cchp_last_error(), "{}") == 0);
    text = cchp_get_me(client); CHECK(text && strstr(text, "fixture-user")); cchp_string_free(text);
    text = cchp_usage_logs(client, "{\"limit\":200}"); CHECK(text && strstr(text, "\"page\"")); cchp_string_free(text);
    text = cchp_usage_summary(client); CHECK(text && strstr(text, "costUnits")); cchp_string_free(text);
    text = cchp_usage_by_model(client, "{\"range\":\"today\"}"); CHECK(text); cchp_string_free(text);
    text = cchp_usage_timeseries(client, "{\"range\":\"7d\",\"granularity\":\"day\"}"); CHECK(text); cchp_string_free(text);
    text = cchp_wallet_overview(client); CHECK(text && strstr(text, "153017766000")); cchp_string_free(text);
    text = cchp_quota_effective(client, "{\"scopeType\":\"org\",\"scopeId\":\"fixture-org\"}"); CHECK(text); cchp_string_free(text);
    CHECK(cchp_quota_effective(client, "{}") == NULL);
    text = cchp_status_snapshot(client); CHECK(text && strstr(text, "15.3017766")); cchp_string_free(text);
    CHECK(cchp_request(client, "GET", "/api/test/forbidden", NULL, NULL) == NULL);
    CHECK(strstr(cchp_last_error(), "\"status\":403") && strstr(cchp_last_error(), "\"code\":10004"));
    CHECK(cchp_subscribe(client, "invalid", NULL, on_event, NULL) == 0);
    event = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(event);
    sub = cchp_subscribe(client, "realtime", "usage_log_created", on_event, event); CHECK(sub);
    CHECK(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0);
    cchp_unsubscribe(client, sub); CHECK(strcmp(cchp_last_error(), "{}") == 0);
    ResetEvent(event);
    sub = cchp_subscribe(client, "realtime", NULL, on_event, event); CHECK(sub);
    CHECK(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0);
    cchp_string_free(NULL);
    cchp_client_destroy(client);
    CHECK(strcmp(cchp_last_error(), "{}") == 0);
    CloseHandle(event);
    cchp_client_destroy(client);
    CHECK(strstr(cchp_last_error(), "Invalid client") != NULL);
    return 0;
}
