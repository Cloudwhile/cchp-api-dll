#include "http.hpp"
#include <array>
#include <cctype>
#include <limits>

namespace cchp {
HINTERNET RequestHandle::get() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_) throw Error(0, -9, "Request cancelled");
    return handle_.get();
}
void RequestHandle::close() { std::lock_guard<std::mutex> lock(mutex_); handle_.reset(); }
void Cancellation::attach(std::shared_ptr<RequestHandle> request) {
    std::lock_guard<std::mutex> lock(mutex_);
    request_ = std::move(request);
    if (stopped_) { request_->close(); throw Error(0, -9, "Request cancelled"); }
}
void Cancellation::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    if (request_) request_->close();
}
namespace {
[[noreturn]] void network_error() {
    const auto code = GetLastError();
    throw Error(0, -3, "WinHTTP failure (Win32 " + std::to_string(code) + ")");
}
void checked(BOOL result) { if (!result) network_error(); }
std::string header(HINTERNET request, DWORD name, DWORD* index = nullptr) {
    DWORD size = 0;
    if (!WinHttpQueryHeaders(request, name, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, index)) {
        if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) network_error();
    }
    std::wstring result(size / sizeof(wchar_t), L'\0');
    checked(WinHttpQueryHeaders(request, name, WINHTTP_HEADER_NAME_BY_INDEX, result.data(), &size, index));
    result.resize(size / sizeof(wchar_t));
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return utf8(result);
}
}
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<size_t>(INT_MAX)) throw Error(0, -1, "String too large");
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw Error(0, -1, "Invalid UTF-8 string");
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw Error(0, -3, "Invalid UTF-16 response header");
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::string url_encode(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
std::string query_string(const Json& query) {
    if (!query.is_object()) throw Error(0, -1, "Query must be an object");
    std::string result;
    for (auto it = query.begin(); it != query.end(); ++it) {
        if (it->is_null()) continue;
        if (!it->is_primitive()) throw Error(0, -1, "Query values must be scalar");
        result += result.empty() ? '?' : '&';
        result += url_encode(it.key()) + '=' + url_encode(it->is_string() ? it->get<std::string>() : it->dump());
    }
    return result;
}
Transport::Transport(const Config& config) {
    const auto base = wide(config.base_url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(base.c_str(), 0, 0, &parts) || parts.dwHostNameLength == 0 ||
        parts.dwUserNameLength || parts.dwPasswordLength || parts.dwUrlPathLength || parts.dwExtraInfoLength)
        throw Error(0, -1, "base_url must be an HTTP(S) origin without credentials, path or query");
    host_.assign(parts.lpszHostName, parts.dwHostNameLength);
    secure_ = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const bool local = host_ == L"localhost" || host_ == L"127.0.0.1" || host_ == L"[::1]" || host_ == L"::1";
    if (!secure_ && (parts.nScheme != INTERNET_SCHEME_HTTP || !local))
        throw Error(0, -1, "HTTPS is required (HTTP is allowed only for loopback tests)");
    port_ = parts.nPort;
    origin = config.base_url;
    session_.reset(WinHttpOpen(L"CCHP-Native/0.1", local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session_) network_error();
    checked(WinHttpSetTimeouts(session_.get(), config.timeout_ms, config.timeout_ms, config.timeout_ms, config.timeout_ms));
}
Response Transport::open(const std::string& method, const std::string& path, const Json& query,
                         const Headers& headers, const std::string& body, Cancellation* cancellation) {
    if (method != "GET" && method != "POST" && method != "PUT" && method != "DELETE" &&
        method != "PATCH" && method != "HEAD" && method != "OPTIONS") throw Error(0, -1, "Unsupported HTTP method");
    if ((path != "/api" && path.compare(0, 5, "/api/") != 0) || path.find_first_of("?#\\\r\n") != std::string::npos ||
        path.find('\0') != std::string::npos) throw Error(0, -1, "Path must start with /api/ and contain no query");
    Response response;
    response.connection.reset(WinHttpConnect(session_.get(), host_.c_str(), port_, 0));
    if (!response.connection) network_error();
    const auto target = wide(path + query_string(query));
    Internet opened(WinHttpOpenRequest(response.connection.get(), wide(method).c_str(), target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure_ ? WINHTTP_FLAG_SECURE : 0));
    if (!opened) network_error();
    response.request = std::make_shared<RequestHandle>(opened.get());
    opened.release();
    if (cancellation) {
        cancellation->attach(response.request);
        // An idle SSE stream is valid. Cancellation closes the request to wake reads.
        checked(WinHttpSetTimeouts(response.request->get(), 30000, 30000, 30000, 0));
    }
    DWORD disabled = WINHTTP_DISABLE_COOKIES;
    checked(WinHttpSetOption(response.request->get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    checked(WinHttpSetOption(response.request->get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    checked(WinHttpSetOption(response.request->get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)));
    std::string all_headers = headers.count("Accept") ? "" : "Accept: application/json\r\n";
    if (!body.empty()) all_headers += "Content-Type: application/json; charset=utf-8\r\n";
    for (const auto& item : headers) {
        if (item.second.find_first_of("\r\n") != std::string::npos || item.second.find('\0') != std::string::npos)
            throw Error(0, -1, "Invalid HTTP header value");
        all_headers += item.first + ": " + item.second + "\r\n";
    }
    if (body.size() > MAXDWORD) throw Error(0, -1, "Request body too large");
    const auto headers_w = wide(all_headers);
    checked(WinHttpSendRequest(response.request->get(), headers_w.c_str(), static_cast<DWORD>(headers_w.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0));
    checked(WinHttpReceiveResponse(response.request->get(), nullptr));
    DWORD status = 0, size = sizeof(status);
    checked(WinHttpQueryHeaders(response.request->get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX));
    response.status = static_cast<int>(status);
    response.content_type = header(response.request->get(), WINHTTP_QUERY_CONTENT_TYPE);
    DWORD index = 0;
    for (;;) {
        auto cookie = header(response.request->get(), WINHTTP_QUERY_SET_COOKIE, &index);
        if (cookie.empty()) break;
        response.set_cookies.push_back(std::move(cookie));
    }
    return response;
}
std::string Response::read_all() {
    std::string text;
    std::array<char, 8192> buffer{};
    for (;;) {
        DWORD read = 0;
        checked(WinHttpReadData(request->get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read));
        if (!read) return text;
        if (text.size() + read > 64 * 1024 * 1024) throw Error(status, -4, "Response exceeds 64 MiB limit");
        text.append(buffer.data(), read);
    }
}
Json response_json(Response& response) {
    const auto body = response.read_all();
    if (response.status == 204 && body.empty()) return nullptr;
    auto result = Json::parse(body, nullptr, false);
    if (result.is_discarded()) {
        if (response.status == 403) throw Error(403, -5,
            "Non-JSON 403: possible Cloudflare challenge; browser clearance is bound to browser/IP");
        throw Error(response.status, -4, "Server returned a non-JSON response");
    }
    if ((response.status < 200 || response.status >= 300) && response.status != 202) {
        std::int64_t code = 0;
        if (result.is_object() && result.contains("code") && result["code"].is_number_integer()) code = result["code"].get<std::int64_t>();
        // Do not echo arbitrary server text: it may contain credentials or request data.
        throw Error(response.status, code, "HTTP " + std::to_string(response.status) + " (business code " + std::to_string(code) + ")");
    }
    return result;
}
bool retryable(const Error& error) {
    return (error.status == 0 && error.code == -3) || error.status == 408 || error.status == 429 ||
           error.status == 500 || error.status == 502 || error.status == 503 || error.status == 504;
}
}
