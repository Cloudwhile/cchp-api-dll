#include "cchp.h"
#include "core.hpp"
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {
thread_local char last_error[8192] = "{}";
std::mutex clients_mutex;
std::unordered_map<void*, std::shared_ptr<cchp::Client>> clients;
std::shared_ptr<cchp::Client> get_client(cchp_client handle) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    const auto it = clients.find(handle);
    if (it == clients.end()) throw cchp::Error(0, -1, "Invalid client handle");
    return it->second;
}
char* allocated(const cchp::Json& json) {
    const auto text = json.dump();
    auto result = std::make_unique<char[]>(text.size() + 1);
    std::memcpy(result.get(), text.c_str(), text.size() + 1);
    return result.release();
}
void clear_error() noexcept { std::memcpy(last_error, "{}", 3); }
void set_error(int status, std::int64_t code, const char* message) noexcept {
    try {
        auto text = cchp::Json({{"status", status}, {"code", code}, {"message", message}}).dump(-1, ' ', false,
            cchp::Json::error_handler_t::replace);
        if (text.size() >= sizeof(last_error)) throw std::length_error("error too long");
        std::memcpy(last_error, text.c_str(), text.size() + 1);
    } catch (...) {
        std::snprintf(last_error, sizeof(last_error), "{\"status\":%d,\"code\":-2,\"message\":\"SDK failure\"}", status);
    }
}
void caught_error() noexcept {
    try { throw; }
    catch (const cchp::Error& error) { set_error(error.status, error.code, error.what()); }
    catch (...) { set_error(0, -2, "SDK failure (exception contained at C ABI boundary)"); }
}
}
extern "C" {
cchp_client CCHP_CALL cchp_client_create(const char* config_json) {
    try {
        clear_error();
        auto client = std::make_shared<cchp::Client>(cchp::Config::parse(config_json));
        auto handle = client.get();
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.emplace(handle, std::move(client));
        return handle;
    } catch (...) { caught_error(); return nullptr; }
}
void CCHP_CALL cchp_client_destroy(cchp_client client) {
    try {
        clear_error();
        std::shared_ptr<cchp::Client> removed;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = clients.find(client);
            if (it == clients.end()) throw cchp::Error(0, -1, "Invalid client handle");
            removed = std::move(it->second);
            clients.erase(it);
        }
    } catch (...) { caught_error(); }
}
void CCHP_CALL cchp_string_free(char* string) {
    try { clear_error(); delete[] string; }
    catch (...) { caught_error(); }
}
char* CCHP_CALL cchp_request(cchp_client client, const char* method, const char* path,
                          const char* query_json, const char* body_json) {
    try {
        clear_error();
        if (!method || !path) throw cchp::Error(0, -1, "method and path are required");
        const auto query = cchp::parse_object(query_json, true);
        cchp::Json body;
        if (body_json) {
            body = cchp::Json::parse(body_json, nullptr, false);
            if (body.is_discarded()) throw cchp::Error(0, -1, "Invalid body JSON");
        }
        return allocated(get_client(client)->request(method, path, query, body_json ? &body : nullptr));
    } catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_get_me(cchp_client client) {
    try { clear_error(); return allocated(get_client(client)->request("GET", "/api/account/me")); }
    catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_usage_logs(cchp_client client, const char* query_json) {
    try { clear_error(); return allocated(get_client(client)->usage_logs(cchp::parse_object(query_json, true))); }
    catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_usage_summary(cchp_client client) {
    try { clear_error(); return allocated(get_client(client)->request("GET", "/api/account/usage/summary")); }
    catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_usage_by_model(cchp_client client, const char* query_json) {
    try { clear_error(); return allocated(get_client(client)->request("GET", "/api/account/usage/by-model", cchp::parse_object(query_json, true))); }
    catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_usage_timeseries(cchp_client client, const char* query_json) {
    try { clear_error(); return allocated(get_client(client)->request("GET", "/api/account/usage/timeseries", cchp::parse_object(query_json, true))); }
    catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_wallet_overview(cchp_client client) {
    try { clear_error(); return allocated(get_client(client)->request("GET", "/api/account/wallet")); }
    catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_quota_effective(cchp_client client, const char* query_json) {
    try {
        clear_error();
        const auto query = cchp::parse_object(query_json);
        const auto scope = cchp::required_string(query, "scopeType");
        cchp::required_string(query, "scopeId");
        if (scope != "user" && scope != "org" && scope != "key") throw cchp::Error(0, -1, "scopeType must be user, org or key");
        return allocated(get_client(client)->request("GET", "/api/account/quota/effective", query));
    } catch (...) { caught_error(); return nullptr; }
}
char* CCHP_CALL cchp_status_snapshot(cchp_client client) {
    try { clear_error(); return allocated(get_client(client)->snapshot()); }
    catch (...) { caught_error(); return nullptr; }
}
const char* CCHP_CALL cchp_last_error(void) {
    try { return last_error; }
    catch (...) { return "{\"status\":0,\"code\":-2,\"message\":\"SDK failure\"}"; }
}
}
