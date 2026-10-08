#pragma once
// Микрофреймворк тестов Luma — без внешних зависимостей.
//
//   TEST(имя_теста) { CHECK(...); CHECK_EQ(a, b); CHECK_THROWS(expr); }
//
// TEST регистрирует функцию через статический объект; test_main.cpp вызывает
// runAllTests(). CHECK_EQ печатает значения — они должны уметь выводиться
// в std::ostream (числа, строки; для enum сравнивайте строки через
// tokenTypeToString и т.п.).

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/error.h"

struct TestFailure : std::runtime_error {
    explicit TestFailure(const std::string& message) : std::runtime_error(message) {}
};

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline void reportFailure(const char* file, int line, const std::string& message) {
    std::ostringstream out;
    out << file << ":" << line << "  " << message;
    throw TestFailure(out.str());
}

struct TestRegistrar {
    TestRegistrar(const std::string& name, std::function<void()> fn) {
        testRegistry().push_back({name, std::move(fn)});
    }
};

inline int runAllTests() {
    int failed = 0;
    for (const TestCase& test : testRegistry()) {
        try {
            test.fn();
            // std::flush: имя последнего теста переживает аварийное падение
            // самого теста (assert/краш) — иначе виновника не найти.
            std::cout << "[ OK ] " << test.name << std::endl;
        } catch (const TestFailure& failure) {
            failed++;
            std::cout << "[FAIL] " << test.name << "\n    " << failure.what() << "\n"
                      << std::flush;
        } catch (const std::exception& error) {
            failed++;
            std::cout << "[FAIL] " << test.name << "\n    unexpected exception: "
                      << error.what() << "\n" << std::flush;
        }
    }
    std::cout << "\n" << (testRegistry().size() - failed) << " passed, "
              << failed << " failed, " << testRegistry().size() << " total\n";
    return failed == 0 ? 0 : 1;
}

#define TEST(name)                                                              \
    static void test_##name();                                                  \
    static TestRegistrar registrar_##name(#name, &test_##name);                 \
    static void test_##name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond))                                                            \
            reportFailure(__FILE__, __LINE__, "CHECK(" #cond ") failed");       \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        auto&& lhs_ = (a);                                                      \
        auto&& rhs_ = (b);                                                      \
        if (!(lhs_ == rhs_)) {                                                  \
            std::ostringstream out_;                                            \
            out_ << "CHECK_EQ(" #a ", " #b ") failed: " << lhs_ << " != " << rhs_; \
            reportFailure(__FILE__, __LINE__, out_.str());                      \
        }                                                                       \
    } while (0)

// Проверяет, что выражение бросает LumaError (ошибку компиляции/рантайма Luma).
#define CHECK_THROWS(expr)                                                      \
    do {                                                                        \
        bool threw_ = false;                                                    \
        try {                                                                   \
            (void)(expr);                                                       \
        } catch (const LumaError&) {                                            \
            threw_ = true;                                                      \
        }                                                                       \
        if (!threw_)                                                            \
            reportFailure(__FILE__, __LINE__,                                   \
                          "CHECK_THROWS(" #expr ") did not throw LumaError");   \
    } while (0)
