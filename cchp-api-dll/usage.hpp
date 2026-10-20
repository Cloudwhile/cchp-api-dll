#pragma once
#include "core.hpp"
#include <optional>

namespace cchp {
std::int64_t utc_now_ms();
std::int64_t parse_time(const std::string& text);
std::string format_time(std::int64_t milliseconds);
std::int64_t integer_units(const Json& value);
struct UsageRow {
    std::string started_at;
    std::int64_t started_ms = 0;
    std::optional<std::string> model, completed_at;
    std::optional<std::int64_t> input_tokens, output_tokens, http_status;
    static UsageRow parse(const Json& row);
};
void validate_usage_query(const Json& query);
void validate_usage_page(const Json& page);
}
