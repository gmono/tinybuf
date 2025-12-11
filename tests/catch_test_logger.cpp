#include "tinybuf_log.h"
#include <catch2/catch_all.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>

static std::string g_current_test_name;
static std::atomic<int> g_timeout_ms{5000};
static std::atomic<int> g_in_test{0};
static std::atomic<long long> g_start_ms{0};
static std::atomic<int> g_watchdog_started{0};
static void _ensure_watchdog(){
    if(g_watchdog_started.exchange(1)==0){
        const char* p = std::getenv("TINYBUF_TEST_TIMEOUT_MS");
        if(p){ int v = atoi(p); if(v>0) g_timeout_ms.store(v); }
        std::thread([](){
            while(true){
                if(g_in_test.load()){ auto now = std::chrono::steady_clock::now(); long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(); long long st = g_start_ms.load(); int to = g_timeout_ms.load(); if(st>0 && to>0 && ms - st > to){ LOGE("TEST TIMEOUT: %s exceeded %d ms", g_current_test_name.c_str(), to); std::_Exit(3); } }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }).detach();
    }
}

struct TestLogger : Catch::EventListenerBase {
    using Catch::EventListenerBase::EventListenerBase;
    void testCaseStarting(Catch::TestCaseInfo const& info) override { _ensure_watchdog(); g_current_test_name = info.name; g_in_test.store(1); auto now = std::chrono::steady_clock::now(); g_start_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()); LOGI("TEST ENTER: %s", info.name.c_str()); }
    void testCaseEnded(Catch::TestCaseStats const&) override { g_in_test.store(0); g_start_ms.store(0); LOGI("TEST EXIT: %s", g_current_test_name.c_str()); }
};

CATCH_REGISTER_LISTENER(TestLogger);
