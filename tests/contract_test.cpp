#include "mock_server.hpp"
#include "http.hpp"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
    using namespace cchp;
    MockServer server;
    auto config = [&](Json auth) { return Config::parse(Json{{"base_url", server.url()}, {"auth", auth}}.dump().c_str()); };
    Client bearer(config({{"type", "bearer"}, {"token", "test-only"}}));
    auto headers = bearer.request("GET", "/api/test/headers", {{"start", "2026-10-20T15:59:59.000Z"}, {"cursor", "a+b /中文"}});
    CHECK(headers["hasBearer"] == true);
    CHECK(headers["org"] == "");
    CHECK(headers["target"].get<std::string>().find("%3A") != std::string::npos);
    CHECK(headers["target"].get<std::string>().find("a%2Bb%20%2F%E4%B8%AD%E6%96%87") != std::string::npos);
    Client cookie(config({{"type", "cookie"}, {"cookie", "__Host-cch_session=session-fixture; cf_clearance=fixture"}, {"org_id", "fixture-org"}}));
    headers = cookie.request("GET", "/api/test/headers");
    CHECK(headers["org"] == "fixture-org");
    CHECK(headers["csrf"] == "");
    const Json write{{"name", "测试"}};
    headers = cookie.request("POST", "/api/test/headers", Json::object(), &write);
    CHECK(headers["csrf"] == "session-csrf");
    CHECK(headers["idempotency"] == true);
    CHECK(headers["origin"] == server.url());
    for (const auto& email : {"plain@fixture.invalid", "mfa@fixture.invalid"}) {
        Client password(config({{"type", "password"}, {"email", email}, {"password", "fixture-only"},
            {"mfa", {{"method", "totp"}, {"code", "fixture-code"}}}}));
        headers = password.request("GET", "/api/test/headers");
        CHECK(headers["hasSession"] == true);
        CHECK(headers["org"] == "fixture-org");
    }
    try {
        Client mfa(config({{"type", "password"}, {"email", "mfa@fixture.invalid"}, {"password", "fixture-only"}}));
        mfa.request("GET", "/api/test/headers"); return 1;
    } catch (const Error& e) { CHECK(e.status == 202 && e.code == -7); }
    CHECK(server.count("POST /api/auth/mfa/verify") == 1);
    try { bearer.request("GET", "/api/test/forbidden"); return 1; }
    catch (const Error& e) { CHECK(e.status == 403 && e.code == 10004); }
    try { bearer.request("GET", "/api/test/text403"); return 1; }
    catch (const Error& e) { CHECK(e.status == 403 && std::string(e.what()).find("Cloudflare") != std::string::npos); }
    try { bearer.request("GET", "/api/account/orgs"); return 1; }
    catch (const Error& e) { CHECK(e.status == 404 && e.code == -4); }
    try { bearer.request("GET", "/api/account/orgs"); return 1; }
    catch (const Error& e) { CHECK(e.status == 404); }
    bearer.request("GET", "/api/test/retry");
    CHECK(server.count("GET /api/test/retry") == 4);
    try { bearer.request("POST", "/api/test/retry", Json::object(), &write); return 1; }
    catch (const Error& e) { CHECK(e.status == 503); }
    CHECK(server.count("POST /api/test/retry") == 1);
    return 0;
}
