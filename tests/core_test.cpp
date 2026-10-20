#include "core.hpp"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
    using namespace cchp;
    const auto cleaned = redact(Json{{"Cookie", "secret"}, {"nested", {{"access_token", "secret"},
        {"password", "secret"}, {"costUnits", 42}}}});
    CHECK(cleaned["Cookie"] == "[REDACTED]");
    CHECK(cleaned["nested"]["access_token"] == "[REDACTED]");
    CHECK(cleaned["nested"]["costUnits"] == 42);
    try { Config::parse(R"({"auth":{"type":"cookie","cookie":"placeholder"}})"); return 1; }
    catch (const Error& e) { CHECK(e.code == -1); }
    try { Config::parse(R"({"auth":{"type":"bearer","token":"bad\r\nheader"}})"); return 1; }
    catch (const Error& e) { CHECK(e.code == -1); }
    return 0;
}
