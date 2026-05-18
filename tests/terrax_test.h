#pragma once
// Minimal test framework for Terrax.
// Usage: define TEST_CASE blocks in any file, call run_all_tests() from main.
// Expand to new modules by adding new test .cpp files to the test build target.

#include <iostream>
#include <string>
#include <functional>
#include <vector>
#include <stdexcept>

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& allTests() {
    static std::vector<TestCase> v;
    return v;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        allTests().push_back({name, fn});
    }
};

#define TEST_CASE(name) \
    static void test_fn_##name(); \
    static TestRegistrar reg_##name(#name, test_fn_##name); \
    static void test_fn_##name()

#define CHECK(expr) \
    do { if (!(expr)) throw std::runtime_error("CHECK(" #expr ") failed at line " + std::to_string(__LINE__)); } while(0)

#define CHECK_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error( \
        std::string("CHECK_EQ failed at line ") + std::to_string(__LINE__) + \
        ": " + std::to_string(a) + " != " + std::to_string(b)); } while(0)

#define CHECK_NE(a, b) \
    do { if ((a) == (b)) throw std::runtime_error( \
        std::string("CHECK_NE failed at line ") + std::to_string(__LINE__)); } while(0)

inline int run_all_tests() {
    int passed = 0, failed = 0;
    for (auto& t : allTests()) {
        try {
            t.fn();
            std::cout << "[PASS] " << t.name << "\n";
            passed++;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
            failed++;
        }
    }
    std::cout << "\n" << passed << "/" << (passed + failed) << " tests passed\n";
    return failed;
}
