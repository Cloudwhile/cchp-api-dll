#include "mock_server.hpp"
#include "usage.hpp"
#include "http.hpp"
#include <array>
#include <algorithm>
#include <sstream>

namespace {
bool send_all(SOCKET socket, const std::string& value) {
    size_t offset = 0;
    while (offset < value.size()) {
        const auto sent = send(socket, value.data() + offset, static_cast<int>(value.size() - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}
MockServer::MockServer() {
    now = cchp::utc_now_ms();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed");
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener_ == INVALID_SOCKET || bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener_, 16)) {
        if (listener_ != INVALID_SOCKET) closesocket(listener_);
        WSACleanup();
        throw std::runtime_error("mock server bind failed");
    }
    int size = sizeof(address);
    getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
    port_ = ntohs(address.sin_port);
    acceptor_ = std::thread([this] {
        while (!stopping_) {
            const auto incoming = accept(listener_, nullptr, nullptr);
            if (incoming == INVALID_SOCKET) break;
            workers_.emplace_back([this, incoming] {
                try { serve(incoming); } catch (...) { /* test peer can disconnect */ }
                shutdown(incoming, SD_BOTH);
                closesocket(incoming);
            });
        }
    });
}
MockServer::~MockServer() {
    stopping_ = true;
    shutdown(listener_, SD_BOTH);
    closesocket(listener_);
    if (acceptor_.joinable()) acceptor_.join();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
    WSACleanup();
}
std::string MockServer::url() const { return "http://127.0.0.1:" + std::to_string(port_); }
int MockServer::count(const std::string& route) {
    std::lock_guard<std::mutex> lock(mutex_);
    return counts_[route];
}
void MockServer::serve(SOCKET socket) {
    DWORD timeout = 2000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::string raw;
    std::array<char, 4096> buffer{};
    while (raw.find("\r\n\r\n") == std::string::npos) {
        const auto read = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (read <= 0 || raw.size() > 1024 * 1024) return;
        raw.append(buffer.data(), static_cast<size_t>(read));
    }
    const auto split = raw.find("\r\n\r\n");
    std::istringstream input(raw.substr(0, split));
    std::string method, target, version, line;
    input >> method >> target >> version;
    std::getline(input, line);
    std::map<std::string, std::string> headers;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            auto value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            headers[lower(line.substr(0, colon))] = std::move(value);
        }
    }
    size_t length = 0;
    if (headers.count("content-length")) length = static_cast<size_t>(std::stoul(headers["content-length"]));
    std::string body = raw.substr(split + 4);
    while (body.size() < length) {
        const auto read = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (read <= 0) return;
        body.append(buffer.data(), static_cast<size_t>(read));
    }
    const auto path = target.substr(0, target.find('?'));
    int call = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        call = ++counts_[method + " " + path];
    }
    using cchp::Json;
    int status = 200;
    std::string content_type = "application/json", extra;
    Json result = Json::object();
    std::string text;
    const auto payload = body.empty() ? Json::object() : Json::parse(body, nullptr, false);
    if (path == "/api/account/usage/realtime" || path == "/api/account/metrics/stream") {
        if (headers["authorization"] != "Bearer test-only" || headers["accept"] != "text/event-stream") {
            send_all(socket, "HTTP/1.1 403 Fixture\r\nContent-Type: application/json\r\nContent-Length: 14\r\n\r\n{\"code\":10004}");
            return;
        }
        send_all(socket, "HTTP/1.1 200 Fixture\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n");
        if (headers["last-event-id"] == "evt-1") {
            ++sse_resumed;
            send_all(socket, "id: evt-2\nevent: usage_log_updated\ndata: {\"resumed\":true}\n\n");
        } else if (path == "/api/account/metrics/stream") {
            send_all(socket, "event: scope\ndata: {\"organizationId\":\"fixture-org\"}\n\n");
        } else {
            const std::string frame = "retry: 100\r\nid: evt-1\r\nevent: usage_log_created\r\ndata: {\"model\":\"中文\",\r\ndata: \"tokens\":1}\r\n\r\n";
            for (char c : frame) if (!send_all(socket, std::string(1, c))) return;
            return; // Force a real reconnect with Last-Event-ID.
        }
        // Stay idle until client cancellation closes the socket.
        while (!stopping_) {
            const auto received = recv(socket, buffer.data(), 1, 0);
            if (received == 0 || (received == SOCKET_ERROR && WSAGetLastError() != WSAETIMEDOUT)) return;
        }
        return;
    } else if (path == "/api/auth/csrf") {
        result = {{"csrf_token", "preauth-fixture"}};
        extra = "Set-Cookie: __Host-cch_preauth=pre=a; Path=/; HttpOnly\r\n";
    } else if (path == "/api/auth/sign-in/password") {
        if (headers["cookie"].find("__Host-cch_preauth=pre=a") == std::string::npos ||
            headers["x-csrf-token"] != "preauth-fixture" || headers["origin"] != url() || headers["idempotency-key"].size() != 36) status = 400;
        else if (payload.value("email", "") == "mfa@fixture.invalid") { status = 202; result = {{"challenge", true}}; }
        else extra = "Set-Cookie: __Host-cch_session=session-fixture; Path=/; HttpOnly\r\nSet-Cookie: login_hint=fixture; Path=/\r\n";
    } else if (path == "/api/auth/mfa/verify") {
        if (payload.value("code", "") != "fixture-code" || headers["cookie"].find("__Host-cch_preauth=pre=a") == std::string::npos) status = 400;
        else extra = "Set-Cookie: __Host-cch_session=session-fixture; Path=/; HttpOnly\r\n";
    } else if (path == "/api/account/me") {
        result = {{"user", {{"id", "fixture-user"}, {"displayName", "测试用户"}}}, {"csrf_token", "session-csrf"},
            {"memberships", Json::array({{{"organization", {{"id", "fixture-org"}}}}})}};
    } else if (path == "/api/account/usage/logs") {
        const bool second_page = target.find("cursor=") != std::string::npos;
        const bool fallback = target.find(cchp::url_encode(cchp::format_time(now - 30LL * 86400000))) != std::string::npos;
        Json items = Json::array();
        if (usage_mode == 3) { status = 403; result = {{"code", 10004}}; }
        else {
            if (usage_mode == 0 || usage_mode == 4) {
                if (!second_page) items.push_back({{"id", "pending"}, {"createdAt", cchp::format_time(now - 1000)},
                    {"completedAt", nullptr}, {"requestedModel", "gpt-6.1-sol"}, {"finalModel", nullptr},
                    {"inputTokens", nullptr}, {"outputTokens", nullptr}, {"httpStatus", nullptr}, {"futureField", true}});
                items.push_back({{"id", second_page ? "complete-2" : "complete-1"},
                    {"createdAt", cchp::format_time(now - 180000)}, {"finalModel", "previous-model"},
                    {"inputTokens", second_page ? 600 : 300}, {"outputTokens", second_page ? 60 : 30}});
            } else if (usage_mode == 1 && fallback) {
                items.push_back({{"id", "old"}, {"createdAt", cchp::format_time(now - 86400000)},
                    {"requestedModel", "last-model"}, {"inputTokens", nullptr}, {"outputTokens", nullptr}});
            }
            const bool more = usage_mode == 4 || (usage_mode == 0 && !second_page);
            result = {{"$schema", "fixture-only"}, {"effectiveScope", "organization"},
                {"page", {{"items", items}, {"nextCursor", more ? Json("a+b /中文") : Json(nullptr)}, {"total", 3}}}};
        }
    } else if (path == "/api/account/usage/summary") {
        result = {{"today", {{"costUnits", 1400000000LL}}}, {"unknownFutureWindow", 42}};
    } else if (path == "/api/account/wallet") {
        result = {{"balanceUnits", 153017766000LL}, {"packages", Json::array()}};
    } else if (path == "/api/account/usage/by-model" || path == "/api/account/usage/timeseries" ||
               path == "/api/account/quota/effective") {
        result = {{"fixture", true}, {"target", target}};
    } else if (path == "/api/test/headers") {
        result = {{"hasBearer", headers["authorization"] == "Bearer test-only"},
            {"org", headers["x-cch-organization"]}, {"csrf", headers["x-csrf-token"]},
            {"hasSession", headers["cookie"].find("__Host-cch_session=session-fixture") != std::string::npos},
            {"idempotency", headers["idempotency-key"].size() == 36}, {"origin", headers["origin"]},
            {"target", target}};
    } else if (path == "/api/test/forbidden") {
        status = 403; result = {{"status", 403}, {"code", 10004}, {"detail", "fixture forbidden"}};
    } else if (path == "/api/test/text403") {
        status = 403; content_type = "text/html"; text = "<html>Cloudflare challenge</html>";
    } else if (path == "/api/test/retry") {
        if (method != "GET" || call <= 3) { status = 503; result = {{"code", 30001}}; }
    } else if (path == "/api/account/orgs") {
        status = 404; content_type = "text/plain"; text = "Not found";
    } else { status = 404; result = {{"code", 404}}; }
    if (text.empty()) text = result.dump();
    send_all(socket, "HTTP/1.1 " + std::to_string(status) + " Fixture\r\nContent-Type: " + content_type +
        "\r\nContent-Length: " + std::to_string(text.size()) + "\r\nConnection: close\r\n" + extra + "\r\n" + text);
}
