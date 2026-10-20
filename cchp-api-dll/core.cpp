#include "core.hpp"
#include <algorithm>
#include <cctype>

namespace cchp {
Json parse_object(const char* text, bool optional) {
    if (!text || !*text) {
        if (optional) return Json::object();
        throw Error(0, -1, "Expected a JSON object");
    }
    auto value = Json::parse(text, nullptr, false);
    if (value.is_discarded() || !value.is_object())
        throw Error(0, -1, "Expected a valid UTF-8 JSON object");
    return value;
}
std::string required_string(const Json& value, const char* key) {
    auto it = value.find(key);
    if (it == value.end() || !it->is_string() || it->get_ref<const std::string&>().empty())
        throw Error(0, -1, std::string("Missing string field: ") + key);
    auto result = it->get<std::string>();
    if (result.find_first_of("\r\n") != std::string::npos || result.find('\0') != std::string::npos)
        throw Error(0, -1, std::string("Invalid control character in: ") + key);
    return result;
}
Json redact(Json value) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            auto key = it.key();
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (key == "cookie" || key == "set-cookie" || key == "authorization" ||
                key == "password" || key == "token" ||
                (key.size() >= 6 && key.compare(key.size() - 6, 6, "_token") == 0))
                it.value() = "[REDACTED]";
            else it.value() = redact(std::move(it.value()));
        }
    } else if (value.is_array()) {
        for (auto& item : value) item = redact(std::move(item));
    }
    return value;
}
Config Config::parse(const char* text) {
    const auto root = parse_object(text);
    Config result;
    if (root.contains("base_url")) result.base_url = required_string(root, "base_url");
    while (!result.base_url.empty() && result.base_url.back() == '/') result.base_url.pop_back();
    if (!root.contains("auth") || !root["auth"].is_object()) throw Error(0, -1, "Missing auth object");
    const auto& auth = root["auth"];
    result.auth_type = required_string(auth, "type");
    if (auth.contains("org_id")) result.org_id = required_string(auth, "org_id");
    if (result.auth_type == "cookie") {
        result.cookie = required_string(auth, "cookie");
        result.org_id = required_string(auth, "org_id");
    } else if (result.auth_type == "bearer") result.token = required_string(auth, "token");
    else if (result.auth_type == "password") {
        result.email = required_string(auth, "email");
        result.password = required_string(auth, "password");
        if (auth.contains("mfa")) {
            if (!auth["mfa"].is_object()) throw Error(0, -1, "mfa must be an object");
            result.mfa_method = required_string(auth["mfa"], "method");
            result.mfa_code = required_string(auth["mfa"], "code");
            if (result.mfa_method != "totp" && result.mfa_method != "recovery_code") throw Error(0, -1, "Unsupported MFA method");
        }
    } else throw Error(0, -1, "Unknown auth type");
    if (root.contains("timeout_ms")) {
        if (!root["timeout_ms"].is_number_integer()) throw Error(0, -1, "Invalid timeout_ms");
        const auto timeout = root["timeout_ms"].get<std::int64_t>();
        if (timeout < 100 || timeout > 300000) throw Error(0, -1, "timeout_ms must be 100..300000");
        result.timeout_ms = static_cast<int>(timeout);
    }
    return result;
}
}
