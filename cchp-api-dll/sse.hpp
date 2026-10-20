#pragma once
#include "http.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>

namespace cchp {
using EventCallback = void (__stdcall *)(const char*, void*);
bool in_callback() noexcept;
class SseParser {
    std::string line_, data_, type_, id_;
    bool skip_lf_ = false, first_ = true;
    bool has_data_ = false;
    std::function<void(const Json&)> emit_;
    void line();
public:
    std::string last_id;
    int retry_ms = 500;
    explicit SseParser(std::function<void(const Json&)> emit, std::string id = {})
        : id_(id), emit_(std::move(emit)), last_id(std::move(id)) {}
    void feed(const char* bytes, size_t size);
};
class Subscription {
    Client& client_;
    std::string path_, types_;
    EventCallback callback_;
    void* user_;
    Cancellation cancellation_;
    std::atomic<bool> stopping_{false};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    void emit(const Json& event);
    void run() noexcept;
public:
    Subscription(Client& client, std::string path, std::string types, EventCallback callback, void* user);
    ~Subscription();
    void start();
    void cancel();
    void join();
};
}
