#include "mock_server.hpp"
#include "sse.hpp"
#include "cchp.h"
#include <chrono>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
struct Events {
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<cchp::Json> values;
    cchp_client client = nullptr;
    bool rejected = false;
};
void CCHP_CALL receive(const char* text, void* opaque) {
    auto& events = *static_cast<Events*>(opaque);
    if (events.client) {
        cchp_client_destroy(events.client);
        events.rejected = std::string(cchp_last_error()).find("\"code\":-8") != std::string::npos;
        cchp_unsubscribe(events.client, 1);
        events.rejected = events.rejected && std::string(cchp_last_error()).find("\"code\":-8") != std::string::npos;
    }
    std::lock_guard<std::mutex> lock(events.mutex);
    events.values.push_back(cchp::Json::parse(text));
    events.wake.notify_all();
}
int main() {
    using namespace cchp;
    std::vector<Json> parsed;
    SseParser parser([&](const Json& event) { parsed.push_back(event); });
    const std::string bytes = "\xEF\xBB\xBF: comment\r\nretry: 150\r\nid: one\r\nevent: heartbeat\r\ndata: {\"中文\":\r\ndata: 1}\r\n\r\n";
    for (const auto c : bytes) parser.feed(&c, 1);
    CHECK(parsed.size() == 1 && parsed[0]["data"]["中文"] == 1);
    CHECK(parser.last_id == "one" && parser.retry_ms == 150);
    const std::string second = "id:\n\ndata: null\n\ndata: {\"unfinished\":true}";
    parser.feed(second.data(), second.size());
    CHECK(parsed.size() == 2 && parsed.back()["event"] == "message" && parser.last_id.empty());
    MockServer server;
    const auto config = Json{{"base_url", server.url()}, {"auth", {{"type", "bearer"}, {"token", "test-only"}}}}.dump();
    Events events;
    events.client = cchp_client_create(config.c_str());
    CHECK(events.client);
    const auto id = cchp_subscribe(events.client, "realtime", "usage_log_created,quota.*", receive, &events);
    CHECK(id);
    {
        std::unique_lock<std::mutex> lock(events.mutex);
        CHECK(events.wake.wait_for(lock, std::chrono::seconds(5), [&] { return events.values.size() >= 2; }));
        CHECK(events.values[0]["data"]["model"] == "中文");
        CHECK(events.values[1]["data"]["resumed"] == true && events.rejected);
    }
    CHECK(server.sse_resumed == 1);
    auto result = cchp_get_me(events.client); // Rejected callback destroy retained the handle.
    CHECK(result); cchp_string_free(result);
    const auto started = std::chrono::steady_clock::now();
    cchp_unsubscribe(events.client, id);
    CHECK(std::string(cchp_last_error()) == "{}");
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
    cchp_client_destroy(events.client);
    CHECK(std::string(cchp_last_error()) == "{}");
    Events metrics;
    metrics.client = cchp_client_create(config.c_str());
    CHECK(cchp_subscribe(metrics.client, "metrics", nullptr, receive, &metrics));
    {
        std::unique_lock<std::mutex> lock(metrics.mutex);
        CHECK(metrics.wake.wait_for(lock, std::chrono::seconds(5), [&] { return !metrics.values.empty(); }));
        CHECK(metrics.values.front()["event"] == "scope");
    }
    cchp_client_destroy(metrics.client); // Must cancel and join an idle worker.
    CHECK(std::string(cchp_last_error()) == "{}");
    return 0;
}
