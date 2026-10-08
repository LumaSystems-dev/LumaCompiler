#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/analyzer.h"
#include "../src/ast.h"
#include "../src/chunk.h"
#include "../src/compiler.h"
#include "../src/error.h"
#include "../src/imports.h"
#include "../src/interpreter.h"
#include "../src/lexer.h"
#include "../src/natives.h"
#include "../src/parser.h"
#include "../src/vm.h"
#include "test.h"

namespace fs = std::filesystem;

namespace {

// Каталог с файлами одного теста: создаётся чистым, удаляется при выходе.
class ModuleDir {
public:
    explicit ModuleDir(const std::string& name) {
        base_ = fs::temp_directory_path() / "luma_import_tests" / name;
        fs::remove_all(base_);
        fs::create_directories(base_);
    }
    ~ModuleDir() { fs::remove_all(base_); }

    void writeFile(const std::string& relPath, const std::string& content) {
        fs::path file = base_ / relPath;
        if (file.has_parent_path()) fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << content;
    }

    void removeFile(const std::string& relPath) { fs::remove(base_ / relPath); }
    bool fileExists(const std::string& relPath) const {
        return fs::exists(base_ / relPath);
    }

    std::string path(const std::string& relPath) const {
        return (base_ / relPath).string();
    }

private:
    fs::path base_;
};

// Полный конвейер CLI для файла-программы: интерпретатор.
std::string runFileInterpreted(const std::string& mainPath) {
    std::ifstream in(mainPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    Lexer lexer(buffer.str());
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    program = resolveImports(std::move(program), mainPath);
    Analyzer analyzer;
    analyzer.analyze(program);
    std::ostringstream out;
    Interpreter interpreter(out);
    interpreter.run(program);
    return out.str();
}

// Полный конвейер CLI для файла-программы: bytecode VM.
std::string runFileVM(const std::string& mainPath) {
    std::ifstream in(mainPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    Lexer lexer(buffer.str());
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    program = resolveImports(std::move(program), mainPath);
    Analyzer analyzer;
    analyzer.analyze(program);
    Compiler compiler;
    std::shared_ptr<Chunk> chunk = compiler.compile(program);
    std::ostringstream out;
    VM vm(out);
    vm.run(*chunk);
    return out.str();
}

// Оба бэкенда обязаны дать одинаковый вывод.
std::string runBothFile(const std::string& mainPath) {
    std::string interpreted = runFileInterpreted(mainPath);
    std::string vm = runFileVM(mainPath);
    CHECK_EQ(vm, interpreted);
    return interpreted;
}

}  // namespace

TEST(import_function_from_module) {
    ModuleDir dir("import_function");
    dir.writeFile("math.luma",
                  "function add(a, b)\n    return a + b\nend\n\n"
                  "function square(x)\n    return x * x\nend\n");
    dir.writeFile("main.luma",
                  "import \"math.luma\"\n\nprint(add(5, 3))\nprint(square(4))\n");

    CHECK_EQ(runBothFile(dir.path("main.luma")), "8\n16\n");
}

TEST(import_several_modules) {
    ModuleDir dir("import_several");
    dir.writeFile("a.luma", "function fa()\nreturn 1\nend\n");
    dir.writeFile("b.luma", "function fb()\nreturn 2\nend\n");
    dir.writeFile("main.luma",
                  "import \"a.luma\"\nimport \"b.luma\"\nprint(fa() + fb())\n");
    CHECK_EQ(runBothFile(dir.path("main.luma")), "3\n");
}

TEST(import_nested_and_deduplicated) {
    // b.luma импортирует a.luma, и main импортирует оба. Без дедупликации
    // a.luma вклеился бы дважды → "Variable 'add' is already defined".
    ModuleDir dir("import_nested_dedup");
    dir.writeFile("a.luma", "function add(x, y)\nreturn x + y\nend\n");
    dir.writeFile("b.luma",
                  "import \"a.luma\"\nfunction twice(x)\nreturn add(x, x)\nend\n");
    dir.writeFile("main.luma",
                  "import \"a.luma\"\nimport \"b.luma\"\nprint(twice(21))\n");
    CHECK_EQ(runBothFile(dir.path("main.luma")), "42\n");
}

TEST(import_from_subdirectory) {
    ModuleDir dir("import_subdir");
    dir.writeFile("lib/math.luma", "function square(x)\nreturn x * x\nend\n");
    dir.writeFile("main.luma",
                  "import \"lib/math.luma\"\nprint(square(4))\n");
    CHECK_EQ(runBothFile(dir.path("main.luma")), "16\n");
}

TEST(import_relative_to_importing_file) {
    // "a.luma" внутри lib/b.luma разрешается относительно lib/, а не корня.
    ModuleDir dir("import_relative");
    dir.writeFile("a.luma", "function who()\nreturn \"root\"\nend\n");
    dir.writeFile("lib/a.luma", "function who()\nreturn \"lib\"\nend\n");
    dir.writeFile("lib/b.luma", "import \"a.luma\"\nfunction call()\nreturn who()\nend\n");
    dir.writeFile("main.luma", "import \"lib/b.luma\"\nprint(call())\n");
    CHECK_EQ(runBothFile(dir.path("main.luma")), "lib\n");
}

TEST(import_missing_file) {
    ModuleDir dir("import_missing");
    dir.writeFile("main.luma", "import \"missing.luma\"\nprint(1)\n");
    bool threw = false;
    try {
        (void)runFileInterpreted(dir.path("main.luma"));
    } catch (const LumaError& e) {
        threw = true;
        CHECK(std::string(e.what()).find("imported file 'missing.luma' not found") !=
              std::string::npos);
    }
    CHECK(threw);
}

TEST(import_requires_string_literal) {
    ModuleDir dir("import_syntax");
    dir.writeFile("main.luma", "import math\n");
    bool threw = false;
    try {
        (void)runFileInterpreted(dir.path("main.luma"));
    } catch (const LumaError& e) {
        threw = true;
        CHECK(std::string(e.what()).find("string literal") != std::string::npos);
    }
    CHECK(threw);

    // import; — тоже синтаксическая ошибка
    dir.writeFile("main2.luma", "import;\n");
    threw = false;
    try {
        (void)runFileInterpreted(dir.path("main2.luma"));
    } catch (const LumaError& e) {
        threw = true;
    }
    CHECK(threw);
}

TEST(import_circular_detected) {
    ModuleDir dir("import_circular");
    dir.writeFile("a.luma", "import \"b.luma\"\nfunction fa()\nreturn 1\nend\n");
    dir.writeFile("b.luma", "import \"a.luma\"\nfunction fb()\nreturn 2\nend\n");
    dir.writeFile("main.luma", "import \"a.luma\"\nprint(fa() + fb())\n");
    bool threw = false;
    try {
        (void)runFileInterpreted(dir.path("main.luma"));
    } catch (const LumaError& e) {
        threw = true;
        std::string message = e.what();
        CHECK(message.find("circular import detected") != std::string::npos);
        // Цепочка видна целиком: a.luma -> b.luma -> a.luma
        CHECK(message.find("a.luma -> b.luma -> a.luma") != std::string::npos);
    }
    CHECK(threw);
}

TEST(import_name_conflict_detected) {
    ModuleDir dir("import_conflict");
    dir.writeFile("a.luma", "function test()\nreturn 1\nend\n");
    dir.writeFile("b.luma", "function test()\nreturn 2\nend\n");
    dir.writeFile("main.luma", "import \"a.luma\"\nimport \"b.luma\"\nprint(test())\n");
    bool threw = false;
    try {
        (void)runBothFile(dir.path("main.luma"));
    } catch (const LumaError& e) {
        threw = true;
        std::string message = e.what();
        CHECK(message.find("main.luma -> b.luma:") != std::string::npos);
        CHECK(message.find("Variable 'test' is already defined") != std::string::npos);
    }
    CHECK(threw);
}

TEST(import_error_inside_imported_file_reported) {
    ModuleDir dir("import_broken_module");
    dir.writeFile("math.luma", "let ok = 1\nlet broken = ;\n");
    dir.writeFile("main.luma", "import \"math.luma\"\nprint(ok)\n");
    bool threw = false;
    try {
        (void)runFileInterpreted(dir.path("main.luma"));
    } catch (const LumaError& e) {
        threw = true;
        std::string message = e.what();
        CHECK(message.find("in imported file 'math.luma':") != std::string::npos);
    }
    CHECK(threw);
}

TEST(import_analyzer_error_shows_module_chain) {
    ModuleDir dir("import_analyzer_context");
    dir.writeFile("lib/leaf.luma", "print(missing)\n");
    dir.writeFile("middle.luma", "import \"lib/leaf.luma\"\n");
    dir.writeFile("main.luma", "import \"middle.luma\"\n");
    bool threw = false;
    try {
        (void)runFileInterpreted(dir.path("main.luma"));
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(e.line, 1);
        std::string message = e.what();
        CHECK(message.find("middle.luma -> lib/leaf.luma") != std::string::npos);
        CHECK(message.find("Unknown variable 'missing'") != std::string::npos);
    }
    CHECK(threw);
}

TEST(import_canonical_alias_is_loaded_once) {
    ModuleDir dir("canonical_alias");
    dir.writeFile("lib/module.luma", "print(\"loaded\")\n");
    dir.writeFile("main.luma", "import \"lib/module.luma\"\nimport \"lib/../lib/module.luma\"\n");
    CHECK_EQ(runBothFile(dir.path("main.luma")), "loaded\n");
}

TEST(imported_syntax_error_preserves_location) {
    ModuleDir dir("import_syntax_location");
    dir.writeFile("leaf.luma", "let x = ;");
    dir.writeFile("middle.luma", "import \"leaf.luma\"");
    dir.writeFile("main.luma", "import \"middle.luma\"");
    bool caught = false;
    try {
        (void)runFileVM(dir.path("main.luma"));
    } catch (const LumaError& error) {
        caught = true;
        CHECK_EQ(error.file, dir.path("leaf.luma"));
        CHECK_EQ(error.line, 1);
        CHECK_EQ(error.column, 9);
    }
    CHECK(caught);
}

TEST(imported_module_can_call_input) {
    ModuleDir dir("import_input");
    dir.writeFile("ask.luma", "function askName()\nreturn input()\nend\n");
    dir.writeFile("main.luma", "import \"ask.luma\"\nprint(askName())\n");
    for (bool useVm : {false, true}) {
        std::istringstream fake("Модульный герой\n");
        std::istream* previous = inputStream();
        inputStream() = &fake;
        try {
            std::string output = useVm ? runFileVM(dir.path("main.luma"))
                                       : runFileInterpreted(dir.path("main.luma"));
            CHECK_EQ(output, "Модульный герой\n");
        } catch (...) {
            inputStream() = previous;
            throw;
        }
        inputStream() = previous;
    }
}

TEST(import_only_at_top_level) {
    ModuleDir dir("import_nested_position");
    dir.writeFile("x.luma", "print(1)\n");
    dir.writeFile("main.luma", "if true then import \"x.luma\" end\n");
    bool threw = false;
    try {
        (void)runFileInterpreted(dir.path("main.luma"));
    } catch (const LumaError& e) {
        threw = true;
        CHECK(std::string(e.what()).find("only allowed at the top level") !=
              std::string::npos);
    }
    CHECK(threw);
}

TEST(artifact_is_self_sufficient_after_import) {
    // ИНТЕГРАЦИОННЫЙ тест: compile → .lbc → удалить исходники → execute.
    // .lbc не должен требовать импортированные .luma файлы.
    ModuleDir dir("import_lbc_selfsufficient");
    dir.writeFile("math.luma", "function add(a, b)\nreturn a + b\nend\n");
    dir.writeFile("main.luma",
                  "import \"math.luma\"\nprint(add(10, 20))\n");

    // compile main.luma → main.lbc (тот же конвейер, что luma compile)
    std::string source;
    {
        std::ifstream in(dir.path("main.luma"), std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        source = buffer.str();  // поток закрыт до удаления файла (Windows)
    }
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    program = resolveImports(std::move(program), dir.path("main.luma"));
    Analyzer analyzer;
    analyzer.analyze(program);
    Compiler compiler;
    std::shared_ptr<Chunk> chunk = compiler.compile(program);
    {
        std::ofstream artifact(dir.path("main.lbc"), std::ios::binary);
        writeChunk(*chunk, artifact);
    }

    // Удаляем ВСЕ исходники — артефакт обязан работать без них.
    dir.removeFile("main.luma");
    dir.removeFile("math.luma");
    CHECK(!dir.fileExists("math.luma"));

    std::ifstream artifactIn(dir.path("main.lbc"), std::ios::binary);
    std::shared_ptr<Chunk> restored = readChunk(artifactIn);
    std::ostringstream out;
    VM vm(out);
    vm.run(*restored);
    CHECK_EQ(out.str(), "30\n");
}
