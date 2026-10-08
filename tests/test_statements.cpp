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

Program parseProgramSource(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    return parser.parseProgram();
}

// Запустить программу (разбор + анализ + исполнение, как luma run)
// и вернуть собранный вывод print().
std::string runSource(const std::string& source) {
    Program program = parseProgramSource(source);

    Analyzer analyzer;
    analyzer.analyze(program);

    std::ostringstream out;
    Interpreter interpreter(out);
    interpreter.run(program);
    return out.str();
}

}  // namespace

// Главный пример этапа: let x = 10; let y = x + 5; print(y) -> 15
TEST(the_stage_example) {
    CHECK_EQ(runSource("let x = 10\nlet y = x + 5\nprint(y)"), "15\n");
}

TEST(assignment_chains) {
    CHECK_EQ(runSource("let x = 1\nx = x + 1\nx = x * 10\nprint(x)"), "20\n");
    CHECK_EQ(runSource("let a = 2\nlet b = a\nprint(a, b)"), "2 2\n");
}

TEST(print_variants) {
    CHECK_EQ(runSource("print()"), "\n");
    CHECK_EQ(runSource("print(1, \"a\", nil, true)"), "1 a nil true\n");
    CHECK_EQ(runSource("print(1.5 + 1.5)"), "3\n");
    CHECK_EQ(runSource("print(\"a\" .. 1)"), "a1\n");
    CHECK_EQ(runSource("print(print)"), "<function>\n");  // функция — значение
}

TEST(function_stored_in_variable) {
    CHECK_EQ(runSource("let p = print\np(7)"), "7\n");
}

TEST(semicolon_optional_and_empty_statements) {
    CHECK_EQ(runSource("let x = 1;\nprint(x);;\n"), "1\n");
}

TEST(sequential_prints_keep_order) {
    CHECK_EQ(runSource("print(1)\nprint(2)\nprint(3)"), "1\n2\n3\n");
}

TEST(argument_evaluation_order_and_values) {
    CHECK_EQ(runSource("print(1+2, 3*4, 5%3)"), "3 12 2\n");
}

TEST(statement_kinds_in_program) {
    Program program = parseProgramSource("let x = 1\nx = 2\nprint(x)");
    CHECK_EQ(program.statements.size(), 3u);

    const LetStmt* let = std::get_if<LetStmt>(&program.statements[0].value);
    CHECK(let != nullptr);
    CHECK_EQ(let->name.lexeme, "x");
    CHECK(let->initializer != nullptr);
    CHECK(std::holds_alternative<LiteralExpr>(let->initializer->value));

    const AssignStmt* assign = std::get_if<AssignStmt>(&program.statements[1].value);
    CHECK(assign != nullptr);
    CHECK(assign->target != nullptr);
    const IdentifierExpr* target = std::get_if<IdentifierExpr>(&assign->target->value);
    CHECK(target != nullptr);
    CHECK_EQ(target->name, "x");

    const ExprStmt* exprStmt = std::get_if<ExprStmt>(&program.statements[2].value);
    CHECK(exprStmt != nullptr);
    CHECK(exprStmt->expression != nullptr);
    CHECK(std::holds_alternative<CallExpr>(exprStmt->expression->value));
}

TEST(assignment_needs_plain_equal_not_comparison) {
    // x = 5 — присваивание
    Program ok = parseProgramSource("x = 5");
    CHECK_EQ(ok.statements.size(), 1u);
    CHECK(std::holds_alternative<AssignStmt>(ok.statements[0].value));

    // x == 5 — сравнение, как оператор не разрешено
    CHECK_THROWS(parseProgramSource("x == 5"));
}

TEST(expression_statement_must_be_call) {
    CHECK_THROWS(parseProgramSource("42"));
    CHECK_THROWS(parseProgramSource("1 + 2"));
    CHECK_THROWS(parseProgramSource("x"));
    CHECK_THROWS(parseProgramSource("let x = 1\nx + 1"));
}

TEST(let_syntax_errors) {
    CHECK_THROWS(parseProgramSource("let x"));      // нет '='
    CHECK_THROWS(parseProgramSource("let 5 = 3"));  // имя не идентификатор
    CHECK_THROWS(parseProgramSource("let = 3"));
    CHECK_THROWS(parseProgramSource("let x = "));   // нет выражения
}

TEST(call_syntax_errors) {
    CHECK_THROWS(parseProgramSource("print(1,,2)"));
    CHECK_THROWS(parseProgramSource("print(1"));
    CHECK_THROWS(parseProgramSource("print)"));
    CHECK_THROWS(parseProgramSource("print(1,)"));  // висячая запятая
}

TEST(runtime_unknown_variable) {
    CHECK_THROWS(runSource("print(y)"));
    CHECK_THROWS(runSource("x = 1"));  // присваивание без объявления
    CHECK_THROWS(runSource("print(1)\nprint(noSuch)"));
}

TEST(runtime_redefinition_forbidden) {
    CHECK_THROWS(runSource("let x = 1\nlet x = 2"));
}

TEST(runtime_call_errors) {
    CHECK_THROWS(runSource("foo(1)"));         // нет переменной foo
    CHECK_THROWS(runSource("let x = 5\nx()"));  // number не вызываем
    CHECK_THROWS(runSource("print(1)()"));      // nil не вызываем
}

TEST(runtime_error_message_and_line) {
    bool threw = false;
    try {
        (void)runSource("let x = 1\nlet x = 2");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()), std::string("Variable 'x' is already defined"));
        CHECK_EQ(e.line, 2);
    }
    CHECK(threw);
}

// --- Этап 5: if / elseif / else и области видимости ---

TEST(if_executes_branch_on_true) {
    CHECK_EQ(runSource("if true then print(1) end"), "1\n");
    CHECK_EQ(runSource("if false then print(1) end\nprint(2)"), "2\n");
}

TEST(the_stage_example_if_else) {
    CHECK_EQ(runSource(
                 "let x = 5\nif x > 10 then print(\"big\") else print(\"small\") end"),
             "small\n");
    CHECK_EQ(runSource(
                 "let x = 50\nif x > 10 then print(\"big\") else print(\"small\") end"),
             "big\n");
}

TEST(if_condition_uses_lua_truthiness) {
    CHECK_EQ(runSource("if 0 then print(1) end"), "1\n");     // 0 истинно
    CHECK_EQ(runSource("if \"\" then print(1) end"), "1\n");  // "" истинно
    CHECK_EQ(runSource("if nil then print(1) end\nprint(2)"), "2\n");
}

TEST(elseif_chain_first_match_wins) {
    CHECK_EQ(runSource("let s = 85\n"
                       "if s >= 90 then print(\"отлично\")\n"
                       "elseif s >= 70 then print(\"хорошо\")\n"
                       "else print(\"можно лучше\") end"),
             "хорошо\n");
    CHECK_EQ(runSource("let s = 95\n"
                       "if s >= 90 then print(\"отлично\")\n"
                       "elseif s >= 70 then print(\"хорошо\")\n"
                       "else print(\"можно лучше\") end"),
             "отлично\n");
    CHECK_EQ(runSource("let s = 50\n"
                       "if s >= 90 then print(\"отлично\")\n"
                       "elseif s >= 70 then print(\"хорошо\")\n"
                       "else print(\"можно лучше\") end"),
             "можно лучше\n");
    CHECK_EQ(runSource("if true then print(1) elseif true then print(2) end"), "1\n");
}

TEST(if_branches_are_separate_scopes) {
    // 'v' объявляется в разных ветвях — разные области, конфликта нет
    CHECK_EQ(runSource("if false then let v = 1 else let v = 2\nprint(v) end"), "2\n");
}

TEST(block_scope_hides_variables) {
    CHECK_THROWS(runSource("if true then let inner = 5 end\nprint(inner)"));
    CHECK_THROWS(runSource("if true then let inner = 5 end\ninner = 7"));
}

TEST(shadowing_in_block_allowed) {
    CHECK_EQ(runSource("let x = 1\nif true then let x = 2\nprint(x) end\nprint(x)"),
             "2\n1\n");
}

TEST(assign_reaches_outer_variable) {
    CHECK_EQ(runSource("let x = 1\nif true then x = 5 end\nprint(x)"), "5\n");
}

TEST(nested_if) {
    CHECK_EQ(runSource("if true then if false then print(1) else print(2) end end"),
             "2\n");
}

TEST(empty_block_body) {
    CHECK_EQ(runSource("if false then end\nprint(7)"), "7\n");
}

TEST(multi_statement_body) {
    CHECK_EQ(runSource("if true then let a = 1\nlet b = 2\nprint(a + b) end"), "3\n");
}

TEST(if_parser_structure) {
    Program program =
        parseProgramSource("if a then print(1) elseif b then print(2) else print(3) end");
    CHECK_EQ(program.statements.size(), 1u);

    const IfStmt* ifStmt = std::get_if<IfStmt>(&program.statements[0].value);
    CHECK(ifStmt != nullptr);
    CHECK_EQ(ifStmt->branches.size(), 3u);
    CHECK(ifStmt->branches[0].condition != nullptr);
    CHECK(ifStmt->branches[1].condition != nullptr);
    CHECK(ifStmt->branches[2].condition == nullptr);  // else — ветвь без условия
    CHECK_EQ(ifStmt->branches[0].body->statements.size(), 1u);
    CHECK_EQ(ifStmt->branches[2].body->statements.size(), 1u);
}

TEST(if_syntax_errors) {
    CHECK_THROWS(parseProgramSource("if true print(1) end"));   // нет 'then'
    CHECK_THROWS(parseProgramSource("if true then print(1)"));  // нет 'end'
    CHECK_THROWS(parseProgramSource("if then end"));            // нет условия
    CHECK_THROWS(parseProgramSource("if true then else elseif true then end"));
}

TEST(if_error_message) {
    bool threw = false;
    try {
        (void)parseProgramSource("if true print(1) end");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()), std::string("expected 'then' but got 'print'"));
        CHECK_EQ(e.line, 1);
    }
    CHECK(threw);
}

// --- Этап 6: while / for / break / continue ---

TEST(while_basic) {
    CHECK_EQ(runSource("let x = 0\nwhile x < 3 do print(x)\n  x = x + 1 end"),
             "0\n1\n2\n");
}

TEST(while_zero_iterations) {
    CHECK_EQ(runSource("let x = 5\nwhile x < 3 do print(1) end\nprint(2)"), "2\n");
    CHECK_EQ(runSource("while nil do print(1) end\nprint(2)"), "2\n");
}

TEST(while_body_scope_fresh_each_iteration) {
    CHECK_EQ(
        runSource("let n = 0\nwhile n < 3 do let v = n * 2\nprint(v)\n  n = n + 1 end"),
        "0\n2\n4\n");
}

TEST(while_continue_rechecks_condition) {
    CHECK_EQ(runSource("let x = 0\n"
                       "while x < 5 do\n"
                       "  x = x + 1\n"
                       "  if x % 2 == 0 then continue end\n"
                       "  print(x)\n"
                       "end"),
             "1\n3\n5\n");
}

TEST(while_break) {
    CHECK_EQ(runSource(
                 "let x = 0\nwhile true do x = x + 1\nif x > 2 then break end end\nprint(x)"),
             "3\n");
}

TEST(for_basic_half_open) {
    CHECK_EQ(runSource("for i in 0..3 do print(i) end"), "0\n1\n2\n");
    CHECK_EQ(runSource("for i in 2..2 do print(i) end\nprint(9)"), "9\n");  // пустой
}

TEST(for_bounds_are_expressions_evaluated_once) {
    CHECK_EQ(runSource("let n = 3\nfor i in 0..n * 2 do print(i) end"),
             "0\n1\n2\n3\n4\n5\n");
    // Границы вычисляются один раз: изменение n внутри цикла не расширяет его.
    CHECK_EQ(runSource("let n = 2\nfor i in 0..n do n = 99\nprint(i) end"), "0\n1\n");
}

TEST(for_by_step) {
    CHECK_EQ(runSource("for i in 0..10 by 3 do print(i) end"), "0\n3\n6\n9\n");
}

TEST(for_negative_step) {
    CHECK_EQ(runSource("for i in 3..0 by -1 do print(i) end"), "3\n2\n1\n");
    // Шаг по умолчанию +1: диапазон вниз пуст
    CHECK_EQ(runSource("for i in 3..0 do print(i) end\nprint(9)"), "9\n");
}

TEST(for_loop_var_modification_ignored) {
    // i в теле — копия: ход цикла задаёт внутренний счётчик (как в Lua и Python)
    CHECK_EQ(runSource("for i in 0..3 do i = 100\nprint(i) end"), "100\n100\n100\n");
}

TEST(for_loop_var_scope) {
    // Переменная цикла затеняет внешнюю и не видна после цикла
    CHECK_EQ(runSource("let i = 99\nfor i in 0..2 do print(i) end\nprint(i)"),
             "0\n1\n99\n");
    CHECK_THROWS(runSource("for i in 0..2 do end\nprint(i)"));
    // let в теле затеняет переменную цикла (она в своей, внешней для тела области)
    CHECK_EQ(runSource("for i in 0..3 do let i = 5\nprint(i) end"), "5\n5\n5\n");
}

TEST(for_body_scope_fresh_each_iteration) {
    // Ключевой тест: без свежей области на итерацию let v упал бы
    // с "Variable 'v' is already defined" на второй итерации
    CHECK_EQ(runSource("for i in 0..3 do let v = i * 10\nprint(v) end"), "0\n10\n20\n");
}

TEST(for_break_and_continue) {
    CHECK_EQ(runSource("for i in 0..10 do if i == 3 then break end\nprint(i) end"),
             "0\n1\n2\n");
    CHECK_EQ(runSource("for i in 0..5 do if i % 2 == 0 then continue end\nprint(i) end"),
             "1\n3\n");
}

TEST(break_from_nested_blocks) {
    CHECK_EQ(
        runSource("let x = 0\nfor i in 0..3 do if i == 0 then x = 7\nbreak end end\nprint(x)"),
        "7\n");
    CHECK_EQ(runSource("for i in 0..3 do if i == 1 then break end let a = 1 end\nprint(9)"),
             "9\n");
}

TEST(nested_loops_break_targets_inner) {
    CHECK_EQ(runSource("for i in 0..3 do\n"
                       "  for j in 0..3 do\n"
                       "    if j == 2 then break end\n"
                       "    print(i .. \"-\" .. j)\n"
                       "  end\n"
                       "end"),
             "0-0\n0-1\n1-0\n1-1\n2-0\n2-1\n");
}

TEST(for_runtime_errors) {
    CHECK_THROWS(runSource("for i in 0..\"x\" do end"));
    CHECK_THROWS(runSource("for i in \"a\"..3 do end"));
    CHECK_THROWS(runSource("for i in true..3 do end"));
    CHECK_THROWS(runSource("for i in 0..3 by 0 do print(i) end"));  // шаг 0
}

TEST(loop_parser_errors) {
    // break/continue вне цикла — семантическая ошибка (Analyzer, test_analyzer),
    // здесь — чисто синтаксические случаи:
    CHECK_THROWS(parseProgramSource("for 5 in 0..3 do end"));       // не имя
    CHECK_THROWS(parseProgramSource("for i 0..3 do end"));          // нет 'in'
    CHECK_THROWS(parseProgramSource("for i in 0 3 do end"));        // нет '..'
    CHECK_THROWS(parseProgramSource("for i in 0..3 print(i) end")); // нет 'do'
    CHECK_THROWS(parseProgramSource("for i in 0..3 do print(i)"));  // нет 'end'
    CHECK_THROWS(parseProgramSource("while do end"));               // нет условия
    CHECK_THROWS(parseProgramSource("while true print(1) end"));    // нет 'do'
}

TEST(loop_context_error_message) {
    bool threw = false;
    try {
        (void)runSource("break");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("break is only allowed inside a loop"));
        CHECK_EQ(e.line, 1);
    }
    CHECK(threw);
}

// --- Этап 7: функции, return, замыкания ---

TEST(the_stage_example_functions) {
    CHECK_EQ(runSource("function add(a, b)\n    return a + b\nend\n"
                       "let result = add(10, 20)\nprint(result)"),
             "30\n");
}

TEST(function_returns_nil_without_return) {
    CHECK_EQ(runSource("function f() end\nprint(f())"), "nil\n");
    CHECK_EQ(runSource("function f() return end\nprint(f())"), "nil\n");
    CHECK_EQ(runSource("function f() print(f) end\nprint(f())"), "<function>\nnil\n");
}

TEST(early_return_branches) {
    CHECK_EQ(runSource("function sign(x)\n"
                       "  if x > 0 then return \"pos\" end\n"
                       "  if x < 0 then return \"neg\" end\n"
                       "  return \"zero\"\n"
                       "end\n"
                       "print(sign(5), sign(-5), sign(0))"),
             "pos neg zero\n");
}

TEST(recursion_factorial) {
    CHECK_EQ(runSource("function fact(n)\n"
                       "  if n <= 1 then return 1 end\n"
                       "  return n * fact(n - 1)\n"
                       "end\n"
                       "print(fact(10))"),
             "3628800\n");
}

TEST(recursion_fibonacci) {
    CHECK_EQ(runSource("function fib(n)\n"
                       "  if n < 2 then return n end\n"
                       "  return fib(n - 1) + fib(n - 2)\n"
                       "end\n"
                       "print(fib(15))"),
             "610\n");
}

TEST(functions_are_first_class_values) {
    CHECK_EQ(runSource("function add(a, b)\nreturn a + b\nend\n"
                       "let f = add\nprint(f(2, 3))"),
             "5\n");
    CHECK_EQ(runSource("function f() end\nprint(f)"), "<function>\n");
}

TEST(functions_passed_as_arguments) {
    CHECK_EQ(runSource("function apply(f, x)\nreturn f(x)\nend\n"
                       "function double(x)\nreturn x * 2\nend\n"
                       "print(apply(double, 21))"),
             "42\n");
}

TEST(function_locals_are_hidden) {
    CHECK_EQ(runSource("function f()\nlet a = 1\nlet b = 2\nreturn a + b\nend\nprint(f())"),
             "3\n");
    CHECK_THROWS(runSource("function f()\nlet a = 1\nreturn a\nend\nprint(f())\nprint(a)"));
}

TEST(params_shadow_globals) {
    CHECK_EQ(runSource("let x = 1\nfunction f(x)\nreturn x * 10\nend\n"
                       "print(f(3))\nprint(x)"),
             "30\n1\n");
}

TEST(function_arity_errors) {
    CHECK_THROWS(runSource("function add(a, b)\nreturn a + b\nend\nprint(add(1))"));
    CHECK_THROWS(runSource("function add(a, b)\nreturn a + b\nend\nprint(add(1, 2, 3))"));

    bool threw = false;
    try {
        (void)runSource("function add(a, b)\nreturn a + b\nend\nprint(add(1))");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("function 'add' expects 2 arguments, got 1"));
        CHECK_EQ(e.line, 4);
    }
    CHECK(threw);
}

TEST(duplicate_parameter_error) {
    bool threw = false;
    try {
        (void)runSource("function f(a, a)\nend");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("Duplicate parameter 'a' in function 'f'"));
        CHECK_EQ(e.line, 1);
    }
    CHECK(threw);
}

TEST(return_outside_function_error) {
    CHECK_THROWS(runSource("return 1"));
    CHECK_THROWS(runSource("if true then return 1 end"));
    CHECK_THROWS(runSource("while true do return end"));

    bool threw = false;
    try {
        (void)runSource("return");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("return is only allowed inside a function"));
    }
    CHECK(threw);
}

TEST(break_cannot_cross_function_boundary) {
    // break внутри тела функции не видит внешний цикл
    CHECK_THROWS(runSource("while true do\nfunction f()\nbreak\nend\nend"));
    // а внутри функции цикл — можно
    CHECK_EQ(runSource("function f()\nwhile true do break end\nreturn \"ok\"\nend\nprint(f())"),
             "ok\n");
}

TEST(closure_captures_definition_environment) {
    CHECK_EQ(runSource("function makeGreeter(prefix)\n"
                       "  function greet(name)\n"
                       "    return prefix .. \" \" .. name\n"
                       "  end\n"
                       "  return greet\n"
                       "end\n"
                       "let hello = makeGreeter(\"Привет\")\n"
                       "print(hello(\"Мир\"))"),
             "Привет Мир\n");
}

TEST(closure_mutation_counter) {
    // Ключевой тест замыкания: increment изменяет count в окружении
    // makeCounter — значение живёт между вызовами
    CHECK_EQ(runSource("function makeCounter()\n"
                       "  let count = 0\n"
                       "  function increment()\n"
                       "    count = count + 1\n"
                       "    return count\n"
                       "  end\n"
                       "  return increment\n"
                       "end\n"
                       "let next = makeCounter()\n"
                       "print(next())\nprint(next())\nprint(next())"),
             "1\n2\n3\n");
}

TEST(closures_are_independent) {
    CHECK_EQ(runSource("function makeCounter()\n"
                       "  let count = 0\n"
                       "  function increment()\n"
                       "    count = count + 1\n"
                       "    return count\n"
                       "  end\n"
                       "  return increment\n"
                       "end\n"
                       "let a = makeCounter()\n"
                       "let b = makeCounter()\n"
                       "print(a())\nprint(a())\nprint(b())"),
             "1\n2\n1\n");
}

TEST(return_from_inside_loop) {
    CHECK_EQ(runSource("function find()\n"
                       "  for i in 0..10 do\n"
                       "    if i * i > 20 then return i end\n"
                       "  end\n"
                       "  return -1\n"
                       "end\n"
                       "print(find())"),
             "5\n");
}

TEST(function_defined_in_loop_captures_iteration_variable) {
    CHECK_EQ(runSource("for i in 0..3 do\n"
                       "  function f()\n"
                       "    return i\n"
                       "  end\n"
                       "  print(f())\n"
                       "end"),
             "0\n1\n2\n");
}

TEST(function_parser_structure) {
    Program program = parseProgramSource("function add(a, b)\nreturn a + b\nend");
    CHECK_EQ(program.statements.size(), 1u);

    const FunctionStmt* fn = std::get_if<FunctionStmt>(&program.statements[0].value);
    CHECK(fn != nullptr);
    CHECK_EQ(fn->name.lexeme, "add");
    CHECK_EQ(fn->params.size(), 2u);
    CHECK_EQ(fn->params[0].lexeme, "a");
    CHECK_EQ(fn->params[1].lexeme, "b");
    CHECK_EQ(fn->body->statements.size(), 1u);
    CHECK(std::holds_alternative<ReturnStmt>(fn->body->statements[0].value));
}

TEST(function_syntax_errors) {
    CHECK_THROWS(parseProgramSource("function () end"));        // нет имени
    CHECK_THROWS(parseProgramSource("function f a end"));       // нет '('
    CHECK_THROWS(parseProgramSource("function f(,) end"));      // не имя
    CHECK_THROWS(parseProgramSource("function f(a b) end"));    // нет запятой
    CHECK_THROWS(parseProgramSource("function f(a) return 1")); // нет 'end'
    CHECK_THROWS(parseProgramSource("function f(a"));           // нет ')'
}

// --- Этап 8: массивы и встроенные функции ---

TEST(array_literal_and_index) {
    CHECK_EQ(runSource("let a = [10, 20, 30]\nprint(a[0])"), "10\n");
    CHECK_EQ(runSource("let a = [10, 20, 30]\nprint(a[1], a[2])"), "20 30\n");
    CHECK_EQ(runSource("print([])"), "[]\n");
    CHECK_EQ(runSource("print([1, 2])"), "[1, 2]\n");
    // элементы любых типов вперемешку
    CHECK_EQ(runSource("let a = [1, \"two\", true, nil]\nprint(a[1], a[2], a[3])"),
             "two true nil\n");
}

TEST(array_nested_and_expression_index) {
    // вложенные массивы через цепочку индексов
    CHECK_EQ(runSource("let m = [[1, 2], [3, 4]]\nprint(m[1][0])"), "3\n");
    // индекс — любое выражение
    CHECK_EQ(runSource("let a = [10, 20]\nlet i = 1\nprint(a[i + 1 - 1])"), "20\n");
    // индексация результата вызова
    CHECK_EQ(runSource("function make()\nreturn [5, 6]\nend\nprint(make()[1])"), "6\n");
}

TEST(array_index_out_of_bounds) {
    CHECK_THROWS(runSource("let a = []\nprint(a[0])"));
    CHECK_THROWS(runSource("let a = [1]\nprint(a[1])"));
    CHECK_THROWS(runSource("let a = [1, 2]\nprint(a[-1])"));  // отрицательный — ошибка

    bool threw = false;
    try {
        (void)runSource("let a = [1, 2]\nprint(a[5])");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("array index 5 is out of bounds (length 2)"));
    }
    CHECK(threw);
}

TEST(array_index_type_errors) {
    CHECK_THROWS(runSource("let a = [1]\nprint(a[\"x\"])"));  // индекс не число
    CHECK_THROWS(runSource("let x = 5\nprint(x[0])"));        // число не индексируется
    CHECK_THROWS(runSource("print(\"hi\"[0])"));              // строки пока не индексируются
    CHECK_THROWS(runSource("print(nil[0])"));
    CHECK_THROWS(runSource("print(true[0])"));
}

TEST(array_index_assignment) {
    CHECK_EQ(runSource("let a = [1, 2, 3]\na[1] = 99\nprint(a[0], a[1], a[2])"),
             "1 99 3\n");
    CHECK_EQ(runSource("let a = [1, 2]\nlet i = 0\na[i + 1] = 7\nprint(a[1])"), "7\n");
    CHECK_EQ(runSource("let m = [[1, 2], [3, 4]]\nm[0][1] = 9\nprint(m[0][1])"), "9\n");
    CHECK_THROWS(runSource("let a = [1]\na[5] = 0"));  // вне границ
    CHECK_THROWS(runSource("let x = 1\nx[0] = 2"));    // цель не массив
}

TEST(assignment_target_validation) {
    CHECK_THROWS(parseProgramSource("1 = 2"));   // цель — литерал
    CHECK_THROWS(parseProgramSource("f() = 2")); // цель — вызов
    CHECK_THROWS(parseProgramSource("(x) = 2")); // цель в скобках не имя
}

TEST(array_reference_semantics) {
    // Массивы — ссылки: b и a указывают на один массив
    CHECK_EQ(runSource("let a = [1, 2]\nlet b = a\nb[0] = 99\nprint(a[0])"), "99\n");
}

TEST(builtin_len) {
    CHECK_EQ(runSource("print(len(\"привет\"))"), "12\n");  // байты UTF-8
    CHECK_EQ(runSource("print(len(\"abc\"))"), "3\n");
    CHECK_EQ(runSource("print(len([10, 20, 30]))"), "3\n");
    CHECK_EQ(runSource("print(len([]))"), "0\n");
    CHECK_THROWS(runSource("len(5)"));
    CHECK_THROWS(runSource("len()"));
    CHECK_THROWS(runSource("len(\"a\", \"b\")"));
}

TEST(builtin_push) {
    CHECK_EQ(runSource("let a = [1]\npush(a, 2)\nprint(a[0], a[1], len(a))"), "1 2 2\n");
    // push возвращает тот же массив
    CHECK_EQ(runSource("let a = [1]\nlet b = push(a, 2)\nprint(b[1])"), "2\n");
    CHECK_THROWS(runSource("push(5, 1)"));
    CHECK_THROWS(runSource("push(\"x\", 1)"));
}

TEST(builtin_str_and_num) {
    CHECK_EQ(runSource("print(str(42), str(1.5), str(nil), str(true))"),
             "42 1.5 nil true\n");
    CHECK_EQ(runSource("print(str([1, \"a\"]))"), "[1, a]\n");
    CHECK_EQ(runSource("print(num(\"12.5\") + 1)"), "13.5\n");
    CHECK_EQ(runSource("print(num(\"-7\"))"), "-7\n");
    CHECK_THROWS(runSource("num(\"abc\")"));
    CHECK_THROWS(runSource("num(\"12 x\")"));  // хвост — ошибка
    CHECK_THROWS(runSource("num(5)"));
}

TEST(array_parser_structure) {
    Program program = parseProgramSource("let a = [1, x + 1]\nprint(a[0])");
    CHECK_EQ(program.statements.size(), 2u);

    const LetStmt* let = std::get_if<LetStmt>(&program.statements[0].value);
    CHECK(let != nullptr);
    const ArrayExpr* array = std::get_if<ArrayExpr>(&let->initializer->value);
    CHECK(array != nullptr);
    CHECK_EQ(array->elements.size(), 2u);
    CHECK(std::holds_alternative<LiteralExpr>(array->elements[0].value));
    CHECK(std::holds_alternative<BinaryExpr>(array->elements[1].value));

    const ExprStmt* call = std::get_if<ExprStmt>(&program.statements[1].value);
    CHECK(call != nullptr);
    const CallExpr* outer = std::get_if<CallExpr>(&call->expression->value);
    CHECK(outer != nullptr);
    CHECK_EQ(outer->args.size(), 1u);
    CHECK(std::holds_alternative<IndexExpr>(outer->args[0].value));
}
