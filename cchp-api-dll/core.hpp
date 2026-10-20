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
class Cancellation;
class Subscription;
using EventCallback = void (__stdcall *)(const char*, void*);
struct Response;
std::int64_t utc_now_ms();
class Client {
    std::mutex subscriptions_mutex_;
    std::map<std::uint32_t, std::unique_ptr<Subscription>> subscriptions_;
    std::uint32_t next_subscription_ = 1;
    bool closing_ = false;
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
    Json usage_logs(const Json& query);
    Json snapshot(std::int64_t now = utc_now_ms());
    std::uint32_t subscribe(const std::string& stream, const std::string& types, EventCallback callback, void* user);
    void unsubscribe(std::uint32_t id);
    void shutdown();
    Response open_stream(const std::string& path, const std::string& types, const std::string& last_id, Cancellation& cancel);
};
}
