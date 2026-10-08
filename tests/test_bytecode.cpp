#include <sstream>
#include <string>
#include <vector>

#include "../src/analyzer.h"
#include "../src/ast.h"
#include "../src/chunk.h"
#include "../src/compiler.h"
#include "../src/error.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "test.h"

namespace {

// Полный фронтенд до bytecode: разбор + анализ + компиляция.
std::shared_ptr<Chunk> compileSource(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Analyzer analyzer;
    analyzer.analyze(program);
    Compiler compiler;
    return compiler.compile(program);
}

struct Decoded {
    OpCode op;
    uint16_t operand;
    int line;
};

std::vector<Decoded> decode(const Chunk& chunk) {
    std::vector<Decoded> result;
    size_t pos = 0;
    while (pos < chunk.code.size()) {
        OpCode op = static_cast<OpCode>(chunk.code[pos]);
        size_t len = instructionLength(op);
        uint16_t operand = 0;
        if (len == 3)
            operand = static_cast<uint16_t>(chunk.code[pos + 1] |
                                            (chunk.code[pos + 2] << 8));
        result.push_back({op, operand, chunk.lines[pos]});
        pos += len;
    }
    return result;
}

std::vector<OpCode> opcodes(const Chunk& chunk) {
    std::vector<OpCode> result;
    for (const Decoded& d : decode(chunk)) result.push_back(d.op);
    return result;
}

// Все переходы должны вести на начало инструкции и не остаться незаплатанными.
void validateJumps(const Chunk& chunk) {
    std::vector<Decoded> decoded = decode(chunk);
    std::vector<size_t> starts;
    for (size_t pos = 0, i = 0; pos < chunk.code.size(); pos += instructionLength(decoded[i++].op))
        starts.push_back(pos);

    for (size_t i = 0; i < decoded.size(); ++i) {
        size_t pos = starts[i];
        if (decoded[i].op == OpCode::JUMP || decoded[i].op == OpCode::JUMP_IF_FALSE) {
            CHECK(decoded[i].operand != 0xFFFF);  // placeholder остался — ошибка патча
            int32_t offset = static_cast<int16_t>(decoded[i].operand);
            size_t target = pos + 3 + static_cast<size_t>(offset);
            bool found = false;
            for (size_t s : starts)
                if (s == target) found = true;
            CHECK(found);
        } else if (decoded[i].op == OpCode::LOOP) {
            size_t target = pos + 3 - decoded[i].operand;
            bool found = false;
            for (size_t s : starts)
                if (s == target) found = true;
            CHECK(found);
        }
    }
}

const double* constantNumber(const Chunk& chunk, uint16_t idx) {
    return std::get_if<double>(&chunk.constants[idx]);
}

const std::string* constantString(const Chunk& chunk, uint16_t idx) {
    return std::get_if<std::string>(&chunk.constants[idx]);
}

}  // namespace

TEST(compile_simple_program) {
    std::shared_ptr<Chunk> chunk = compileSource("let x = 10\nprint(x)");
    // print — встроенная функция: вызов через GET_VAR + CALL, не опкод.
    const std::vector<OpCode> expected = {
        OpCode::CONSTANT, OpCode::DEFINE_VAR,
        OpCode::GET_VAR, OpCode::GET_VAR, OpCode::CALL, OpCode::POP,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*chunk) == expected);
    CHECK(constantNumber(*chunk, 0) != nullptr);      // 10
    CHECK_EQ(*constantNumber(*chunk, 0), 10.0);
    CHECK(constantString(*chunk, 1) != nullptr);      // "x"
    CHECK_EQ(*constantString(*chunk, 1), "x");
    validateJumps(*chunk);
}

TEST(compile_arithmetic_and_comparison_mapping) {
    // print(2 + 3 * 4) → GET_VAR print, 2, 3, 4, MUL, ADD, CALL, POP
    std::shared_ptr<Chunk> mul = compileSource("print(2 + 3 * 4)");
    const std::vector<OpCode> mulExpected = {
        OpCode::GET_VAR, OpCode::CONSTANT, OpCode::CONSTANT, OpCode::CONSTANT,
        OpCode::MUL, OpCode::ADD, OpCode::CALL, OpCode::POP,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*mul) == mulExpected);

    // 1 != 2 → EQUAL, NOT
    std::shared_ptr<Chunk> ne = compileSource("print(1 != 2)");
    const std::vector<OpCode> neExpected = {
        OpCode::GET_VAR, OpCode::CONSTANT, OpCode::CONSTANT,
        OpCode::EQUAL, OpCode::NOT, OpCode::CALL, OpCode::POP,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*ne) == neExpected);

    // <= uses existing comparisons with private operands; unlike GREATER NOT
    // it also handles IEEE NaN correctly.
    std::shared_ptr<Chunk> le = compileSource("print(1 <= 2)");
    validateChunk(*le);
    validateJumps(*le);
}

TEST(compile_short_circuit_and_or) {
    // 1 and 2: DUP сохраняет левый операнд как результат ложного пути
    std::shared_ptr<Chunk> andChunk = compileSource("print(1 and 2)");
    const std::vector<OpCode> andExpected = {
        OpCode::GET_VAR,
        OpCode::CONSTANT, OpCode::DUP, OpCode::JUMP_IF_FALSE, OpCode::POP,
        OpCode::CONSTANT, OpCode::CALL, OpCode::POP,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*andChunk) == andExpected);
    validateJumps(*andChunk);

    // 1 or 2
    std::shared_ptr<Chunk> orChunk = compileSource("print(1 or 2)");
    const std::vector<OpCode> orExpected = {
        OpCode::GET_VAR,
        OpCode::CONSTANT, OpCode::DUP, OpCode::JUMP_IF_FALSE, OpCode::JUMP,
        OpCode::POP, OpCode::CONSTANT, OpCode::CALL, OpCode::POP,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*orChunk) == orExpected);
    validateJumps(*orChunk);
}

TEST(compile_if_statement) {
    std::shared_ptr<Chunk> chunk = compileSource("if 1 < 2 then print(\"y\") end");
    // JUMP_IF_FALSE снимает условие; тело ветви — в собственной области.
    const std::vector<OpCode> expected = {
        OpCode::CONSTANT, OpCode::CONSTANT, OpCode::LESS,
        OpCode::JUMP_IF_FALSE,
        OpCode::ENTER_SCOPE,
        OpCode::GET_VAR, OpCode::CONSTANT, OpCode::CALL, OpCode::POP,
        OpCode::EXIT_SCOPE,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*chunk) == expected);
    validateJumps(*chunk);
}

TEST(compile_while_loop) {
    std::shared_ptr<Chunk> chunk =
        compileSource("let i = 0\nwhile i < 2 do i = i + 1 end");
    const std::vector<OpCode> expected = {
        OpCode::CONSTANT, OpCode::DEFINE_VAR,
        // условие:
        OpCode::GET_VAR, OpCode::CONSTANT, OpCode::LESS,
        OpCode::JUMP_IF_FALSE,
        // тело в своей области:
        OpCode::ENTER_SCOPE,
        OpCode::GET_VAR, OpCode::CONSTANT, OpCode::ADD, OpCode::SET_VAR,
        OpCode::EXIT_SCOPE,
        OpCode::LOOP,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*chunk) == expected);
    validateJumps(*chunk);

    // LOOP должен возвращать на начало условия (первая инструкция условия).
    std::vector<Decoded> decoded = decode(*chunk);
    size_t pos = 0;
    size_t loopTarget = 0;
    for (const Decoded& d : decoded) {
        if (d.op == OpCode::LOOP) loopTarget = pos + 3 - d.operand;
        pos += instructionLength(d.op);
    }
    CHECK_EQ(loopTarget, static_cast<size_t>(2 * 3));  // после let: 2 инструкции по 3 байта
}

TEST(compile_function_and_closure) {
    std::shared_ptr<Chunk> chunk =
        compileSource("function add(a, b)\nreturn a + b\nend");
    const std::vector<OpCode> mainExpected = {
        OpCode::CLOSURE, OpCode::DEFINE_VAR, OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*chunk) == mainExpected);

    // Прототип и имя функции — в пуле констант главной программы
    CHECK_EQ(chunk->constants.size(), 2u);
    auto* proto = std::get_if<std::shared_ptr<FunctionProto>>(&chunk->constants[0]);
    CHECK(proto != nullptr && *proto != nullptr);
    CHECK_EQ((*proto)->name, "add");
    CHECK_EQ((*proto)->params.size(), 2u);
    CHECK_EQ((*proto)->params[0], "a");
    CHECK_EQ((*proto)->params[1], "b");

    // Тело: ENTER_SCOPE, чтение параметров, ADD, RETURN.
    // Явный return в конце тела → неявный NIL+RETURN не добавляется.
    const std::vector<OpCode> bodyExpected = {
        OpCode::ENTER_SCOPE,
        OpCode::GET_VAR, OpCode::GET_VAR, OpCode::ADD, OpCode::RETURN,
    };
    CHECK(opcodes(*(*proto)->body) == bodyExpected);
}

TEST(compile_for_loop) {
    std::shared_ptr<Chunk> chunk = compileSource("for i in 0..3 do print(i) end");
    std::vector<OpCode> ops = opcodes(*chunk);

    // Структурные инструкции цикла for
    size_t enterCount = 0, exitCount = 0, loops = 0, fails = 0, closures = 0;
    for (OpCode op : ops) {
        if (op == OpCode::ENTER_SCOPE) enterCount++;
        if (op == OpCode::EXIT_SCOPE) exitCount++;
        if (op == OpCode::LOOP) loops++;
        if (op == OpCode::FAIL) fails++;
        if (op == OpCode::CLOSURE) closures++;
    }
    CHECK_EQ(enterCount, 3u);   // скрытая область + итерация + тело
    CHECK_EQ(exitCount, 3u);
    CHECK_EQ(loops, 1u);
    CHECK_EQ(fails, 1u);        // проверка шага на ноль
    CHECK_EQ(closures, 0u);

    // Скрытые переменные и сообщение FAIL — в пуле констант
    bool foundHidden = false, foundFailMessage = false, foundUserVar = false;
    for (const Constant& c : chunk->constants) {
        if (auto* s = std::get_if<std::string>(&c)) {
            if (*s == "@i" || *s == "@to" || *s == "@step") foundHidden = true;
            if (*s == "i") foundUserVar = true;
            if (*s == "for loop step cannot be zero") foundFailMessage = true;
        }
    }
    CHECK(foundHidden);
    CHECK(foundUserVar);
    CHECK(foundFailMessage);
    validateJumps(*chunk);
}

TEST(compile_break_continue_targets) {
    std::shared_ptr<Chunk> chunk = compileSource(
        "for i in 0..5 do\n"
        "  if i == 1 then continue end\n"
        "  if i == 3 then break end\n"
        "  print(i)\n"
        "end");
    validateJumps(*chunk);  // все переходы разрешаются в начала инструкций

    // У continue и break есть выход из областей: EXIT_SCOPE >= 3 + 2 от if'ов
    std::vector<OpCode> ops = opcodes(*chunk);
    size_t exits = 0;
    for (OpCode op : ops)
        if (op == OpCode::EXIT_SCOPE) exits++;
    CHECK(exits >= 7u);  // 3 цикла + 2 if-ветки + 2 выхода break/continue
}

TEST(compile_break_outside_loop_defensive) {
    // Компилятор защищается даже без анализатора (в CLI анализатор сработает раньше)
    Lexer lexer("break");
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Compiler compiler;
    bool threw = false;
    try {
        (void)compiler.compile(program);
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("break is only allowed inside a loop"));
    }
    CHECK(threw);
}

TEST(compile_arrays_and_index_assignment) {
    std::shared_ptr<Chunk> chunk =
        compileSource("let a = [1, 2]\na[0] = 5\nprint(a[0])");
    const std::vector<OpCode> expected = {
        // let a = [1, 2]
        OpCode::CONSTANT, OpCode::CONSTANT, OpCode::ARRAY, OpCode::DEFINE_VAR,
        // a[0] = 5
        OpCode::GET_VAR, OpCode::CONSTANT, OpCode::CONSTANT, OpCode::SET_INDEX,
        // print(a[0])
        OpCode::GET_VAR, OpCode::GET_VAR, OpCode::CONSTANT,
        OpCode::GET_INDEX, OpCode::CALL, OpCode::POP,
        OpCode::NIL, OpCode::RETURN,
    };
    CHECK(opcodes(*chunk) == expected);
    validateJumps(*chunk);
}

TEST(disassembler_output) {
    std::shared_ptr<Chunk> chunk = compileSource("let x = 10\nprint(x)");
    std::ostringstream out;
    disassembleChunk(*chunk, "<main>", out);
    std::string text = out.str();

    CHECK(text.find("== <main> ==") != std::string::npos);
    CHECK(text.find("CONSTANT") != std::string::npos);
    CHECK(text.find("DEFINE_VAR") != std::string::npos);
    CHECK(text.find("CALL") != std::string::npos);
    CHECK(text.find("RETURN") != std::string::npos);
    CHECK(text.find("; 10") != std::string::npos);    // комментарий константы
    CHECK(text.find("; x") != std::string::npos);     // имя переменной
    CHECK(text.find("; print") != std::string::npos); // имя вызываемой функции
}

TEST(function_disassembler_shows_nested_proto) {
    std::shared_ptr<Chunk> chunk =
        compileSource("function double(n)\nreturn n * 2\nend");
    std::ostringstream out;
    disassembleChunk(*chunk, "<main>", out);
    std::string text = out.str();

    CHECK(text.find("== function 'double(n)' ==") != std::string::npos);
    CHECK(text.find("CLOSURE") != std::string::npos);
    CHECK(text.find("MUL") != std::string::npos);
}
