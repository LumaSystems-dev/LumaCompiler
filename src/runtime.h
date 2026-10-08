#pragma once
// Семантика операций Luma — общая для ОБОИХ бэкендов (tree-walking
// интерпретатора и стековой VM). Один источник правил = идентичные сообщения
// об ошибках и результаты на одинаковых программах.

#include <cmath>
#include <cstdlib>
#include <string>

#include "error.h"
#include "token.h"
#include "util.h"
#include "value.h"

// Число обязано быть числом: для границ и шага for-цикла, индексов массивов.
inline double requireNumber(const Value& value, int line, const char* what) {
    const double* number = std::get_if<double>(&value);
    if (!number) {
        throw LumaError(line, std::string(what) + " must be a number, got " +
                                  valueTypeToString(value));
    }
    return *number;
}

// Ровно N аргументов у встроенной функции.
inline void requireArity(const char* name, const std::vector<Value>& args, size_t count,
                         int line) {
    if (args.size() != count) {
        throw LumaError(line, std::string("function '") + name + "' expects " +
                                  std::to_string(count) + " arguments, got " +
                                  std::to_string(args.size()));
    }
}

inline const double* asNumber(const Value& value) {
    return std::get_if<double>(&value);
}

inline const std::string* asString(const Value& value) {
    return std::get_if<std::string>(&value);
}

inline const std::shared_ptr<Array>* asArray(const Value& value) {
    return std::get_if<std::shared_ptr<Array>>(&value);
}

// % — остаток со знаком делителя: a - floor(a / b) * b (как в Lua и Python).
inline double modulo(double a, double b) {
    return a - std::floor(a / b) * b;
}

inline std::string arithmeticVerb(TokenKind op) {
    switch (op) {
        case TokenKind::PLUS:    return "add";
        case TokenKind::MINUS:   return "subtract";
        case TokenKind::STAR:    return "multiply";
        case TokenKind::SLASH:   return "divide";
        case TokenKind::PERCENT: return "modulo";
        default:                 return "operate on";
    }
}

// Арифметика: только number × number (DESIGN.md 4.1).
inline Value arithmetic(TokenKind op, int line, const Value& left, const Value& right) {
    const double* l = asNumber(left);
    const double* r = asNumber(right);
    if (!l || !r) {
        throw LumaError(line, "cannot " + arithmeticVerb(op) + " " +
                                  valueTypeToString(left) + " and " +
                                  valueTypeToString(right));
    }
    switch (op) {
        case TokenKind::PLUS:    return *l + *r;
        case TokenKind::MINUS:   return *l - *r;
        case TokenKind::STAR:    return *l * *r;
        case TokenKind::SLASH:   return *l / *r;  // деление на 0 → inf/nan (IEEE 754)
        case TokenKind::PERCENT: return modulo(*l, *r);
        default: break;
    }
    throw LumaError(line, "unsupported arithmetic operator");  // недостижимо
}

// Порядковые сравнения: number×number и string×string, смешение типов — ошибка.
inline Value ordered(TokenKind op, int line, const Value& left, const Value& right) {
    if (const double* l = asNumber(left)) {
        if (const double* r = asNumber(right)) {
            switch (op) {
                case TokenKind::LESS:          return *l < *r;
                case TokenKind::LESS_EQUAL:    return *l <= *r;
                case TokenKind::GREATER:       return *l > *r;
                case TokenKind::GREATER_EQUAL: return *l >= *r;
                default: break;
            }
        }
    } else if (const std::string* l = asString(left)) {
        if (const std::string* r = asString(right)) {
            switch (op) {
                case TokenKind::LESS:          return *l < *r;
                case TokenKind::LESS_EQUAL:    return *l <= *r;
                case TokenKind::GREATER:       return *l > *r;
                case TokenKind::GREATER_EQUAL: return *l >= *r;
                default: break;
            }
        }
    }
    throw LumaError(line, "cannot compare " + valueTypeToString(left) + " and " +
                              valueTypeToString(right));
}

// Конкатенация "..": string/number в любом порядке; числа приводятся к тексту.
inline Value concatenate(int line, const Value& left, const Value& right) {
    auto isConcatable = [](const Value& value) {
        return std::holds_alternative<double>(value) ||
               std::holds_alternative<std::string>(value);
    };
    if (!isConcatable(left) || !isConcatable(right)) {
        throw LumaError(line, "cannot concatenate " + valueTypeToString(left) +
                                  " and " + valueTypeToString(right));
    }
    return valueToString(left) + valueToString(right);
}
