#pragma once
// Встроенные функции Luma — общие для
// обоих бэкендов: tree-walking интерпретатор и VM регистрируют их в начальном
// окружении через registerBuiltins.
//
// Analyzer получает имена и арности из builtinDefinitions(); реализация
// каждого builtin также проверяет арность для динамических вызовов.

#include <cmath>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <array>
#include <optional>

#include "error.h"
#include "interpreter.h"  // Environment
#include "runtime.h"
#include "util.h"
#include "value.h"

// Поток ввода для input(): по умолчанию std::cin. Тесты подменяют указатель
// на istringstream (шов для тестируемости; интерпретатор и VM не меняются).
inline std::istream*& inputStream() {
    static std::istream* stream = &std::cin;
    return stream;
}

inline Value nativePrint(std::ostream& out, std::vector<Value> args, int line) {
    (void)line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out << ' ';
        out << valueToString(args[i]);
    }
    out << '\n';
    return Value{};  // print возвращает nil
}

inline Value nativeLen(std::ostream& out, std::vector<Value> args, int line) {
    (void)out;
    requireArity("len", args, 1, line);
    if (auto* string = std::get_if<std::string>(&args[0]))
        return static_cast<double>(string->size());  // байты UTF-8 (DESIGN.md 4.4)
    if (auto* array = std::get_if<std::shared_ptr<Array>>(&args[0])) {
        if (!*array) return 0.0;
        return static_cast<double>((*array)->elements.size());
    }
    throw LumaError(line, "cannot take length of " + valueTypeToString(args[0]));
}

inline Value nativePush(std::ostream& out, std::vector<Value> args, int line) {
    (void)out;
    requireArity("push", args, 2, line);
    auto* array = std::get_if<std::shared_ptr<Array>>(&args[0]);
    if (!array || !*array)
        throw LumaError(line, "cannot push to " + valueTypeToString(args[0]));
    (*array)->elements.push_back(std::move(args[1]));
    return std::move(args[0]);  // возвращает тот же массив (DESIGN.md 5)
}

inline Value nativeStr(std::ostream& out, std::vector<Value> args, int line) {
    (void)out;
    requireArity("str", args, 1, line);
    return valueToString(args[0]);
}

inline Value nativeNum(std::ostream& out, std::vector<Value> args, int line) {
    (void)out;
    requireArity("num", args, 1, line);
    auto* string = std::get_if<std::string>(&args[0]);
    if (!string)
        throw LumaError(line, "num() expects a string, got " + valueTypeToString(args[0]));
    // Строгий разбор: вся строка должна быть числом ("12 " или "1x" — ошибки).
    char* end = nullptr;
    double value = std::strtod(string->c_str(), &end);
    if (end == string->c_str() || end != string->c_str() + string->size())
        throw LumaError(line, "cannot convert '" + *string + "' to a number");
    return value;
}

// input(): одна строка из стандартного ввода ЦЕЛИКОМ (включая пробелы),
// без завершающего перевода строки. Результат — string, БЕЗ преобразования
// типов ("15" остаётся строкой; число — явно через num(input())).
// При EOF (читать нечего) — nil: `input() or "default"` работает благодаря
// truthiness. Вызов с аргументами — ошибка арности.
inline Value nativeInput(std::ostream& out, std::vector<Value> args, int line) {
    (void)out;
    requireArity("input", args, 0, line);
    std::string text;
    if (!std::getline(*inputStream(), text)) return Value{};  // EOF → nil
    return text;
}

inline Value nativeAssert(std::ostream&, std::vector<Value> args, int line) {
    requireArity("assert", args, 2, line);
    auto message = std::get_if<std::string>(&args[1]);
    if (!message) throw LumaError(line, "assert() message must be a string");
    if (!isTruthy(args[0])) throw LumaError(line, *message);
    return Value{};
}

inline Value nativeType(std::ostream&, std::vector<Value> args, int line) {
    requireArity("type", args, 1, line);
    return valueTypeToString(args[0]);
}

inline double finiteNativeNumber(const Value& value, int line, const char* what) {
    double number = requireNumber(value, line, what);
    if (!std::isfinite(number))
        throw LumaError(line, std::string(what) + " must be finite");
    return number;
}

inline Value nativeSqrt(std::ostream&, std::vector<Value> args, int line) {
    requireArity("_luma_sqrt", args, 1, line);
    double value = finiteNativeNumber(args[0], line, "sqrt argument");
    if (value < 0) throw LumaError(line, "sqrt argument must be nonnegative");
    return std::sqrt(value);
}

inline Value nativePow(std::ostream&, std::vector<Value> args, int line) {
    requireArity("_luma_pow", args, 2, line);
    double base = finiteNativeNumber(args[0], line, "pow base");
    double exponent = finiteNativeNumber(args[1], line, "pow exponent");
    double result = std::pow(base, exponent);
    if (!std::isfinite(result))
        throw LumaError(line, "pow result is outside the finite real-number domain");
    return result;
}

inline Value nativeSlice(std::ostream&, std::vector<Value> args, int line) {
    requireArity("_luma_slice", args, 3, line);
    auto text = std::get_if<std::string>(&args[0]);
    if (!text) throw LumaError(line, "substr expects a string");
    double start = finiteNativeNumber(args[1], line, "substr start");
    double count = finiteNativeNumber(args[2], line, "substr count");
    if (start < 0 || count < 0 || std::trunc(start) != start || std::trunc(count) != count)
        throw LumaError(line, "substr start and count must be nonnegative integers");
    if (start > static_cast<double>(text->size()) ||
        count > static_cast<double>(text->size()) - start)
        throw LumaError(line, "substr range is out of bounds");
    return text->substr(static_cast<size_t>(start), static_cast<size_t>(count));
}

// One catalogue is used by Analyzer and both runtime backends. Adding a
// builtin cannot silently leave static arity checks out of sync.
struct BuiltinDefinition {
    const char* name;
    NativeFn fn;
    std::optional<size_t> arity;
};

inline const std::array<BuiltinDefinition, 11>& builtinDefinitions() {
    static const std::array<BuiltinDefinition, 11> definitions = {{
        {"print", nativePrint, std::nullopt}, {"len", nativeLen, 1},
        {"push", nativePush, 2}, {"str", nativeStr, 1},
        {"num", nativeNum, 1}, {"input", nativeInput, 0},
        {"assert", nativeAssert, 2}, {"_luma_sqrt", nativeSqrt, 1},
        {"_luma_pow", nativePow, 2}, {"_luma_slice", nativeSlice, 3},
        {"type", nativeType, 1}
    }};
    return definitions;
}

// Объявить встроенные функции в начальном (глобальном) окружении.
// Поток вывода нативные функции получают в момент вызова, а не здесь.
inline void registerBuiltins(Environment& env) {
    auto define = [&](const char* name, NativeFn fn) {
        auto function = std::make_shared<NativeFunction>();
        function->name = name;
        function->fn = fn;
        env.define(name, Value(std::move(function)), 0);
    };
    for (const BuiltinDefinition& builtin : builtinDefinitions())
        define(builtin.name, builtin.fn);
}
