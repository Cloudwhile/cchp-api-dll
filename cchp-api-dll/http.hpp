#pragma once
#include "core.hpp"
#include <windows.h>
#include <winhttp.h>
#include <map>
#include <vector>

namespace cchp {
struct InternetCloser {
    void operator()(void* handle) const noexcept { if (handle) WinHttpCloseHandle(handle); }
};
using Internet = std::unique_ptr<void, InternetCloser>;
using Headers = std::map<std::string, std::string>;
class RequestHandle {
    std::mutex mutex_;
    Internet handle_;
public:
    explicit RequestHandle(HINTERNET handle) : handle_(handle) {}
    HINTERNET get();
    void close();
};
class Cancellation {
    std::mutex mutex_;
    bool stopped_ = false;
    std::shared_ptr<RequestHandle> request_;
public:
    void attach(std::shared_ptr<RequestHandle> request);
    void stop();
};
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);
std::string url_encode(const std::string& value);
std::string query_string(const Json& query);
struct Response {
    Internet connection;
    std::shared_ptr<RequestHandle> request;
    int status = 0;
    std::string content_type;
    std::vector<std::string> set_cookies;
    std::string read_all();
};
class Transport {
    Internet session_;
    std::wstring host_;
    INTERNET_PORT port_ = 0;
    bool secure_ = true;
public:
    std::string origin;
    explicit Transport(const Config& config);
    Response open(const std::string& method, const std::string& path, const Json& query,
                  const Headers& headers, const std::string& body, Cancellation* cancellation = nullptr);
};
Json response_json(Response& response);
bool retryable(const Error& error);
}
