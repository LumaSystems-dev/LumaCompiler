#include <memory>
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
#include "../src/vm.h"
#include "test.h"

namespace {

std::shared_ptr<Chunk> compileSource(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    Analyzer analyzer;
    analyzer.analyze(program);
    Compiler compiler;
    return compiler.compile(program);
}

// Компиляция → сериализация → десериализация (полный круг артефакта).
std::shared_ptr<Chunk> roundtrip(const std::string& source) {
    std::shared_ptr<Chunk> chunk = compileSource(source);
    std::ostringstream blob;
    writeChunk(*chunk, blob);
    std::istringstream in(blob.str());
    return readChunk(in);
}

// Исполнение программы через артефакт (compile → write → read → VM).
std::string runViaArtifact(const std::string& source) {
    std::shared_ptr<Chunk> chunk = compileSource(source);
    std::ostringstream blob;
    writeChunk(*chunk, blob);
    std::istringstream in(blob.str());
    std::shared_ptr<Chunk> restored = readChunk(in);

    std::ostringstream out;
    VM vm(out);
    vm.run(*restored);
    return out.str();
}

void appendU32(std::string& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
}

// Минимальный .lbc для негативных тестов. Констант нет: нужные инструкции
// намеренно используют несуществующий индекс, если это требуется тестом.
std::string artifactWithCode(const std::vector<uint8_t>& code,
                             const std::string& suffix = "") {
    std::string out("LBC\x01", 4);
    appendU32(out, static_cast<uint32_t>(code.size()));
    for (uint8_t byte : code) out.push_back(static_cast<char>(byte));
    appendU32(out, static_cast<uint32_t>(code.size()));
    for (size_t i = 0; i < code.size(); ++i) appendU32(out, 1);
    appendU32(out, 0);  // constantCount
    return out + suffix;
}

void checkCorruptArtifact(const std::string& bytes, const std::string& expected) {
    bool threw = false;
    try {
        std::istringstream in(bytes);
        (void)readChunk(in);
    } catch (const LumaError& e) {
        threw = true;
        CHECK(std::string(e.what()).find(expected) != std::string::npos);
    }
    CHECK(threw);
}

}  // namespace

TEST(serialize_roundtrip_disassembly_identical) {
    const std::string sources[] = {
        "let x = 10\nprint(x)",
        "function add(a, b)\nreturn a + b\nend\nprint(add(1, 2))",
        "for i in 0..3 by 2 do\nif i == 2 then break end\nprint(i)\nend",
        "let m = [[1, 2], [3, 4]]\nprint(m[1][0], \"s\" .. 1, nil, true)",
    };
    for (const std::string& source : sources) {
        std::shared_ptr<Chunk> original = compileSource(source);
        std::shared_ptr<Chunk> restored = roundtrip(source);

        std::ostringstream before, after;
        disassembleChunk(*original, "<main>", before);
        disassembleChunk(*restored, "<main>", after);
        CHECK_EQ(after.str(), before.str());
    }
}

TEST(serialize_artifact_executes_like_source) {
    // Исполнение артефакта обязано дать тот же вывод, что и VM на исходнике.
    const std::string sources[] = {
        "let x = 10\nlet y = 20\nprint(x + y)",
        "function fact(n)\nif n <= 1 then return 1 end\nreturn n * fact(n - 1)\nend\n"
        "print(fact(10))",
        "for i in 0..10 by 3 do print(i) end",
        "function makeCounter()\nlet count = 0\nfunction inc()\ncount = count + 1\n"
        "return count\nend\nreturn inc\nend\nlet c = makeCounter()\nprint(c(), c())",
    };
    const std::string expected[] = {"30\n", "3628800\n", "0\n3\n6\n9\n", "1 2\n"};
    for (size_t i = 0; i < 4; ++i) {
        std::shared_ptr<Chunk> chunk = compileSource(sources[i]);
        std::ostringstream out;
        VM vm(out);
        vm.run(*chunk);
        CHECK_EQ(out.str(), expected[i]);

        // и через артефакт — то же самое
        CHECK_EQ(runViaArtifact(sources[i]), expected[i]);
    }
}

TEST(serialize_rejects_bad_magic) {
    std::istringstream in("this is not bytecode at all........");
    bool threw = false;
    try {
        (void)readChunk(in);
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(std::string(e.what()),
                 std::string("not a Luma bytecode file (bad magic)"));
    }
    CHECK(threw);
}

TEST(serialize_rejects_truncated_file) {
    std::shared_ptr<Chunk> chunk = compileSource("let x = 10\nprint(x)");
    std::ostringstream blob;
    writeChunk(*chunk, blob);
    std::string data = blob.str();

    // Обрезаем файл до половины — десериализация обязана упасть, а не упасть
    // молча или вернуть мусор.
    std::istringstream in(data.substr(0, data.size() / 2));
    bool threw = false;
    try {
        (void)readChunk(in);
    } catch (const LumaError& e) {
        threw = true;
        CHECK(std::string(e.what()).find("corrupted bytecode file") == 0);
    }
    CHECK(threw);
}

TEST(serialize_rejects_unsafe_bytecode) {
    // Корректный контейнер .lbc всё ещё может нести опасные инструкции.
    // Они должны быть отвергнуты до запуска VM.
    checkCorruptArtifact(artifactWithCode({0xFF}), "unknown opcode");
    checkCorruptArtifact(artifactWithCode({static_cast<uint8_t>(OpCode::CONSTANT)}),
                         "truncated instruction");
    checkCorruptArtifact(
        artifactWithCode({static_cast<uint8_t>(OpCode::CONSTANT), 0, 0,
                          static_cast<uint8_t>(OpCode::RETURN)}),
        "constant index out of range");
    checkCorruptArtifact(
        artifactWithCode({static_cast<uint8_t>(OpCode::JUMP), 1, 0,
                          static_cast<uint8_t>(OpCode::CONSTANT), 0, 0}),
        "jump target is not an instruction boundary");
    checkCorruptArtifact(
        artifactWithCode({static_cast<uint8_t>(OpCode::DUP),
                          static_cast<uint8_t>(OpCode::NIL),
                          static_cast<uint8_t>(OpCode::RETURN)}),
        "stack underflow");
    checkCorruptArtifact(
        artifactWithCode({static_cast<uint8_t>(OpCode::EXIT_SCOPE),
                          static_cast<uint8_t>(OpCode::NIL),
                          static_cast<uint8_t>(OpCode::RETURN)}),
        "scope stack underflow");
}

TEST(serialize_enforces_size_and_integrity_limits) {
    std::string oversized("LBC\x01", 4);
    appendU32(oversized, 16 * 1024 * 1024 + 1);
    checkCorruptArtifact(oversized, "code section is too large");

    std::shared_ptr<Chunk> chunk = compileSource("print(1)");
    std::ostringstream valid;
    writeChunk(*chunk, valid);
    checkCorruptArtifact(valid.str() + "extra", "trailing data");
}

TEST(vm_validates_chunks_created_outside_deserializer) {
    // Защита нужна и для C++ API: не все Chunk приходят из readChunk().
    Chunk chunk;
    chunk.code = {static_cast<uint8_t>(OpCode::DUP),
                  static_cast<uint8_t>(OpCode::NIL),
                  static_cast<uint8_t>(OpCode::RETURN)};
    chunk.lines = {1, 1, 1};
    std::ostringstream out;
    VM vm(out);
    bool threw = false;
    try {
        vm.run(chunk);
    } catch (const LumaError& e) {
        threw = true;
        CHECK(std::string(e.what()).find("stack underflow") != std::string::npos);
    }
    CHECK(threw);
}

TEST(serialize_rejects_unsupported_version) {
    checkCorruptArtifact(std::string("LBC\x02", 4), "format version");
}

TEST(validator_checks_unused_function_bodies) {
    Chunk main;
    main.writeOp(OpCode::NIL, 0);
    main.writeOp(OpCode::RETURN, 0);
    auto proto = std::make_shared<FunctionProto>();
    proto->body = std::make_shared<Chunk>();
    proto->body->write(255, 0);
    main.constants.push_back(proto);
    CHECK_THROWS(validateChunk(main));
}

TEST(serialize_rejects_every_truncation) {
    auto chunk = compileSource("function f(x)\nreturn x + 1\nend\nprint(f(2))");
    std::ostringstream bytes;
    writeChunk(*chunk, bytes);
    const std::string blob = bytes.str();
    for (size_t length = 0; length < blob.size(); ++length) {
        std::istringstream input(blob.substr(0, length));
        CHECK_THROWS(readChunk(input));
    }
}

TEST(validator_rejects_wrong_constant_types_and_bad_flow) {
    Chunk wrongType;
    wrongType.constants.push_back(1.0);
    wrongType.writeOp(OpCode::GET_VAR, 1);
    wrongType.writeOperand(0, 1);
    wrongType.writeOp(OpCode::RETURN, 1);
    CHECK_THROWS(validateChunk(wrongType));
    checkCorruptArtifact(artifactWithCode({static_cast<uint8_t>(OpCode::LOOP), 255, 255}),
                         "invalid loop offset");
    checkCorruptArtifact(artifactWithCode({static_cast<uint8_t>(OpCode::NIL)}),
                         "falls past code");
    // NIL; LOOP to NIL grows the operand stack every iteration.
    checkCorruptArtifact(artifactWithCode({static_cast<uint8_t>(OpCode::NIL),
                         static_cast<uint8_t>(OpCode::LOOP), 4, 0}), "inconsistent stack");
    std::vector<uint8_t> scopes(129, static_cast<uint8_t>(OpCode::ENTER_SCOPE));
    scopes.push_back(static_cast<uint8_t>(OpCode::NIL));
    scopes.push_back(static_cast<uint8_t>(OpCode::RETURN));
    checkCorruptArtifact(artifactWithCode(scopes), "scope nesting is too deep");
}

TEST(deserializer_bounds_constant_string_and_parameter_sizes) {
    std::string base = artifactWithCode({static_cast<uint8_t>(OpCode::NIL),
                                        static_cast<uint8_t>(OpCode::RETURN)});
    base.resize(base.size() - 4); // replace zero constant count
    std::string constants = base;
    appendU32(constants, 0xFFFFFFFF);
    checkCorruptArtifact(constants, "too many constants");
    std::string string = base;
    appendU32(string, 1);
    string.push_back(2);
    appendU32(string, 0xFFFFFFFF);
    checkCorruptArtifact(string, "string is too large");
    std::string proto = base;
    appendU32(proto, 1);
    proto.push_back(3);
    appendU32(proto, 0); // empty function name
    appendU32(proto, 0xFFFFFFFF);
    checkCorruptArtifact(proto, "too many function parameters");
}
