#include "tinybuf_log.h"
#include <catch2/catch_all.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <string>

static std::string g_current_test_name;

struct TestLogger : Catch::EventListenerBase {
    using Catch::EventListenerBase::EventListenerBase;
    void testCaseStarting(Catch::TestCaseInfo const& info) override { g_current_test_name = info.name; LOGI("TEST ENTER: %s", info.name.c_str()); }
    void testCaseEnded(Catch::TestCaseStats const&) override { LOGI("TEST EXIT: %s", g_current_test_name.c_str()); }
};

CATCH_REGISTER_LISTENER(TestLogger);
