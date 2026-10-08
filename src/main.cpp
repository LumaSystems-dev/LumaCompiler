#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cctype>

#ifdef _WIN32
// SetConsoleOutputCP объявляем напрямую, без <windows.h>: системные заголовки
// определяют макросы (TokenType, TRUE, ...), конфликтующие с нашим кодом.
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePage);
extern "C" __declspec(dllimport) int __stdcall SetConsoleCP(unsigned int codePage);
constexpr unsigned int CP_UTF8_LUMA = 65001;
#endif

#include "analyzer.h"
#include "ast.h"
#include "chunk.h"
#include "compiler.h"
#include "error.h"
#include "imports.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "util.h"
#include "value.h"
#include "vm.h"

namespace {

// Версия стабилизационного выпуска.
constexpr const char* kVersion = "0.2.0";

// Коды возврата в духе sysexits: 64 — плохой вызов, 65 — ошибка в программе,
// 66 — не открывается файл.
constexpr int EXIT_USAGE = 64;
constexpr int EXIT_COMPILE_ERROR = 65;
constexpr int EXIT_NO_INPUT = 66;
std::string diagnosticPath;

std::string readFile(const std::string& path) {
    diagnosticPath = path;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << path << "\n";
        std::exit(EXIT_NO_INPUT);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// luma tokens <file.luma> — напечатать поток токенов.
int commandTokens(const std::string& path) {
    Lexer lexer(readFile(path));
    for (const Token& token : lexer.tokenize()) {
        std::cout << token.line << "\t" << tokenTypeToString(token.type);
        switch (token.type) {
            case TokenKind::IDENTIFIER:
                std::cout << "(" << token.lexeme << ")";
                break;
            case TokenKind::NUMBER:
                std::cout << "(" << formatNumber(token.number) << ")";
                break;
            case TokenKind::STRING:
                std::cout << "(\"" << token.str << "\")";
                break;
            default:
                break;
        }
        std::cout << "\n";
    }
    return 0;
}

// --- luma ast: печать дерева разбора в виде ветвей ---

std::string nodeLabel(const Expr& expr) {
    if (auto* node = std::get_if<LiteralExpr>(&expr.value)) {
        switch (node->token.type) {
            case TokenKind::NUMBER:   return "Literal " + formatNumber(node->token.number);
            case TokenKind::STRING:   return "Literal \"" + node->token.str + "\"";
            case TokenKind::KW_TRUE:  return "Literal true";
            case TokenKind::KW_FALSE: return "Literal false";
            case TokenKind::KW_NIL:   return "Literal nil";
            default:                  return "Literal";
        }
    }
    if (auto* node = std::get_if<IdentifierExpr>(&expr.value))
        return "Identifier '" + node->name + "'";
    if (std::get_if<GroupingExpr>(&expr.value))
        return "Grouping";
    if (auto* node = std::get_if<UnaryExpr>(&expr.value))
        return "Unary " + node->op.lexeme;
    if (auto* node = std::get_if<BinaryExpr>(&expr.value))
        return "Binary " + node->op.lexeme;
    if (std::get_if<CallExpr>(&expr.value))
        return "Call";
    if (std::get_if<ArrayExpr>(&expr.value))
        return "Array";
    if (std::get_if<IndexExpr>(&expr.value))
        return "Index";
    return "?";  // недостижимо: вариант исчерпывающий
}

// Один ребёнок дерева: выражение или вложенный стейтмент.
struct TreeChild {
    const Expr* expr = nullptr;
    const Stmt* stmt = nullptr;
};

std::vector<TreeChild> exprChildren(const Expr& expr) {
    std::vector<TreeChild> children;
    if (auto* node = std::get_if<UnaryExpr>(&expr.value)) {
        children.push_back({node->operand.get(), nullptr});
    } else if (auto* node = std::get_if<BinaryExpr>(&expr.value)) {
        children.push_back({node->left.get(), nullptr});
        children.push_back({node->right.get(), nullptr});
    } else if (auto* node = std::get_if<GroupingExpr>(&expr.value)) {
        children.push_back({node->expression.get(), nullptr});
    } else if (auto* node = std::get_if<CallExpr>(&expr.value)) {
        children.push_back({node->callee.get(), nullptr});
        for (const Expr& arg : node->args) children.push_back({&arg, nullptr});
    } else if (auto* node = std::get_if<ArrayExpr>(&expr.value)) {
        for (const Expr& element : node->elements) children.push_back({&element, nullptr});
    } else if (auto* node = std::get_if<IndexExpr>(&expr.value)) {
        children.push_back({node->object.get(), nullptr});
        children.push_back({node->index.get(), nullptr});
    }
    return children;
}

std::vector<TreeChild> stmtChildren(const Stmt& stmt) {
    std::vector<TreeChild> children;
    if (auto* node = std::get_if<ExprStmt>(&stmt.value)) {
        children.push_back({node->expression.get(), nullptr});
    } else if (auto* node = std::get_if<LetStmt>(&stmt.value)) {
        children.push_back({node->initializer.get(), nullptr});
    } else if (auto* node = std::get_if<AssignStmt>(&stmt.value)) {
        children.push_back({node->value.get(), nullptr});
    } else if (auto* node = std::get_if<WhileStmt>(&stmt.value)) {
        children.push_back({node->condition.get(), nullptr});
        for (const Stmt& inner : node->body->statements) children.push_back({nullptr, &inner});
    } else if (auto* node = std::get_if<FunctionStmt>(&stmt.value)) {
        for (const Stmt& inner : node->body->statements) children.push_back({nullptr, &inner});
    } else if (auto* node = std::get_if<ReturnStmt>(&stmt.value)) {
        if (node->value) children.push_back({node->value.get(), nullptr});
    }
    return children;
}

std::string stmtLabel(const Stmt& stmt) {
    if (std::get_if<ExprStmt>(&stmt.value)) return "ExprStmt";
    if (auto* node = std::get_if<LetStmt>(&stmt.value))
        return "Let '" + node->name.lexeme + "'";
    if (auto* node = std::get_if<AssignStmt>(&stmt.value)) {
        if (auto* name = std::get_if<IdentifierExpr>(&node->target->value))
            return "Assign '" + name->name + "'";
        return "Assign index";
    }
    if (std::get_if<IfStmt>(&stmt.value)) return "If";
    if (std::get_if<WhileStmt>(&stmt.value)) return "While";
    if (auto* node = std::get_if<ForStmt>(&stmt.value))
        return "For '" + node->variable.lexeme + "'";
    if (auto* node = std::get_if<FunctionStmt>(&stmt.value)) {
        std::string label = "Function '" + node->name.lexeme + "(";
        for (size_t i = 0; i < node->params.size(); ++i) {
            if (i > 0) label += ", ";
            label += node->params[i].lexeme;
        }
        return label + ")'";
    }
    if (std::get_if<ReturnStmt>(&stmt.value)) return "Return";
    if (std::get_if<BreakStmt>(&stmt.value)) return "Break";
    if (std::get_if<ContinueStmt>(&stmt.value)) return "Continue";
    return "?";
}

void printChildren(const std::vector<TreeChild>& children, const std::string& bodyPrefix);
void printStmtChildren(const Stmt& stmt, const std::string& bodyPrefix);

void printTree(const TreeChild& child, const std::string& bodyPrefix, bool last) {
    const std::string childPrefix = bodyPrefix + (last ? "   " : "│  ");
    if (child.expr) {
        std::cout << bodyPrefix << (last ? "└─ " : "├─ ") << nodeLabel(*child.expr) << "\n";
        printChildren(exprChildren(*child.expr), childPrefix);
        return;
    }
    std::cout << bodyPrefix << (last ? "└─ " : "├─ ") << stmtLabel(*child.stmt) << "\n";
    printStmtChildren(*child.stmt, childPrefix);
}

void printChildren(const std::vector<TreeChild>& children, const std::string& bodyPrefix) {
    for (size_t i = 0; i < children.size(); ++i)
        printTree(children[i], bodyPrefix, i + 1 == children.size());
}

// Дети стейтмента; у if — ветви с псевдоузлами if/elseif/else,
// у for — псевдоузлы from/to/by/do.
void printStmtChildren(const Stmt& stmt, const std::string& bodyPrefix) {
    if (auto* node = std::get_if<ForStmt>(&stmt.value)) {
        auto printGroup = [&](const char* label, std::vector<TreeChild> children,
                              bool last) {
            std::cout << bodyPrefix << (last ? "└─ " : "├─ ") << label << "\n";
            printChildren(children, bodyPrefix + (last ? "   " : "│  "));
        };

        std::vector<TreeChild> bodyChildren;
        for (const Stmt& inner : node->body->statements)
            bodyChildren.push_back({nullptr, &inner});

        printGroup("from", {{node->from.get(), nullptr}}, false);
        printGroup("to", {{node->to.get(), nullptr}}, false);
        if (node->step) printGroup("by", {{node->step.get(), nullptr}}, false);
        printGroup("do", std::move(bodyChildren), true);
        return;
    }
    if (auto* node = std::get_if<IfStmt>(&stmt.value)) {
        for (size_t i = 0; i < node->branches.size(); ++i) {
            const IfBranch& branch = node->branches[i];
            bool lastBranch = (i + 1 == node->branches.size());
            const char* label = branch.condition ? (i == 0 ? "if" : "elseif") : "else";
            std::cout << bodyPrefix << (lastBranch ? "└─ " : "├─ ") << label << "\n";

            const std::string branchPrefix = bodyPrefix + (lastBranch ? "   " : "│  ");
            std::vector<TreeChild> children;
            if (branch.condition) children.push_back({branch.condition.get(), nullptr});
            for (const Stmt& inner : branch.body->statements)
                children.push_back({nullptr, &inner});
            printChildren(children, branchPrefix);
        }
        return;
    }
    printChildren(stmtChildren(stmt), bodyPrefix);
}

// luma ast <file.luma> — разобрать программу и показать деревья стейтментов.
int commandAst(const std::string& path) {
    Lexer lexer(readFile(path));
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();

    for (size_t i = 0; i < program.statements.size(); ++i) {
        if (i > 0) std::cout << "\n";
        const Stmt& stmt = program.statements[i];
        std::cout << stmtLabel(stmt) << "\n";  // корень без коннектора
        printStmtChildren(stmt, "");
    }
    return 0;
}

// luma eval <file.luma> — вычислить единственное выражение и напечатать
// значение. Вспомогательная команда этапа выражений.
int commandEval(const std::string& path) {
    Lexer lexer(readFile(path));
    Parser parser(lexer.tokenize());
    Expr ast = parser.parseSingleExpression();

    Interpreter interpreter;
    std::cout << valueToString(interpreter.eval(ast)) << "\n";
    return 0;
}

// luma bytecode <file.luma> — скомпилировать программу и напечатать байткод.
int commandBytecode(const std::string& path) {
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".lbc") {
        std::ifstream in(path, std::ios::binary);
        if (!in) { std::cerr << "Cannot open file: " << path << "\n"; return EXIT_NO_INPUT; }
        auto chunk = readChunk(in);
        disassembleChunk(*chunk, "<main>", std::cout);
        return 0;
    }
    Lexer lexer(readFile(path));
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    program = resolveImports(std::move(program), path);

    Analyzer analyzer;
    analyzer.analyze(program);

    Compiler compiler;
    std::shared_ptr<Chunk> chunk = compiler.compile(program);
    disassembleChunk(*chunk, "<main>", std::cout);
    return 0;
}

// luma run <file.luma> — выполнить программу.
// Полный конвейер: лексер → парсер → РАЗРЕШЕНИЕ IMPORT'ОВ → анализ → интерпретация.
int commandRun(const std::string& path) {
    Lexer lexer(readFile(path));
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    program = resolveImports(std::move(program), path);

    Analyzer analyzer;
    analyzer.analyze(program);  // семантические ошибки — до исполнения

    Interpreter interpreter;
    interpreter.run(program);
    return 0;
}

// luma vm <file.luma> — выполнить программу на стековой VM.
// Тот же фронтенд, но исполнение — байткод вместо AST.
int commandVm(const std::string& path) {
    Lexer lexer(readFile(path));
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    // VM использует тот же фронтенд, что и интерпретатор и `compile`:
    // ImportStmt не является байткодовой инструкцией и должен быть развёрнут
    // до семантического анализа.
    program = resolveImports(std::move(program), path);

    Analyzer analyzer;
    analyzer.analyze(program);

    Compiler compiler;
    std::shared_ptr<Chunk> chunk = compiler.compile(program);

    VM vm;
    vm.run(*chunk);
    return 0;
}

// Компиляция в файл-артефакт .lbc (конвейер до компилятора включительно).
// resolveImports вклеивает модули ДО анализа: .lbc самодостаточен.
std::shared_ptr<Chunk> compileFile(const std::string& path) {
    Lexer lexer(readFile(path));
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    program = resolveImports(std::move(program), path);

    Analyzer analyzer;
    analyzer.analyze(program);

    Compiler compiler;
    return compiler.compile(program);
}

// Артефакт по умолчанию: hello.luma → hello.lbc.
std::string defaultArtifactPath(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path.substr(0, dot) + ".lbc";
    return path + ".lbc";
}

// luma compile <input.luma> [output.lbc] — скомпилировать в файл-артефакт.
int commandCompile(const std::string& input, const std::string& output) {
    std::shared_ptr<Chunk> chunk = compileFile(input);

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file: " << output << "\n";
        return EXIT_NO_INPUT;
    }
    writeChunk(*chunk, out);

    std::cout << "compiled " << input << " -> " << output << "\n";
    return 0;
}

// luma execute <file.lbc> — исполнить скомпилированный артефакт.
// Фронтенд (лексер/парсер/анализатор) не нужен: байткод уже проверен.
int commandExecute(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open file: " << path << "\n";
        return EXIT_NO_INPUT;
    }
    std::shared_ptr<Chunk> chunk = readChunk(in);

    VM vm;
    vm.run(*chunk);
    return 0;
}

void printUsage() {
    std::cerr << "Luma " << kVersion << "\n"
              << "Usage:\n"
              << "  luma run <file.luma>      execute a program (tree-walking)\n"
              << "  luma vm <file.luma>       execute a program (bytecode VM)\n"
              << "  luma compile <in.luma> [out.lbc]  compile to a bytecode file\n"
              << "  luma execute <file.lbc>   execute a compiled bytecode file\n"
              << "  luma tokens <file.luma>   print the token stream\n"
              << "  luma ast <file.luma>      print the AST of the program\n"
              << "  luma bytecode <file.luma|file.lbc> print the bytecode\n"
              << "  luma eval <file.luma>     evaluate a single expression\n"
              << "  luma --help               show this help\n"
              << "  luma --version            print the version\n";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Исходники и строки — UTF-8; консоль Windows по умолчанию ждёт другую кодировку.
    SetConsoleOutputCP(CP_UTF8_LUMA);
    SetConsoleCP(CP_UTF8_LUMA);
#endif

    try {
        if (argc >= 2) {
            std::string command = argv[1];
            if ((command == "--version" || command == "-v") && argc == 2) {
                std::cout << "Luma " << kVersion << "\n";
                return 0;
            }
            if ((command == "--help" || command == "-h") && argc == 2) {
                printUsage();
                return 0;
            }
            if (command == "run" && argc == 3) return commandRun(argv[2]);
            if (command == "vm" && argc == 3) return commandVm(argv[2]);
            if (command == "tokens" && argc == 3) return commandTokens(argv[2]);
            if (command == "ast" && argc == 3) return commandAst(argv[2]);
            if (command == "bytecode" && argc == 3) return commandBytecode(argv[2]);
            if (command == "eval" && argc == 3) return commandEval(argv[2]);
            if (command == "compile" && (argc == 3 || argc == 4)) {
                std::string output =
                    argc == 4 ? argv[3] : defaultArtifactPath(argv[2]);
                return commandCompile(argv[2], output);
            }
            if (command == "execute" && argc == 3) return commandExecute(argv[2]);

            // Без подкоманды: "luma program.luma" ≡ vm, "luma program.lbc" ≡ execute.
            if (argc == 2) {
                if (command.size() < 4 || (command.substr(command.size() - 4) != ".lbc" &&
                    (command.size() < 5 || command.substr(command.size() - 5) != ".luma"))) {
                    std::cerr << "Unknown command or unsupported file: " << command << "\n";
                    return EXIT_USAGE;
                }
                bool isArtifact = command.size() >= 4 &&
                                  command.compare(command.size() - 4, 4, ".lbc") == 0;
                return isArtifact ? commandExecute(command) : commandVm(command);
            }
        }
    } catch (const LumaError& error) {
        std::string path = error.file.empty() ? diagnosticPath : error.file;
        if (!path.empty() && error.line > 0) {
            std::cerr << path << ":" << error.line;
            if (error.column > 0) std::cerr << ":" << error.column;
            std::cerr << "\n";
            std::ifstream source(path);
            std::string text;
            for (int i = 0; i < error.line && std::getline(source, text); ++i) {
                if (i + 1 == error.line) {
                    std::cerr << "    " << text << "\n";
                    if (error.column > 0 && static_cast<size_t>(error.column) <= text.size() + 1)
                        std::cerr << "    " << std::string(error.column - 1, ' ') << "^\n";
                }
            }
        }
        // Ошибки фаз с номером строки; файловые/сериализационные — без неё.
        if (error.line > 0)
            std::cerr << "Error at line " << error.line << ": " << error.what() << "\n";
        else
            std::cerr << "Error: " << error.what() << "\n";
        return EXIT_COMPILE_ERROR;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return EXIT_COMPILE_ERROR;
    }

    printUsage();
    return EXIT_USAGE;
}
