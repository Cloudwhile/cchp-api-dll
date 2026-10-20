#include "usage.hpp"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <limits>
#include <set>

namespace cchp {
namespace {
constexpr std::int64_t epoch_ticks = 116444736000000000LL;
int digits(const std::string& text, size_t offset, size_t count) {
    if (offset + count > text.size()) throw Error(0, -4, "Invalid RFC3339 timestamp");
    int result = 0;
    for (size_t i = offset; i < offset + count; ++i) {
        if (text[i] < '0' || text[i] > '9') throw Error(0, -4, "Invalid RFC3339 timestamp");
        result = result * 10 + text[i] - '0';
    }
    return result;
}
std::optional<std::int64_t> nullable_integer(const Json& row, const char* name) {
    auto it = row.find(name);
    if (it == row.end() || it->is_null()) return std::nullopt;
    return integer_units(*it);
}
std::optional<std::string> nullable_string(const Json& row, const char* name) {
    auto it = row.find(name);
    if (it == row.end() || it->is_null()) return std::nullopt;
    if (!it->is_string()) throw Error(0, -4, "Invalid usage string field");
    return it->get<std::string>();
}
void add_tokens(std::int64_t& total, const std::optional<std::int64_t>& amount) {
    const auto count = amount.value_or(0);
    if (count < 0 || count > (std::numeric_limits<std::int64_t>::max)() - total)
        throw Error(0, -4, "Invalid or overflowing token count");
    total += count;
}
}
std::int64_t utc_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::int64_t parse_time(const std::string& text) {
    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':')
        throw Error(0, -4, "Invalid RFC3339 timestamp");
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(digits(text, 0, 4));
    st.wMonth = static_cast<WORD>(digits(text, 5, 2));
    st.wDay = static_cast<WORD>(digits(text, 8, 2));
    st.wHour = static_cast<WORD>(digits(text, 11, 2));
    st.wMinute = static_cast<WORD>(digits(text, 14, 2));
    st.wSecond = static_cast<WORD>(digits(text, 17, 2));
    size_t pos = 19;
    if (text[pos] == '.') {
        const auto start = ++pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
        if (pos == start) throw Error(0, -4, "Invalid RFC3339 fractional seconds");
        for (size_t i = 0; i < 3; ++i) st.wMilliseconds = static_cast<WORD>(st.wMilliseconds * 10 +
            (start + i < pos ? text[start + i] - '0' : 0));
    }
    int offset_minutes = 0;
    if (pos < text.size() && text[pos] == 'Z' && pos + 1 == text.size()) { /* UTC */ }
    else if (pos + 6 == text.size() && (text[pos] == '+' || text[pos] == '-') && text[pos + 3] == ':') {
        const auto hours = digits(text, pos + 1, 2), minutes = digits(text, pos + 4, 2);
        if (hours > 23 || minutes > 59) throw Error(0, -4, "Invalid RFC3339 UTC offset");
        offset_minutes = (hours * 60 + minutes) * (text[pos] == '+' ? 1 : -1);
    } else throw Error(0, -4, "RFC3339 timezone is required");
    FILETIME ft{};
    SYSTEMTIME verify{};
    if (!SystemTimeToFileTime(&st, &ft) || !FileTimeToSystemTime(&ft, &verify) ||
        verify.wYear != st.wYear || verify.wMonth != st.wMonth || verify.wDay != st.wDay ||
        verify.wHour != st.wHour || verify.wMinute != st.wMinute || verify.wSecond != st.wSecond)
        throw Error(0, -4, "Invalid RFC3339 calendar date");
    const auto ticks = (static_cast<std::int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (ticks - epoch_ticks) / 10000 - static_cast<std::int64_t>(offset_minutes) * 60000;
}
std::string format_time(std::int64_t milliseconds) {
    const auto ticks = milliseconds * 10000 + epoch_ticks;
    FILETIME ft{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) throw Error(0, -4, "Time outside supported range");
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth,
        st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}
std::int64_t integer_units(const Json& value) {
    if (!value.is_number_integer() || (value.is_number_unsigned() &&
        value.get<std::uint64_t>() > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())))
        throw Error(0, -4, "Expected a signed 64-bit integer");
    return value.get<std::int64_t>();
}
UsageRow UsageRow::parse(const Json& row) {
    if (!row.is_object()) throw Error(0, -4, "Usage row must be an object");
    UsageRow result;
    result.created_at = required_string(row, "createdAt");
    result.created_ms = parse_time(result.created_at);
    result.model = nullable_string(row, "finalModel");
    if (!result.model || result.model->empty()) result.model = nullable_string(row, "requestedModel");
    result.completed_at = nullable_string(row, "completedAt");
    result.input_tokens = nullable_integer(row, "inputTokens");
    result.output_tokens = nullable_integer(row, "outputTokens");
    result.http_status = nullable_integer(row, "httpStatus");
    return result;
}
void validate_usage_query(const Json& query) {
    if (!query.is_object()) throw Error(0, -1, "Query must be an object");
    if (query.contains("limit")) {
        const auto limit = integer_units(query["limit"]);
        if (limit < 1 || limit > 200) throw Error(0, -1, "Usage log limit must be 1..200");
    }
    for (const auto* field : {"start", "end"}) {
        if (query.contains(field)) parse_time(required_string(query, field));
    }
    if (query.contains("start") && query.contains("end") &&
        parse_time(query["start"]) > parse_time(query["end"])) throw Error(0, -1, "start must not follow end");
}
void validate_usage_page(const Json& result) {
    if (!result.is_object() || !result.contains("page") || !result["page"].is_object() ||
        !result["page"].contains("items") || !result["page"]["items"].is_array())
        throw Error(0, -4, "Usage logs must contain page.items");
    for (const auto& row : result["page"]["items"]) UsageRow::parse(row);
    const auto& page = result["page"];
    if (!page.contains("nextCursor") || (!page["nextCursor"].is_null() && !page["nextCursor"].is_string()))
        throw Error(0, -4, "Usage page requires a nullable nextCursor");
}
Json Client::usage_logs(const Json& query) {
    validate_usage_query(query);
    auto result = request("GET", "/api/account/usage/logs", query);
    validate_usage_page(result);
    return result;
}
Json Client::snapshot(std::int64_t now) {
    std::optional<UsageRow> latest;
    std::int64_t input = 0, output = 0;
    auto scan = [&](std::int64_t start, bool count_tokens) {
        Json query{{"start", format_time(start)}, {"end", format_time(now)}, {"limit", 200}};
        std::set<std::string> cursors, row_ids;
        for (size_t pages = 0; pages < 10000; ++pages) {
            const auto response = usage_logs(query);
            for (const auto& value : response["page"]["items"]) {
                const auto row = UsageRow::parse(value);
                if (row.created_ms < start || row.created_ms > now) continue;
                if (value.contains("id") && value["id"].is_string() && !row_ids.insert(value["id"]).second) continue;
                if (!latest || row.created_ms > latest->created_ms) latest = row;
                if (count_tokens) { add_tokens(input, row.input_tokens); add_tokens(output, row.output_tokens); }
            }
            const auto& cursor = response["page"]["nextCursor"];
            if (cursor.is_null()) return;
            const auto next = cursor.get<std::string>();
            if (next.empty() || !cursors.insert(next).second) throw Error(0, -4, "Repeated usage cursor");
            query["cursor"] = next;
        }
        throw Error(0, -4, "Usage pagination exceeded 10000 pages");
    };
    scan(now - 300000, true);
    if (!latest) scan(now - 30LL * 86400000, false);
    const auto summary = request("GET", "/api/account/usage/summary");
    const auto wallet = request("GET", "/api/account/wallet");
    const auto cost = integer_units(summary.at("today").at("costUnits"));
    const auto balance = integer_units(wallet.at("balanceUnits"));
    Json result{{"nowActiveModel", nullptr}, {"lastModel", nullptr}, {"lastActiveAt", nullptr},
        {"inputTokPerSec", static_cast<double>(input) / 300.0}, {"outputTokPerSec", static_cast<double>(output) / 300.0},
        {"todayCostUsd", static_cast<double>(cost) / 1e10}, {"balanceUsd", static_cast<double>(balance) / 1e10}};
    if (latest) {
        result["lastModel"] = latest->model ? Json(*latest->model) : Json(nullptr);
        result["lastActiveAt"] = latest->created_at;
        if (now - latest->created_ms <= 120000) result["nowActiveModel"] = result["lastModel"];
    }
    return result;
}
}
