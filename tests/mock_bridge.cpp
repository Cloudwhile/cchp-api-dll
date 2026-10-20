#include "mock_server.hpp"
#include <cstdlib>

extern "C" int run_abi_tests(const char* config);
extern "C" int run_status_example(const char* config);
int main(int argc, char** argv) {
    MockServer server;
    const auto config = cchp::Json{{"base_url", server.url()},
        {"auth", {{"type", "bearer"}, {"token", "test-only"}}}}.dump();
    if (argc > 1 && std::string(argv[1]) == "example") return run_status_example(config.c_str());
    return run_abi_tests(config.c_str());
}
