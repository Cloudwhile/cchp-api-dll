#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
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
    std::string auth_type, cookie, org_id, token, email, password;
    int timeout_ms = 30000;
    static Config parse(const char* text);
};
class Client {
public:
    Config config;
    explicit Client(Config value) : config(std::move(value)) {}
};
}
