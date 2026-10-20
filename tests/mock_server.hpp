#pragma once
#include <winsock2.h>
#include "core.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <thread>

class MockServer {
    SOCKET listener_ = INVALID_SOCKET;
    std::thread acceptor_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::map<std::string, int> counts_;
    std::atomic<bool> stopping_{false};
    unsigned short port_ = 0;
    void serve(SOCKET socket);
public:
    MockServer();
    ~MockServer();
    std::string url() const;
    int count(const std::string& route);
    std::atomic<int> usage_mode{0}; // 0=recent, 1=30-day fallback, 2=empty, 3=forbidden, 4=cursor loop
    std::int64_t now = 0;
    std::atomic<int> sse_resumed{0};
};
