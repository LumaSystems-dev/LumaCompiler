#include <cmath>
#include <string>

#include "../src/ast.h"
#include "../src/error.h"
#include "../src/interpreter.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "test.h"

namespace {

// Исходник -> вычисленное значение выражения.
Value evalSource(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Expr ast = parser.parseSingleExpression();
    Interpreter interpreter;
    return interpreter.eval(ast);
}

// Извлекатели значений с проверкой типа (Value — тип-значение, без висячих
// указателей: временные объекты безопасны).
double number(const Value& value) {
    const double* result = std::get_if<double>(&value);
    CHECK(result != nullptr);
    return *result;
}

bool boolean(const Value& value) {
    const bool* result = std::get_if<bool>(&value);
    CHECK(result != nullptr);
    return *result;
}

std::string str(const Value& value) {
    const std::string* result = std::get_if<std::string>(&value);
    CHECK(result != nullptr);
    return *result;
}

bool isNil(const Value& value) {
    return std::holds_alternative<std::monostate>(value);
}

}  // namespace

TEST(eval_arithmetic) {
    CHECK_EQ(number(evalSource("1 + 2")), 3.0);
    CHECK_EQ(number(evalSource("2 + 3 * 4")), 14.0);
    CHECK_EQ(number(evalSource("(2 + 3) * 4")), 20.0);
    CHECK_EQ(number(evalSource("10 - 4 - 3")), 3.0);
    CHECK_EQ(number(evalSource("7 / 2")), 3.5);
    CHECK_EQ(number(evalSource("-5 + 3")), -2.0);
    CHECK_EQ(number(evalSource("-(2 + 3)")), -5.0);
}

TEST(eval_modulo_sign_follows_divisor) {
    CHECK_EQ(number(evalSource("7 % 3")), 1.0);
    CHECK_EQ(number(evalSource("-7 % 3")), 2.0);   // знак делителя (Lua/Python)
    CHECK_EQ(number(evalSource("7 % -3")), -2.0);
    CHECK_EQ(number(evalSource("2 * 3 + 4 % 3")), 7.0);
}

TEST(eval_division_by_zero_is_inf) {
    double value = number(evalSource("1 / 0"));
    CHECK(std::isinf(value));
    CHECK(value > 0);
}

TEST(eval_modulo_by_zero_is_nan) {
    CHECK(std::isnan(number(evalSource("1 % 0"))));
}

TEST(eval_comparisons) {
    CHECK(boolean(evalSource("1 < 2")));
    CHECK(!boolean(evalSource("2 < 1")));
    CHECK(boolean(evalSource("2 <= 2")));
    CHECK(boolean(evalSource("3 > 2")));
    CHECK(!boolean(evalSource("2 >= 3")));
    CHECK(boolean(evalSource("1 == 1")));
    CHECK(!boolean(evalSource("1 != 1")));
    CHECK(boolean(evalSource("1 == 1.0")));
}

TEST(eval_string_comparisons) {
    CHECK(boolean(evalSource("\"a\" < \"b\"")));
    CHECK(boolean(evalSource("\"abc\" < \"abd\"")));
    CHECK(boolean(evalSource("\"a\" == \"a\"")));
    CHECK(!boolean(evalSource("\"a\" > \"b\"")));
    CHECK(!boolean(evalSource("\"a\" != \"a\"")));
}

TEST(eval_equality_across_types_is_false) {
    CHECK(!boolean(evalSource("1 == \"1\"")));
    CHECK(!boolean(evalSource("nil == false")));
    CHECK(!boolean(evalSource("0 == false")));   // разные типы всегда неравны
    CHECK(boolean(evalSource("nil == nil")));
    CHECK(boolean(evalSource("\"a\" != nil")));
}

TEST(eval_concat) {
    CHECK_EQ(str(evalSource("\"a\" .. \"b\"")), "ab");
    CHECK_EQ(str(evalSource("\"n=\" .. 5")), "n=5");
    CHECK_EQ(str(evalSource("1 .. 2")), "12");
    CHECK_EQ(str(evalSource("1.5 .. \"x\"")), "1.5x");
    CHECK_EQ(str(evalSource("-2 .. \"\"")), "-2");
}

TEST(eval_and_or_return_operand_values) {
    CHECK_EQ(number(evalSource("1 and 2")), 2.0);        // левый истинный → правый
    CHECK(isNil(evalSource("nil and 2")));               // левый ложный → левый
    CHECK_EQ(number(evalSource("false or 3")), 3.0);     // левый ложный → правый
    CHECK_EQ(number(evalSource("1 or 2")), 1.0);         // левый истинный → левый
    CHECK_EQ(str(evalSource("nil or \"x\"")), "x");
}

TEST(eval_not_and_truthiness) {
    CHECK(!boolean(evalSource("not true")));
    CHECK(boolean(evalSource("not nil")));
    CHECK(boolean(evalSource("not false")));
    CHECK(!boolean(evalSource("not 0")));      // 0 истинно (семантика Lua)!
    CHECK(!boolean(evalSource("not \"\"")));   // "" истинно!
}

TEST(eval_truthiness_lua_style) {
    CHECK_EQ(number(evalSource("0 or 5")), 0.0);   // 0 истинный → or вернул его
    CHECK_EQ(number(evalSource("\"\" and 1")), 1.0);
}

TEST(eval_short_circuit_skips_right_side) {
    // Если бы правая часть вычислялась, 1/0 дал бы inf, но главное —
    // короткое замыкание обязано её пропустить.
    CHECK(!boolean(evalSource("false and 1 / 0")));
    CHECK(boolean(evalSource("true or 1 / 0")));
    // Здесь правая часть вычисляется: true and (1/0) → inf, без ошибки.
    CHECK(std::isinf(number(evalSource("true and 1 / 0"))));
}

TEST(eval_precedence_chain) {
    CHECK_EQ(str(evalSource("1 + 2 .. \"!\"")), "3!");   // ".." слабее "+"
    CHECK(boolean(evalSource("not 1 == 2")));            // not (1 == 2)
    CHECK(boolean(evalSource("1 < 2 and 2 < 3")));
    CHECK(boolean(evalSource("1 + 2 == 3 and \"a\" .. \"b\" == \"ab\"")));
    CHECK_EQ(number(evalSource("2 * 3 + 4 % 3")), 7.0);
    CHECK_EQ(number(evalSource("-2 * 3")), -6.0);
}

TEST(eval_type_errors) {
    CHECK_THROWS(evalSource("1 + \"a\""));
    CHECK_THROWS(evalSource("\"a\" - 1"));
    CHECK_THROWS(evalSource("-\"x\""));
    CHECK_THROWS(evalSource("1 < \"a\""));
    CHECK_THROWS(evalSource("\"a\" < 1"));
    CHECK_THROWS(evalSource("true .. 1"));
    CHECK_THROWS(evalSource("nil .. \"\""));
    CHECK_THROWS(evalSource("x"));   // переменных ещё нет (Этап 4)
}

TEST(eval_error_message_format) {
    bool threw = false;
    try {
        (void)evalSource("1 + \"a\"");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()), std::string("cannot add number and string"));
    }
    CHECK(threw);

    threw = false;
    try {
        (void)evalSource("1 < \"a\"");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()), std::string("cannot compare number and string"));
    }
    CHECK(threw);
}
