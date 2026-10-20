#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <map>
#include <stdexcept>
#include <string>

namespace cchp {
using Json = nlohmann::json;
class Error : public std::runtime_error {
public:
    int status;
    std::int64_t code;
    Error(int status_, std::int64_t code_, const std::string& message)
        : std::runtime_error(message), status(status_), code(code_) {}
};
Json parse_object(const char* text, bool optional = false);
std::string required_string(const Json& value, const char* key);
Json redact(Json value);
struct Config {
    std::string base_url = "https://ccx.autobits.cc";
    std::string auth_type, cookie, org_id, token, email, password, mfa_method, mfa_code;
    int timeout_ms = 30000;
    static Config parse(const char* text);
};
class Transport;
struct Response;
class Client {
    std::unique_ptr<Transport> transport_;
    std::recursive_mutex auth_mutex_;
    std::map<std::string, std::string> cookies_;
    std::string csrf_;
    bool authenticated_ = false;
    void merge_cookies(const std::vector<std::string>& cookies);
    Json perform(const std::string& method, const std::string& path, const Json& query, const Json* body, bool auth_route);
    void ensure_authenticated();
    void capture_me(const Json& me);
public:
    Config config;
    explicit Client(Config value);
    ~Client();
    Json request(const std::string& method, const std::string& path, const Json& query = Json::object(), const Json* body = nullptr);
};
}
