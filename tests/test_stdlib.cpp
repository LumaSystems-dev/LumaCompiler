#include <filesystem>
#include <sstream>
#include <string>

#include "../src/analyzer.h"
#include "../src/compiler.h"
#include "../src/imports.h"
#include "../src/lexer.h"
#include "../src/natives.h"
#include "../src/parser.h"
#include "../src/vm.h"
#include "test.h"

namespace {
struct LibraryInput {
    std::istringstream stream;
    std::istream* saved;
    explicit LibraryInput(const std::string& text) : stream(text), saved(inputStream()) {
        inputStream() = &stream;
    }
    ~LibraryInput() { inputStream() = saved; }
};

std::string runLibrary(const std::string& body, int mode, const std::string& input = "") {
    LibraryInput fake(input);
    const std::string imports =
        "import \"../stdlib/math.luma\"\n"
        "import \"../stdlib/string.luma\"\n"
        "import \"../stdlib/random.luma\"\n"
        "import \"../stdlib/io.luma\"\n";
    Lexer lexer(imports + body);
    Parser parser(lexer.tokenize());
    Program program = resolveImports(parser.parseProgram(),
        (std::filesystem::current_path() / "tests" / "stdlib_test.luma").string());
    Analyzer analyzer;
    analyzer.analyze(program);
    std::ostringstream output;
    if (mode == 0) {
        Interpreter interpreter(output);
        interpreter.run(program);
    } else {
        Compiler compiler;
        auto chunk = compiler.compile(program);
        if (mode == 2) {
            std::stringstream artifact(std::ios::in | std::ios::out | std::ios::binary);
            writeChunk(*chunk, artifact);
            artifact.seekg(0);
            chunk = readChunk(artifact);
        }
        VM vm(output);
        vm.run(*chunk);
    }
    return output.str();
}

void checkLibrary(const std::string& source, const std::string& expected,
                  const std::string& input = "") {
    for (int mode = 0; mode < 3; ++mode)
        CHECK_EQ(runLibrary(source, mode, input), expected);
}

void checkLibraryError(const std::string& source, const std::string& message,
                       const std::string& input = "") {
    for (int mode = 0; mode < 3; ++mode) {
        bool caught = false;
        try { (void)runLibrary(source, mode, input); }
        catch (const LumaError& error) {
            caught = true;
            CHECK(std::string(error.what()).find(message) != std::string::npos);
        }
        CHECK(caught);
    }
}
}

TEST(stdlib_math_finite_helpers) {
    checkLibrary("print(abs(-4), abs(0), min(3,-1), max(-3,-1))\n"
                 "print(clamp(-2,0,5), clamp(8,0,5), clamp(3,0,5), clamp(8,2,2))",
                 "4 0 -1 -1\n0 5 3 2\n");
}

TEST(stdlib_math_pow_and_sqrt) {
    checkLibrary("print(pow(2,10), pow(4,0.5), pow(2,-3), pow(-2,3), pow(0,0))\n"
                 "print(sqrt(0), sqrt(9), sqrt(0.25))", "1024 2 0.125 -8 1\n0 3 0.5\n");
}

TEST(stdlib_math_invalid_types_and_domains) {
    checkLibraryError("abs(\"3\")", "must be a number");
    checkLibraryError("min(1,nil)", "must be a number");
    checkLibraryError("max(1,0/0)", "must be finite");
    checkLibraryError("abs(1/0)", "must be finite");
    checkLibraryError("clamp(1,3,2)", "lower bound");
    checkLibraryError("sqrt(-1)", "nonnegative");
    checkLibraryError("sqrt(false)", "must be a number");
    checkLibraryError("pow(-1,0.5)", "finite real-number domain");
    checkLibraryError("pow(10,400)", "finite real-number domain");
    checkLibraryError("pow(0,-1)", "finite real-number domain");
}

TEST(stdlib_string_search_and_predicates) {
    checkLibrary("print(find(\"banana\",\"ana\"), find(\"abc\",\"z\"), find(\"\",\"\"))\n"
                 "print(contains(\"abc\",\"\"), starts_with(\"abc\",\"a\"), ends_with(\"abc\",\"bc\"))\n"
                 "print(starts_with(\"a\",\"ab\"), ends_with(\"a\",\"ab\"), find(\"a\",\"ab\"))",
                 "1 -1 0\ntrue true true\nfalse false -1\n");
}

TEST(stdlib_string_slices_and_unicode_bytes) {
    checkLibrary("print(substr(\"abc\",0,3), substr(\"abc\",3,0) == \"\")\n"
                 "print(string_length(\"Лума\"), substr(\"Лума\",0,2), find(\"Лума\",\"ма\"))",
                 "abc true\n8 Л 4\n");
}

TEST(stdlib_string_trim_and_join) {
    checkLibrary("print(trim(\" \\tHello\\r\\n\"), trim(\" \\n\") == \"\")\n"
                 "print(join([\"a\",\"b\",\"\"],\"|\"), join([],\",\") == \"\")",
                 "Hello true\na|b| true\n");
}

TEST(stdlib_string_rejects_invalid_ranges) {
    checkLibraryError("substr(\"abc\",-1,1)", "nonnegative integers");
    checkLibraryError("substr(\"abc\",0.5,1)", "nonnegative integers");
    checkLibraryError("substr(\"abc\",2,2)", "out of bounds");
    checkLibraryError("substr(\"abc\",0,0/0)", "must be finite");
    checkLibraryError("substr(\"abc\",1/0,0)", "must be finite");
}

TEST(stdlib_string_rejects_invalid_types) {
    checkLibraryError("substr([],0,0)", "expects a string");
    checkLibraryError("find(1,\"a\")", "expects a string");
    checkLibraryError("find(\"a\",[])", "expects a string");
    checkLibraryError("join(\"\",\",\")", "expects an array");
    checkLibraryError("join([],0)", "separator must be a string");
    checkLibraryError("join([\"a\",2],\",\")", "elements must be strings");
}

TEST(stdlib_random_known_sequence) {
    checkLibrary("random_seed(1)\nprint(_random_next(),_random_next(),_random_next(),_random_next())",
                 "16806 282475248 1622650072 984943657\n");
    // With this seed and span the first two samples belong to the rejected
    // tail; the third must be accepted. Test the rejection path explicitly.
    checkLibrary("random_seed(2147483646)\nprint(random_range(0,1073741823))\nprint(_random_state)",
                 "524833573\n524833574\n");
}

TEST(stdlib_random_reseed_and_bounds) {
    checkLibrary("random_seed(123)\nlet first = random()\nrandom_seed(123)\n"
                 "print(first == random(), random_range(7,7))\n"
                 "for i in 0..1000 do\nlet r = random()\nassert(r >= 0 and r < 1,\"random bound\")\n"
                 "let n = random_range(-5,5)\nassert(n >= -5 and n <= 5 and n % 1 == 0,\"range bound\")\nend\nprint(\"ok\")",
                 "true 7\nok\n");
}

TEST(stdlib_random_invalid_seed_and_bounds) {
    checkLibraryError("random_seed(0)", "random seed must be an integer");
    checkLibraryError("random_seed(2147483647)", "random seed must be an integer");
    checkLibraryError("random_seed(1.5)", "random seed must be an integer");
    checkLibraryError("random_seed(\"1\")", "must be a number");
    checkLibraryError("random_range(2,1)", "bounds are invalid");
    checkLibraryError("random_range(0,1.5)", "must be integers");
    checkLibraryError("random_range(0,1/0)", "bounds are invalid");
    checkLibraryError("random_range(0/0,1)", "bounds are invalid");
    checkLibraryError("random_range(-2147483646,2147483646)", "span is too large");
    checkLibraryError("random_range(\"0\",1)", "must be numbers");
}

TEST(stdlib_io_prompt_and_read_number) {
    checkLibrary("print(prompt(\"name?\"))\nprint(read_number())", "name?\nЛума\n-12.5\n", "Лума\n-12.5\n");
    checkLibrary("print(prompt(\"name?\") == nil, read_number() == nil)", "name?\ntrue true\n");
    checkLibrary("print(prompt(\"name?\") == \"\")", "name?\ntrue\n", "\n");
}

TEST(stdlib_io_invalid_input_is_normal_error) {
    checkLibraryError("read_number()", "cannot convert", "hello\n");
    checkLibraryError("read_number()", "cannot convert", "\n");
    checkLibraryError("prompt(1)", "message must be a string");
}

TEST(stdlib_type_and_assert_builtins) {
    checkLibrary("print(type(nil),type(false),type(1),type(\"\"),type([]),type(print))\n"
                 "assert(0,\"zero is truthy\")\nprint(assert(true,\"ok\"))",
                 "nil boolean number string array function\nnil\n");
    checkLibraryError("assert(false,\"custom failure\")", "custom failure");
    checkLibraryError("assert(true,1)", "message must be a string");
}

TEST(stdlib_static_and_dynamic_native_arity) {
    checkLibraryError("_luma_slice(\"x\",0)", "expects 3 arguments");
    checkLibraryError("let f = _luma_slice\nf(\"x\",0)", "expects 3 arguments");
    checkLibraryError("let f = _luma_pow\nf(2)", "expects 2 arguments");
    checkLibraryError("sqrt()", "expects 1 arguments");
    checkLibraryError("type()", "expects 1 arguments");
}

TEST(stdlib_records_existing_string_index_limitation) {
    // Slicing cannot be implemented through the existing [] operator:
    // immutable strings are not indexable, unlike arrays.
    checkLibraryError("let text = \"abc\"\nprint(text[0])", "is not indexable");
    checkLibrary("print(len([]) == len(\"\"), type([]) != type(\"\"))", "true true\n");
}

TEST(stdlib_all_native_definitions_check_runtime_arity) {
    for (const BuiltinDefinition& builtin : builtinDefinitions()) {
        if (!builtin.arity || *builtin.arity == 0) continue;
        // A variable callee bypasses Analyzer's static arity check and reaches
        // the native implementation in every backend, including .lbc.
        std::string source = std::string("let f = ") + builtin.name + "\nf()";
        checkLibraryError(source, "expects " + std::to_string(*builtin.arity) + " arguments");
    }
}

TEST(stdlib_native_numeric_and_slice_edges) {
    checkLibrary("assert(sqrt(num(\"1e300\")) == num(\"1e150\"),\"large sqrt\")\n"
                 "assert(abs(sqrt(3) - pow(3,0.5)) < 0.0000000001,\"sqrt precision\")\n"
                 "random_seed(2147483646)\nprint(_random_next())\n"
                 "let a = random_range(1,2147483646)\n"
                 "assert(a >= 1 and a <= 2147483646,\"maximum span\")",
                 "2147466839\n");
    checkLibraryError("sqrt(0/0)", "must be finite");
    checkLibraryError("sqrt(1/0)", "must be finite");
    checkLibraryError("pow(\"2\",1)", "must be a number");
    checkLibraryError("pow(2,0/0)", "must be finite");
    checkLibraryError("substr(\"abc\",[],1)", "must be a number");
    checkLibraryError("substr(\"abc\",0,-1)", "nonnegative integers");
}
