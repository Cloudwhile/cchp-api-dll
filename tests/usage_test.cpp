#include "mock_server.hpp"
#include "usage.hpp"
#include <cmath>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
    using namespace cchp;
    const auto instant = parse_time("2026-10-20T15:59:59.123Z");
    CHECK(parse_time("2026-10-20T23:59:59.123+08:00") == instant);
    CHECK(format_time(instant) == "2026-10-20T15:59:59.123Z");
    try { parse_time("2026-02-30T15:59:59Z"); return 1; } catch (const Error&) {}
    try { validate_usage_page(Json::array()); return 1; } catch (const Error&) {}
    MockServer server;
    const auto config = Json{{"base_url", server.url()}, {"auth", {{"type", "bearer"}, {"token", "test-only"}}}};
    Client client(Config::parse(config.dump().c_str()));
    auto logs = client.usage_logs({{"limit", 200}});
    CHECK(logs["page"]["items"][0]["completedAt"].is_null());
    CHECK(logs["page"]["items"][0]["futureField"] == true);
    const auto before = server.count("GET /api/account/usage/logs");
    auto snap = client.snapshot(server.now);
    CHECK(server.count("GET /api/account/usage/logs") == before + 2);
    CHECK(snap["nowActiveModel"] == "gpt-6.1-sol" && snap["lastModel"] == "gpt-6.1-sol");
    CHECK(snap["inputTokPerSec"] == 3.0 && snap["outputTokPerSec"] == 0.3);
    CHECK(std::abs(snap["balanceUsd"].get<double>() - 15.3017766) < 1e-9);
    CHECK(std::abs(snap["todayCostUsd"].get<double>() - 0.14) < 1e-12);
    server.usage_mode = 1;
    snap = client.snapshot(server.now);
    CHECK(snap["nowActiveModel"].is_null() && snap["lastModel"] == "last-model");
    CHECK(snap["inputTokPerSec"] == 0.0);
    server.usage_mode = 2;
    snap = client.snapshot(server.now);
    CHECK(snap["lastModel"].is_null() && snap["lastActiveAt"].is_null());
    server.usage_mode = 3;
    try { client.snapshot(server.now); return 1; } catch (const Error& e) { CHECK(e.status == 403 && e.code == 10004); }
    server.usage_mode = 4;
    try { client.snapshot(server.now); return 1; } catch (const Error& e) { CHECK(e.code == -4); }
    try { client.usage_logs({{"limit", 201}}); return 1; } catch (const Error& e) { CHECK(e.code == -1); }
    return 0;
}
