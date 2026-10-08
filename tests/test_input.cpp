#include <sstream>
#include <string>

#include "../src/analyzer.h"
#include "../src/ast.h"
#include "../src/chunk.h"
#include "../src/compiler.h"
#include "../src/error.h"
#include "../src/interpreter.h"
#include "../src/lexer.h"
#include "../src/natives.h"
#include "../src/parser.h"
#include "../src/vm.h"
#include "test.h"

namespace {

// Подменяет inputStream() на строку с вводом, исполняет программу выбранным
// бэкендом и возвращает вывод print(). Поток восстанавливается при любом выходе.
struct FakeInput {
    std::istringstream stream;
    std::istream* previous;

    FakeInput(const std::string& input) : stream(input), previous(inputStream()) {
        inputStream() = &stream;
    }
    ~FakeInput() { inputStream() = previous; }
};

std::string runInterpretedWithInput(const std::string& source,
                                    const std::string& input) {
    FakeInput fake(input);
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

std::string runVMWithInput(const std::string& source, const std::string& input) {
    FakeInput fake(input);
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

// Оба бэкенда с одним вводом обязаны дать одинаковый результат.
std::string runBothWithInput(const std::string& source, const std::string& input) {
    std::string interpreted = runInterpretedWithInput(source, input);
    std::string vm = runVMWithInput(source, input);
    CHECK_EQ(vm, interpreted);
    return interpreted;
}

}  // namespace

TEST(input_returns_string_not_number) {
    // "15" обязана остаться строкой: сравнение со строкой даёт true,
    // автоматического преобразования типов нет (спека 0.1.1 п.10).
    CHECK_EQ(runBothWithInput("let age = input()\nprint(age == \"15\")", "15\n"),
             "true\n");
}

TEST(input_reads_whole_line_with_spaces) {
    CHECK_EQ(runBothWithInput("let name = input()\nprint(name)", "John Smith\n"),
             "John Smith\n");
    CHECK_EQ(runBothWithInput("print(input())", "  leading spaces kept  \n"),
             "  leading spaces kept  \n");
}

TEST(input_preserves_unicode_and_long_lines) {
    CHECK_EQ(runBothWithInput("print(input())", "Привет, Лума!\n"),
             "Привет, Лума!\n");
    std::string longLine(12000, 'x');
    CHECK_EQ(runBothWithInput("let text = input()\nprint(len(text))", longLine + "\n"),
             "12000\n");
}

TEST(input_in_assignment_and_print) {
    CHECK_EQ(runBothWithInput("let name = input()\nprint(\"Hello, \" .. name)",
                              "Alex\n"),
             "Hello, Alex\n");
    CHECK_EQ(runBothWithInput("print(input())", "hi\n"), "hi\n");
}

TEST(input_multiple_calls_read_lines_in_order) {
    CHECK_EQ(runBothWithInput("let a = input()\nlet b = input()\nprint(a .. \"-\" .. b)",
                              "one\ntwo\n"),
             "one-two\n");
    CHECK_EQ(runBothWithInput("print(input())\nprint(input())\nprint(input())",
                              "1\n2\n3\n"),
             "1\n2\n3\n");
}

TEST(input_empty_line_is_empty_string) {
    CHECK_EQ(runBothWithInput("let s = input()\nprint(s == \"\")", "\n"), "true\n");
}

TEST(input_eof_returns_nil) {
    // Пустой ввод: читать нечего → nil (никаких зависаний и UB).
    CHECK_EQ(runBothWithInput("print(input())", ""), "nil\n");
    // С truthiness пустая программа продолжается: input() or default.
    CHECK_EQ(runBothWithInput("print(input() or \"гость\")", ""), "гость\n");
    // Одна строка, потом EOF: второй вызов — nil.
    CHECK_EQ(runBothWithInput("print(input())\nprint(input())", "только одна\n"),
             "только одна\nnil\n");
}

TEST(input_with_num_conversion) {
    // Число из ввода — явно через num(input()).
    CHECK_EQ(runBothWithInput("print(num(input()) + 1)", "15\n"), "16\n");
}

TEST(input_invalid_num_conversion_is_luma_error) {
    const std::string source = "print(num(input()))";
    for (bool useVm : {false, true}) {
        bool threw = false;
        try {
            if (useVm)
                (void)runVMWithInput(source, "not-a-number\n");
            else
                (void)runInterpretedWithInput(source, "not-a-number\n");
        } catch (const LumaError& e) {
            threw = true;
            CHECK_EQ(std::string(e.what()),
                     std::string("cannot convert 'not-a-number' to a number"));
        }
        CHECK(threw);
    }
}

TEST(num_empty_input_is_not_zero) {
    CHECK_THROWS(runVMWithInput("print(num(input()))", "\n"));
    CHECK_THROWS(runInterpretedWithInput("print(num(input()))", "\n"));
    CHECK_EQ(runBothWithInput("print(num(input()))", "-123\n"), "-123\n");
}

TEST(input_inside_function) {
    CHECK_EQ(runBothWithInput("function ask()\nreturn input()\nend\nprint(ask())",
                              "ответ\n"),
             "ответ\n");
}

TEST(input_works_after_bytecode_serialization) {
    // input() — обычная builtin-функция, поэтому должен остаться доступен
    // и после source -> bytecode -> .lbc -> VM, без специального opcode.
    FakeInput fake("Ada Lovelace\n");
    Lexer lexer("let name = input()\nprint(\"Hello, \" .. name)");
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Analyzer analyzer;
    analyzer.analyze(program);
    Compiler compiler;
    std::shared_ptr<Chunk> chunk = compiler.compile(program);

    std::stringstream artifact(std::ios::in | std::ios::out | std::ios::binary);
    writeChunk(*chunk, artifact);
    artifact.seekg(0);
    std::shared_ptr<Chunk> restored = readChunk(artifact);

    std::ostringstream out;
    VM vm(out);
    vm.run(*restored);
    CHECK_EQ(out.str(), "Hello, Ada Lovelace\n");
}

TEST(input_arity_error) {
    // Аргументы запрещены; статическую арность ловит анализатор до исполнения.
    bool threw = false;
    {
        FakeInput fake("\n");
        try {
            Lexer lexer("input(1)");
            Parser parser(lexer.tokenize());
            Program program = parser.parseProgram();
            Analyzer analyzer;
            analyzer.analyze(program);
            (void)program;
        } catch (const LumaError& e) {
            threw = true;
            CHECK_EQ(std::string(e.what()),
                     std::string("function 'input' expects 0 arguments, got 1"));
        }
    }
    CHECK(threw);
}
