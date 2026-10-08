#include <sstream>
#include <string>

#include "../src/analyzer.h"
#include "../src/ast.h"
#include "../src/error.h"
#include "../src/interpreter.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "test.h"

namespace {

// Полный фронтенд: разбор + семантический анализ (без исполнения).
void analyzeSource(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Analyzer analyzer;
    analyzer.analyze(program);
}

// luma run целиком: разбор + анализ + исполнение. Вывод — в out.
void runProgram(const std::string& source, std::ostringstream& out) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Analyzer analyzer;
    analyzer.analyze(program);
    Interpreter interpreter(out);
    interpreter.run(program);
}

}  // namespace

TEST(analyzer_accepts_valid_program) {
    analyzeSource("let x = 10\n"
                  "let y = x + 5\n"
                  "print(y)\n"
                  "if x < y then print(\"мало\") else print(\"много\") end\n"
                  "for i in 0..3 do print(i) end\n"
                  "while y > 0 do y = y - 1 end\n"
                  "let a = [x, y]\npush(a, len(a))\nprint(str(a), num(\"2\"))\n"
                  "function fact(n)\n"
                  "  if n <= 1 then return 1 end\n"
                  "  return n * fact(n - 1)\n"
                  "end\n"
                  "print(fact(5))");
}

TEST(analyzer_unknown_name_has_column) {
    bool caught = false;
    try {
        analyzeSource("let good = 1\n    print(unknown)");
    } catch (const LumaError& error) {
        caught = true;
        CHECK_EQ(error.line, 2);
        CHECK_EQ(error.column, 11);
    }
    CHECK(caught);
}

// --- Неизвестные переменные ---

TEST(unknown_variable_read) {
    // Эталонный пример из постановки этапа
    bool threw = false;
    try {
        (void)analyzeSource("print(score)");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()), std::string("Unknown variable 'score'"));
        CHECK_EQ(e.line, 1);
    }
    CHECK(threw);
}

TEST(unknown_variable_in_expression_and_assignment) {
    CHECK_THROWS(analyzeSource("let x = 1\nprint(x + missing)"));
    CHECK_THROWS(analyzeSource("missing = 5"));
    CHECK_THROWS(analyzeSource("let a = [1]\nprint(b[0])"));    // объект-индекс
    CHECK_THROWS(analyzeSource("let a = [1]\nb[0] = 2"));       // цель-индекс
    CHECK_THROWS(analyzeSource("let x = noSuch\nprint(x)"));
}

TEST(unknown_variable_line_reported) {
    bool threw = false;
    try {
        (void)analyzeSource("let x = 1\nprint(x)\nprint(y)\nprint(x)");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(e.line, 3);
    }
    CHECK(threw);
}

// --- Объявления и области ---

TEST(redeclaration_caught_shadowing_allowed) {
    CHECK_THROWS(analyzeSource("let x = 1\nlet x = 2"));
    CHECK_THROWS(analyzeSource("function f()\nend\nfunction f()\nend"));
    // теневание во вложенной области — корректно
    analyzeSource("let x = 1\nif true then let x = 2 end\nlet y = 3");
    // let в разных ветвях — разные области
    analyzeSource("if true then let v = 1 else let v = 2 end");
}

// --- Статическая проверка арности ---

TEST(static_arity_user_function) {
    CHECK_THROWS(analyzeSource("function add(a, b)\nreturn a + b\nend\nadd(1)"));
    CHECK_THROWS(analyzeSource("function add(a, b)\nreturn a + b\nend\nadd(1, 2, 3)"));

    bool threw = false;
    try {
        (void)analyzeSource("function add(a, b)\nreturn a + b\nend\nadd(1)");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("function 'add' expects 2 arguments, got 1"));
        CHECK_EQ(e.line, 4);
    }
    CHECK(threw);
}

TEST(static_arity_builtins) {
    CHECK_THROWS(analyzeSource("len()"));
    CHECK_THROWS(analyzeSource("len(\"a\", \"b\")"));
    CHECK_THROWS(analyzeSource("push([1])"));
    CHECK_THROWS(analyzeSource("num()"));
    CHECK_THROWS(analyzeSource("str(1, 2)"));
    // print вариадичен — любые арности корректны
    analyzeSource("print()\nprint(1)\nprint(1, 2, 3, 4)");
}

TEST(call_before_declaration_is_unknown) {
    // Как в рантайме: имя появляется в момент выполнения function-стейтмента
    CHECK_THROWS(analyzeSource("add(1, 2)\nfunction add(a, b)\nreturn a + b\nend"));
}

TEST(computed_callee_skips_static_arity) {
    // Вызов не по имени — статическая арность неприменима (поймает рантайм)
    analyzeSource("let f = print\nf(1, 2, 3)");
    analyzeSource("let a = [len]\nprint(a)");
    // вызов по имени переменной — не функция, статической проверки нет
    analyzeSource("let x = 5\nprint(x)");
}

TEST(recursion_and_nested_functions_analyze_ok) {
    analyzeSource("function makeCounter()\n"
                  "  let count = 0\n"
                  "  function increment()\n"
                  "    count = count + 1\n"
                  "    return count\n"
                  "  end\n"
                  "  return increment\n"
                  "end\n"
                  "let next = makeCounter()\nprint(next())");
}

// --- Контексты return / break / continue ---

TEST(return_context_checks) {
    CHECK_THROWS(analyzeSource("return 1"));
    CHECK_THROWS(analyzeSource("if true then return end"));
    CHECK_THROWS(analyzeSource("for i in 0..3 do return end"));
    // корректные случаи
    analyzeSource("function f()\nreturn 1\nend");
    analyzeSource("function f()\nfor i in 0..3 do if i == 1 then return i end end\nreturn 0\nend");
}

TEST(break_continue_context_checks) {
    CHECK_THROWS(analyzeSource("break"));
    CHECK_THROWS(analyzeSource("continue"));
    CHECK_THROWS(analyzeSource("if true then break end"));
    CHECK_THROWS(analyzeSource("while true do function f() continue end end"));

    bool threw = false;
    try {
        (void)analyzeSource("while true do\nfunction f()\nbreak\nend\nend");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("break is only allowed inside a loop"));
        CHECK_EQ(e.line, 3);
    }
    CHECK(threw);

    // корректные случаи
    analyzeSource("while true do break end");
    analyzeSource("function f()\nwhile true do break end\nreturn 1\nend");
}

// --- Анализ происходит ДО исполнения ---

TEST(analysis_happens_before_execution) {
    // Ни один print не выполнится: анализ падает раньше интерпретации
    std::ostringstream out;
    bool threw = false;
    try {
        runProgram("print(1)\nprint(missing)\nprint(2)", out);
    } catch (const LumaError&) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(out.str(), std::string(""));
}

TEST(error_inside_uncalled_function_caught) {
    // Функция никогда не вызывается, но неизвестная переменная в её теле
    // ловится уже при анализе
    CHECK_THROWS(analyzeSource("print(\"start\")\n"
                               "function f()\n"
                               "  return noSuchVariable\n"
                               "end\n"
                               "print(\"end\")"));
}
