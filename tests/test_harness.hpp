#pragma once

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace test {

struct Registry {
    struct Case {
        const char* name;
        void (*fn)();
    };
    static Registry& instance() {
        static Registry r;
        return r;
    }
    void add(const char* name, void (*fn)()) { cases.push_back({name, fn}); }
    std::vector<Case> cases;
    int failures = 0;
};

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry::instance().add(name, fn); }
};

inline void fail(const char* file, int line, const std::string& message) {
    ++Registry::instance().failures;
    std::cerr << "FAIL " << file << ":" << line << " " << message << "\n";
}

inline bool near(float a, float b, float eps = 1.0e-5f) {
    return std::fabs(a - b) <= eps;
}

inline int run_all() {
    auto& reg = Registry::instance();
    std::cout << "Running " << reg.cases.size() << " tests...\n";
    for (const auto& c : reg.cases) {
        std::cout << "  - " << c.name << "\n";
        const int before = reg.failures;
        c.fn();
        if (reg.failures == before) {
            std::cout << "    ok\n";
        }
    }
    if (reg.failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cerr << reg.failures << " failure(s)\n";
    return 1;
}

} // namespace test

#define TEST(name)                                                                 \
    static void test_##name();                                                     \
    static test::Registrar registrar_##name(#name, &test_##name);                  \
    static void test_##name()

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            test::fail(__FILE__, __LINE__, "CHECK(" #cond ")");                    \
        }                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                             \
    do {                                                                           \
        const auto _va = (a);                                                      \
        const auto _vb = (b);                                                      \
        if (!(_va == _vb)) {                                                       \
            test::fail(__FILE__, __LINE__,                                         \
                       std::string("CHECK_EQ(" #a ", " #b ") got ") +              \
                           std::to_string(_va) + " vs " + std::to_string(_vb));    \
        }                                                                          \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                      \
    do {                                                                           \
        const float _va = static_cast<float>(a);                                   \
        const float _vb = static_cast<float>(b);                                   \
        if (!test::near(_va, _vb, static_cast<float>(eps))) {                      \
            test::fail(__FILE__, __LINE__,                                         \
                       std::string("CHECK_NEAR(" #a ", " #b ") got ") +            \
                           std::to_string(_va) + " vs " + std::to_string(_vb));    \
        }                                                                          \
    } while (0)

#define CHECK_STREQ(a, b)                                                          \
    do {                                                                           \
        const std::string _va = (a);                                               \
        const std::string _vb = (b);                                               \
        if (_va != _vb) {                                                          \
            test::fail(__FILE__, __LINE__,                                         \
                       std::string("CHECK_STREQ(" #a ", " #b ") got \"") + _va +   \
                           "\" vs \"" + _vb + "\"");                               \
        }                                                                          \
    } while (0)
