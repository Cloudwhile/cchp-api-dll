#include "http.hpp"
#include "sse.hpp"
#include <rpc.h>
#include <chrono>
#include <sstream>
#include <thread>

namespace cchp {
namespace {
std::string uuid() {
    UUID id{};
    const auto status = UuidCreate(&id);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY) throw Error(0, -2, "UUID creation failed");
    RPC_CSTR text = nullptr;
    if (UuidToStringA(&id, &text) != RPC_S_OK) throw Error(0, -2, "UUID encoding failed");
    struct Free { void operator()(unsigned char* p) const { RpcStringFreeA(&p); } };
    const std::unique_ptr<unsigned char, Free> owner(text);
    return reinterpret_cast<const char*>(text);
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}
}
Client::Client(Config value) : config(std::move(value)) {
    transport_ = std::make_unique<Transport>(config);
    std::istringstream stream(config.cookie);
    std::string pair;
    while (std::getline(stream, pair, ';')) merge_cookies({trim(pair)});
    authenticated_ = config.auth_type != "password";
}
Client::~Client() { shutdown(); }
void Client::merge_cookies(const std::vector<std::string>& cookies) {
    for (const auto& raw : cookies) {
        const auto end = raw.find(';'), equal = raw.find('=');
        if (equal == std::string::npos || equal >= end) continue;
        const auto name = trim(raw.substr(0, equal));
        const auto value = raw.substr(equal + 1, end == std::string::npos ? std::string::npos : end - equal - 1);
        if (name.empty()) continue;
        if (value.empty() || raw.find("Max-Age=0") != std::string::npos || raw.find("max-age=0") != std::string::npos) cookies_.erase(name);
        else cookies_[name] = value;
    }
}
void Client::capture_me(const Json& me) {
    if (!me.is_object()) throw Error(200, -4, "Invalid account/me response");
    if (me.contains("csrf_token") && me["csrf_token"].is_string()) csrf_ = me["csrf_token"].get<std::string>();
    if (!config.org_id.empty()) return;
    if (me.contains("activeOrganizationId") && me["activeOrganizationId"].is_string())
        config.org_id = me["activeOrganizationId"].get<std::string>();
    if (config.org_id.empty() && me.contains("memberships") && me["memberships"].is_array()) {
        for (const auto& item : me["memberships"]) {
            if (item.is_object() && item.contains("organization") && item["organization"].is_object() &&
                item["organization"].contains("id") && item["organization"]["id"].is_string()) {
                config.org_id = item["organization"]["id"].get<std::string>(); break;
            }
        }
    }
    if (config.org_id.empty() && me.contains("organizations") && me["organizations"].is_array()) {
        for (const auto& item : me["organizations"]) {
            if (item.is_object() && item.contains("id") && item["id"].is_string()) {
                config.org_id = item["id"].get<std::string>(); break;
            }
        }
    }
}
Json Client::perform(const std::string& method, const std::string& path, const Json& query, const Json* body, bool auth_route) {
    const bool write = method != "GET" && method != "HEAD" && method != "OPTIONS";
    Headers headers;
    if (config.auth_type == "bearer") headers["Authorization"] = "Bearer " + config.token;
    else {
        std::string cookie;
        for (const auto& item : cookies_) {
            if (!cookie.empty()) cookie += "; ";
            cookie += item.first + '=' + item.second;
        }
        if (!cookie.empty()) headers["Cookie"] = std::move(cookie);
        if (!config.org_id.empty()) headers["X-Cch-Organization"] = config.org_id;
        if (write) {
            if (csrf_.empty()) throw Error(0, -6, "Browser write requires a CSRF token from account/me");
            headers["X-CSRF-Token"] = csrf_;
            headers["Origin"] = transport_->origin;
        }
    }
    if (write) headers["Idempotency-Key"] = uuid();
    const auto payload = body ? body->dump() : std::string();
    for (int attempt = 0;; ++attempt) {
        try {
            auto response = transport_->open(method, path, query, headers, payload);
            merge_cookies(response.set_cookies);
            const auto status = response.status;
            auto result = response_json(response);
            if (auth_route && status == 202) {
                if (config.mfa_code.empty()) throw Error(202, -7, "MFA required: configure auth.mfa.method and auth.mfa.code");
                return Json{{"mfa_required", true}};
            }
            return result;
        } catch (const Error& error) {
            if (method != "GET" || attempt >= 3 || !retryable(error)) throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(100 << attempt));
        }
    }
}
void Client::ensure_authenticated() {
    if (authenticated_) return;
    const auto csrf = perform("GET", "/api/auth/csrf", Json::object(), nullptr, true);
    csrf_ = required_string(csrf, "csrf_token");
    const Json credentials{{"email", config.email}, {"password", config.password}};
    auto result = perform("POST", "/api/auth/sign-in/password", Json::object(), &credentials, true);
    if (result.value("mfa_required", false)) {
        const Json mfa{{"method", config.mfa_method}, {"code", config.mfa_code}};
        perform("POST", "/api/auth/mfa/verify", Json::object(), &mfa, true);
    }
    capture_me(perform("GET", "/api/account/me", Json::object(), nullptr, true));
    if (config.org_id.empty()) throw Error(0, -6, "No organization available; set auth.org_id");
    authenticated_ = true;
    config.password.clear();
    config.mfa_code.clear();
}
Json Client::request(const std::string& method, const std::string& path, const Json& query, const Json* body) {
    std::lock_guard<std::recursive_mutex> lock(auth_mutex_);
    ensure_authenticated();
    if (config.auth_type != "bearer" && method != "GET" && method != "HEAD" && method != "OPTIONS" && csrf_.empty())
        capture_me(perform("GET", "/api/account/me", Json::object(), nullptr, false));
    auto result = perform(method, path, query, body, false);
    if (path == "/api/account/me" && method == "GET") capture_me(result);
    return result;
}
}
