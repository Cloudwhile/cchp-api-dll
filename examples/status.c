#include "cchp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The snapshot has fixed scalar keys. A small extractor keeps this C example
 * independent of a JSON runtime. Use a full parser for arbitrary API responses. */
static const char* field(const char* json, const char* name) {
    char key[80];
    const char* result;
    snprintf(key, sizeof(key), "\"%s\":", name);
    result = strstr(json, key);
    if (!result) return "null";
    result += strlen(key);
    while (*result == ' ' || *result == '\t') ++result;
    return result;
}
static void string_value(const char* json, const char* name, char* output, size_t capacity) {
    const char* value = field(json, name);
    size_t used = 0;
    if (*value != '"') { snprintf(output, capacity, "-"); return; }
    ++value;
    while (*value && *value != '"' && used + 1 < capacity) {
        if (*value == '\\') {
            ++value;
            if (!*value) break;
            if (*value == 'n' || *value == 'r' || *value == 't') { output[used++] = ' '; ++value; continue; }
        }
        output[used++] = *value++;
    }
    output[used] = '\0';
}
int run_status_example(const char* config) {
    cchp_client client = cchp_client_create(config);
    char* snapshot;
    char active[256], last[256], timestamp[80];
    if (!client) { fprintf(stderr, "%s\n", cchp_last_error()); return 1; }
    snapshot = cchp_status_snapshot(client);
    if (!snapshot) {
        fprintf(stderr, "%s\n", cchp_last_error()); cchp_client_destroy(client); return 1;
    }
    string_value(snapshot, "nowActiveModel", active, sizeof(active));
    string_value(snapshot, "lastModel", last, sizeof(last));
    string_value(snapshot, "lastActiveAt", timestamp, sizeof(timestamp));
    printf("Model: %s, last: %s@%s, ↑ %.1f tok/s ↓ %.1f tok/s, balance: $%.2f\\$%.2f\n",
        active, last, timestamp, strtod(field(snapshot, "inputTokPerSec"), NULL),
        strtod(field(snapshot, "outputTokPerSec"), NULL), strtod(field(snapshot, "todayCostUsd"), NULL),
        strtod(field(snapshot, "balanceUsd"), NULL));
    cchp_string_free(snapshot);
    cchp_client_destroy(client);
    return 0;
}
#ifndef CCHP_EXAMPLE_NO_MAIN
int main(int argc, char** argv) {
    char config[8192];
    FILE* input = NULL;
    size_t bytes;
    if (argc != 2) { fprintf(stderr, "Usage: cchp_status <UTF-8-config.json>\n"); return 2; }
    if (fopen_s(&input, argv[1], "rb") || !input) { fprintf(stderr, "Cannot open config file\n"); return 2; }
    bytes = fread(config, 1, sizeof(config) - 1, input);
    if (ferror(input) || !feof(input)) { fclose(input); fprintf(stderr, "Config file too large or unreadable\n"); return 2; }
    fclose(input);
    config[bytes] = '\0';
    return run_status_example(config);
}
#endif
