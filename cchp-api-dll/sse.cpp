#include "sse.hpp"
#include <algorithm>
#include <array>
#include <chrono>

namespace cchp {
namespace { thread_local bool callback_active = false; }
bool in_callback() noexcept { return callback_active; }
void SseParser::line() {
    if (first_) {
        first_ = false;
        if (line_.compare(0, 3, "\xEF\xBB\xBF") == 0) line_.erase(0, 3);
    }
    if (line_.empty()) {
        last_id = id_;
        if (has_data_) {
            data_.pop_back();
            auto data = Json::parse(data_, nullptr, false);
            if (data.is_discarded()) throw Error(0, -4, "SSE data is not valid UTF-8 JSON");
            emit_(Json{{"event", type_.empty() ? "message" : type_}, {"id", id_}, {"data", std::move(data)}});
        }
        data_.clear(); type_.clear(); has_data_ = false;
        return;
    }
    if (line_[0] == ':') return;
    const auto colon = line_.find(':');
    const auto field = line_.substr(0, colon);
    auto value = colon == std::string::npos ? std::string() : line_.substr(colon + 1);
    if (!value.empty() && value[0] == ' ') value.erase(0, 1);
    if (field == "data") {
        if (data_.size() + value.size() > 4 * 1024 * 1024) throw Error(0, -4, "SSE event exceeds 4 MiB");
        data_ += value + '\n'; has_data_ = true;
    } else if (field == "event") type_ = std::move(value);
    else if (field == "id" && value.find('\0') == std::string::npos) id_ = std::move(value);
    else if (field == "retry" && !value.empty() && value.find_first_not_of("0123456789") == std::string::npos) {
        unsigned delay = 0;
        for (char c : value) delay = (std::min)(30000u, delay * 10 + static_cast<unsigned>(c - '0'));
        retry_ms = static_cast<int>((std::max)(100u, delay));
    }
}
void SseParser::feed(const char* bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        const char c = bytes[i];
        if (skip_lf_ && c == '\n') { skip_lf_ = false; continue; }
        skip_lf_ = c == '\r';
        if (c == '\r' || c == '\n') { line(); line_.clear(); }
        else {
            if (line_.size() >= 65536) throw Error(0, -4, "SSE line exceeds 64 KiB");
            line_ += c;
        }
    }
}
Subscription::Subscription(Client& client, std::string path, std::string types, EventCallback callback, void* user)
    : client_(client), path_(std::move(path)), types_(std::move(types)), callback_(callback), user_(user) {}
Subscription::~Subscription() { cancel(); join(); }
void Subscription::start() { thread_ = std::thread([this] { run(); }); }
void Subscription::cancel() { stopping_ = true; cancellation_.stop(); wake_.notify_all(); }
void Subscription::join() { if (thread_.joinable()) thread_.join(); }
void Subscription::emit(const Json& event) {
    if (stopping_) return;
    const auto text = event.dump();
    struct Guard { Guard() { callback_active = true; } ~Guard() { callback_active = false; } } guard;
    try { callback_(text.c_str(), user_); }
    catch (...) { throw Error(0, -8, "Host callback threw an exception; subscription stopped"); }
}
void Subscription::run() noexcept {
    try {
        std::string last_id;
        int backoff = 500;
        while (!stopping_) {
            bool terminal = false;
            SseParser parser([&](const Json& event) { emit(event); backoff = 500; }, last_id);
            try {
                auto response = client_.open_stream(path_, types_, last_id, cancellation_);
                if (response.status < 200 || response.status >= 300) response_json(response);
                if (response.status == 204) return;
                if (response.content_type.find("text/event-stream") == std::string::npos)
                    throw Error(response.status, -4, "Expected text/event-stream");
                std::array<char, 8192> buffer{};
                while (!stopping_) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(response.request->get(), &available))
                        throw Error(0, -3, "SSE availability query failed (Win32 " + std::to_string(GetLastError()) + ")");
                    if (!available) break;
                    DWORD read = 0;
                    if (!WinHttpReadData(response.request->get(), buffer.data(),
                        (std::min)(available, static_cast<DWORD>(buffer.size())), &read))
                        throw Error(0, -3, "SSE read failed (Win32 " + std::to_string(GetLastError()) + ")");
                    if (!read) break;
                    parser.feed(buffer.data(), read);
                }
            } catch (const Error& error) {
                terminal = !retryable(error);
                if (!stopping_) emit({{"event", "sdk_error"}, {"id", parser.last_id},
                    {"data", {{"status", error.status}, {"code", error.code}, {"message", error.what()}, {"terminal", terminal}}}});
            }
            last_id = parser.last_id;
            if (terminal || stopping_) break;
            std::unique_lock<std::mutex> lock(wait_mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds((std::max)(backoff, parser.retry_ms)), [&] { return stopping_.load(); });
            backoff = (std::min)(30000, backoff * 2);
        }
    } catch (...) { /* No exception may escape the worker or a host callback. */ }
}
std::uint32_t Client::subscribe(const std::string& stream, const std::string& types, EventCallback callback, void* user) {
    if (in_callback()) throw Error(0, -8, "Subscription lifecycle calls must run outside SDK callbacks");
    if (!callback || (stream != "realtime" && stream != "metrics")) throw Error(0, -1, "stream must be realtime or metrics and callback is required");
    if (types.find_first_of("\r\n") != std::string::npos || types.find('\0') != std::string::npos)
        throw Error(0, -1, "Invalid SSE types");
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    if (closing_) throw Error(0, -9, "Client is closing");
    if (next_subscription_ == 0) throw Error(0, -2, "Subscription IDs exhausted");
    const auto id = next_subscription_++;
    auto sub = std::make_unique<Subscription>(*this, stream == "realtime" ? "/api/account/usage/realtime" : "/api/account/metrics/stream",
        types, callback, user);
    auto& stored = subscriptions_.emplace(id, std::move(sub)).first->second;
    try { stored->start(); } catch (...) { subscriptions_.erase(id); throw; }
    return id;
}
void Client::unsubscribe(std::uint32_t id) {
    if (in_callback()) throw Error(0, -8, "Subscription lifecycle calls must run outside SDK callbacks");
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    auto it = subscriptions_.find(id);
    if (it == subscriptions_.end()) throw Error(0, -1, "Invalid subscription ID");
    subscriptions_.erase(it); // cancel + join before returning
}
void Client::shutdown() {
    if (in_callback()) throw Error(0, -8, "Client destruction must run outside SDK callbacks");
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    closing_ = true;
    for (auto& sub : subscriptions_) sub.second->cancel();
    subscriptions_.clear(); // all workers joined outside DllMain
}
Response Client::open_stream(const std::string& path, const std::string& types, const std::string& last_id, Cancellation& cancel) {
    Headers headers;
    {
        std::lock_guard<std::recursive_mutex> lock(auth_mutex_);
        ensure_authenticated();
        if (config.auth_type == "bearer") headers["Authorization"] = "Bearer " + config.token;
        else {
            std::string cookie;
            for (const auto& item : cookies_) { if (!cookie.empty()) cookie += "; "; cookie += item.first + '=' + item.second; }
            headers["Cookie"] = std::move(cookie);
            headers["X-Cch-Organization"] = config.org_id;
        }
    }
    headers["Accept"] = "text/event-stream";
    if (!last_id.empty()) headers["Last-Event-ID"] = last_id;
    Json query = Json::object();
    if (!types.empty() && path == "/api/account/usage/realtime") query["types"] = types;
    auto response = transport_->open("GET", path, query, headers, {}, &cancel);
    {
        std::lock_guard<std::recursive_mutex> lock(auth_mutex_);
        merge_cookies(response.set_cookies);
    }
    return response;
}
}
