#include <sstream>
#include <string>

#include "../src/analyzer.h"
#include "../src/ast.h"
#include "../src/chunk.h"
#include "../src/compiler.h"
#include "../src/error.h"
#include "../src/interpreter.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/vm.h"
#include "test.h"

namespace {

// Оба бэкенда на одном источнике. Вывод print() собирается в строку.

std::string runInterpreted(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Analyzer analyzer;
    analyzer.analyze(program);
    std::ostringstream out;
    Interpreter interpreter(out);
    interpreter.run(program);
    return out.str();
}

std::string runVM(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Analyzer analyzer;
    analyzer.analyze(program);
    Compiler compiler;
    std::shared_ptr<Chunk> chunk = compiler.compile(program);
    std::ostringstream out;
    VM vm(out);
    vm.run(*chunk);
    return out.str();
}

// ОБА бэкенда обязаны дать одинаковый вывод — главный инвариант этапа.
std::string runBoth(const std::string& source) {
    std::string interpreted = runInterpreted(source);
    std::string vm = runVM(source);
    CHECK_EQ(vm, interpreted);  // при расхождении печатает оба вывода
    return interpreted;
}

// Ошибки тоже должны совпадать (сообщение и строка) в обоих бэкендах.
void checkSameError(const std::string& source) {
    std::string message1, message2;
    int line1 = 0, line2 = 0;
    bool threw1 = false, threw2 = false;
    try {
        (void)runInterpreted(source);
    } catch (const LumaError& e) {
        threw1 = true;
        message1 = e.what();
        line1 = e.line;
    }
    try {
        (void)runVM(source);
    } catch (const LumaError& e) {
        threw2 = true;
        message2 = e.what();
        line2 = e.line;
    }
    CHECK(threw1);
    CHECK(threw2);
    CHECK_EQ(message1, message2);
    CHECK_EQ(line1, line2);
}

}  // namespace

// --- Эквивалентность бэкендов на всех конструкциях языка ---

TEST(vm_equivalence_arithmetic) {
    CHECK_EQ(runBoth("print(1 + 2 * 3 - 4 / 2)"), "5\n");
    CHECK_EQ(runBoth("print(7 % 3, -7 % 3, 2 * 3 + 4 % 3)"), "1 2 7\n");
    CHECK_EQ(runBoth("print(1 / 0)"), "inf\n");
    CHECK_EQ(runBoth("print(-5 + 3, -(2 + 3))"), "-2 -5\n");
    CHECK_EQ(runBoth("print(1.5 + 1.5)"), "3\n");
}

TEST(vm_equivalence_strings_and_concat) {
    CHECK_EQ(runBoth("print(\"a\" .. \"b\" .. \"c\")"), "abc\n");
    CHECK_EQ(runBoth("print(\"n=\" .. 5, 1 .. 2)"), "n=5 12\n");
    CHECK_EQ(runBoth("let s = \"сумма: \"\nprint(s .. (10 + 5))"), "сумма: 15\n");
}

TEST(vm_equivalence_comparisons) {
    CHECK_EQ(runBoth("print(1 < 2, 2 <= 2, 3 > 4, 4 >= 5, 1 == 1, 1 != 1)"),
             "true true false false true false\n");
    CHECK_EQ(runBoth("print(1 == \"1\", nil == nil, 0 == false)"),
             "false true false\n");
    CHECK_EQ(runBoth("print(\"a\" < \"b\")"), "true\n");
}

TEST(vm_equivalence_if_elseif_else) {
    CHECK_EQ(runBoth("let s = 85\n"
                     "if s >= 90 then print(\"отлично\")\n"
                     "elseif s >= 70 then print(\"хорошо\")\n"
                     "else print(\"можно лучше\") end"),
             "хорошо\n");
    CHECK_EQ(runBoth("let x = 5\nif x > 10 then print(\"big\") else print(\"small\") end"),
             "small\n");
    CHECK_EQ(runBoth("if 0 then print(1) end\nif nil then print(2) end\nprint(3)"),
             "1\n3\n");
}

TEST(vm_equivalence_while_break_continue) {
    CHECK_EQ(runBoth("let x = 0\n"
                     "while x < 5 do\n"
                     "  x = x + 1\n"
                     "  if x % 2 == 0 then continue end\n"
                     "  print(x)\n"
                     "end"),
             "1\n3\n5\n");
    CHECK_EQ(runBoth("let x = 0\n"
                     "while true do\n"
                     "  x = x + 1\n"
                     "  if x > 2 then break end\n"
                     "end\n"
                     "print(x)"),
             "3\n");
}

TEST(vm_equivalence_for_loops) {
    CHECK_EQ(runBoth("for i in 0..3 do print(i) end"), "0\n1\n2\n");
    CHECK_EQ(runBoth("for i in 0..10 by 3 do print(i) end"), "0\n3\n6\n9\n");
    CHECK_EQ(runBoth("for i in 3..0 by -1 do print(i) end"), "3\n2\n1\n");
    CHECK_EQ(runBoth("for i in 2..2 do print(i) end\nprint(9)"), "9\n");
    // переменная цикла — копия: модификация в теле не влияет на ход
    CHECK_EQ(runBoth("for i in 0..3 do i = 100\nprint(i) end"), "100\n100\n100\n");
    // границы вычисляются один раз
    CHECK_EQ(runBoth("let n = 2\nfor i in 0..n do n = 99\nprint(i) end"), "0\n1\n");
    // свежая область на итерацию
    CHECK_EQ(runBoth("for i in 0..3 do let v = i * 10\nprint(v) end"), "0\n10\n20\n");
}

TEST(vm_equivalence_functions_and_recursion) {
    CHECK_EQ(runBoth("function add(a, b)\nreturn a + b\nend\nprint(add(10, 20))"),
             "30\n");
    CHECK_EQ(runBoth("function fact(n)\n"
                     "  if n <= 1 then return 1 end\n"
                     "  return n * fact(n - 1)\n"
                     "end\n"
                     "print(fact(10))"),
             "3628800\n");
    CHECK_EQ(runBoth("function fib(n)\n"
                     "  if n < 2 then return n end\n"
                     "  return fib(n - 1) + fib(n - 2)\n"
                     "end\n"
                     "print(fib(15))"),
             "610\n");
    CHECK_EQ(runBoth("function f() end\nprint(f())"), "nil\n");
    CHECK_EQ(runBoth("function apply(f, x)\nreturn f(x)\nend\n"
                     "function double(x)\nreturn x * 2\nend\n"
                     "print(apply(double, 21))"),
             "42\n");
}

TEST(vm_equivalence_closures) {
    CHECK_EQ(runBoth("function makeGreeter(prefix)\n"
                     "  function greet(name)\n"
                     "    return prefix .. \" \" .. name\n"
                     "  end\n"
                     "  return greet\n"
                     "end\n"
                     "let hello = makeGreeter(\"Привет\")\n"
                     "print(hello(\"Мир\"))"),
             "Привет Мир\n");
    // Замыкание с мутацией + независимость счётчиков
    CHECK_EQ(runBoth("function makeCounter()\n"
                     "  let count = 0\n"
                     "  function increment()\n"
                     "    count = count + 1\n"
                     "    return count\n"
                     "  end\n"
                     "  return increment\n"
                     "end\n"
                     "let a = makeCounter()\n"
                     "let b = makeCounter()\n"
                     "print(a(), a(), b())"),
             "1 2 1\n");
    // Захват переменной цикла — по итерациям
    CHECK_EQ(runBoth("for i in 0..3 do\n"
                     "  function f()\n"
                     "    return i\n"
                     "  end\n"
                     "  print(f())\n"
                     "end"),
             "0\n1\n2\n");
}

TEST(vm_equivalence_arrays) {
    CHECK_EQ(runBoth("let a = [10, 20, 30]\n"
                     "print(a[0], a[2], len(a))\n"
                     "push(a, 40)\n"
                     "print(len(a), a[3])\n"
                     "a[0] = 99\n"
                     "print(str(a))"),
             "10 30 3\n4 40\n[99, 20, 30, 40]\n");
    CHECK_EQ(runBoth("let m = [[1, 2], [3, 4]]\nm[1][0] = 9\nprint(m[1][0])"), "9\n");
    CHECK_EQ(runBoth("let a = [1, 2]\nlet b = a\nb[0] = 99\nprint(a[0])"), "99\n");
    CHECK_EQ(runBoth("print(str([1, \"x\", nil, true]))"), "[1, x, nil, true]\n");
    CHECK_EQ(runBoth("print(num(\"12.5\") + 1)"), "13.5\n");
}

TEST(vm_equivalence_logic_and_scopes) {
    CHECK_EQ(runBoth("print(1 and 2, nil and 2, false or 3, 1 or 2)"), "2 nil 3 1\n");
    CHECK_EQ(runBoth("print(not true, not nil, not 0, not \"\")"),
             "false true false false\n");
    // Короткое замыкание: правая часть с делением на ноль не вычисляется
    CHECK_EQ(runBoth("print(false and 1 / 0, true or 1 / 0)"), "false true\n");
    CHECK_EQ(runBoth("let x = 1\nif true then let x = 2\nprint(x) end\nprint(x)"),
             "2\n1\n");
    CHECK_EQ(runBoth("print(nil or \"гость\")"), "гость\n");
}

TEST(vm_equivalence_big_program) {
    // Программа-витрина: всё вместе
    CHECK_EQ(runBoth("let numbers = [10, 20, 30]\n"
                     "push(numbers, 40)\n"
                     "let total = 0\n"
                     "for i in 0..len(numbers) do\n"
                     "  total = total + numbers[i]\n"
                     "end\n"
                     "print(total)\n"
                     "function makeGreeter(prefix)\n"
                     "  function greet(name)\n"
                     "    return prefix .. \" \" .. name\n"
                     "  end\n"
                     "  return greet\n"
                     "end\n"
                     "print(makeGreeter(\"Привет\")(\"Мир\"))\n"
                     "let n = 2\n"
                     "while n > 0 do\n"
                     "  print(\"тикет \" .. n)\n"
                     "  n = n - 1\n"
                     "end"),
             "100\nПривет Мир\nтикет 2\nтикет 1\n");
}

// --- Ошибки времени исполнения: одинаковые в обоих бэкендах ---

TEST(vm_errors_match_interpreter) {
    checkSameError("print(1 + \"a\")");        // cannot add number and string
    checkSameError("print(\"a\" - 1)");
    checkSameError("print(-\"x\")");
    checkSameError("print(1 < \"a\")");
    checkSameError("print(true .. 1)");
    checkSameError("let a = [1, 2]\nprint(a[5])");  // out of bounds, строка 2
    checkSameError("let a = [1]\nprint(a[\"x\"])");
    checkSameError("let x = 5\nprint(x[0])");
    checkSameError("for i in 0..3 by 0 do end");    // FAIL: шаг ноль
    checkSameError("function g(x)\nreturn x\nend\nlet f = g\nf()");  // арность через переменную
}

TEST(vm_runtime_arity_error) {
    // Вызов по переменной — статическая арность неприменима, ловит рантайм.
    // Сообщения обоих бэкендов обязаны совпасть.
    bool threwVM = false, threwInterp = false;
    std::string vmMessage, interpMessage;
    try {
        (void)runVM("function add(a, b)\nreturn a + b\nend\nlet f = add\nf(1)");
    } catch (const LumaError& e) {
        threwVM = true;
        vmMessage = e.what();
    }
    try {
        (void)runInterpreted("function add(a, b)\nreturn a + b\nend\nlet f = add\nf(1)");
    } catch (const LumaError& e) {
        threwInterp = true;
        interpMessage = e.what();
    }
    CHECK(threwVM);
    CHECK(threwInterp);
    CHECK_EQ(vmMessage, std::string("function 'add' expects 2 arguments, got 1"));
    CHECK_EQ(vmMessage, interpMessage);
}

TEST(vm_calls_native_with_exact_arity) {
    // Встроенные функции через CALL в VM: len/push/str/num
    CHECK_EQ(runBoth("print(len([1, 2, 3]), push([1], 2)[1], str(nil), num(\"7\") + 0)"),
             "3 2 nil 7\n");
}

TEST(vm_recursion_does_not_exhaust_c_stack) {
    // Рекурсия Luma в VM — итеративные кадры в куче: глубина 10000 допустима
    // (tree-walking упал бы на C++ стеке).
    CHECK_EQ(runVM("function count(n)\n"
                   "  if n == 0 then return 0 end\n"
                   "  return 1 + count(n - 1)\n"
                   "end\n"
                   "print(count(10000))"),
             "10000\n");
}

TEST(array_nan_indices_are_safe) {
    checkSameError("let a = [1]\nprint(a[0/0])");
    checkSameError("let a = [1]\na[0/0] = 2");
}

TEST(cyclic_arrays_can_be_printed) {
    CHECK_EQ(runBoth("let a = []\npush(a,a)\nprint(a)"), "[[<cycle>]]\n");
}

TEST(large_numbers_print_safely) {
    CHECK_EQ(runBoth("print(1/0, 0/0)"), "inf nan\n");
}

TEST(nan_ordered_comparisons_match_interpreter) {
    CHECK_EQ(runBoth("let n = 0/0\nprint(n <= 1, n >= 1, 1 <= n, 1 >= n)"),
             "false false false false\n");
    CHECK_EQ(runBoth("let count = 0\nfunction next()\ncount = count + 1\nreturn count\nend\nprint(next() <= next(), count)"), "true 2\n");
}

TEST(vm_unbounded_recursion_reports_resource_limit) {
    bool caught = false;
    try {
        (void)runVM("function forever()\nreturn forever()\nend\nforever()");
    } catch (const LumaError& error) {
        caught = true;
        CHECK(std::string(error.what()).find("resource limit") != std::string::npos);
    }
    CHECK(caught);
}

TEST(compiler_operand_is_not_return_opcode) {
    std::string source = "function f()\n";
    for (int i = 0; i < 3900; ++i)
        source += "let v" + std::to_string(i) + " = 1\n";
    source += "end\nprint(f())";
    CHECK_EQ(runVM(source), "nil\n");
}
