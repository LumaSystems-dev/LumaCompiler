#include "imports.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

#include "error.h"
#include "lexer.h"
#include "parser.h"

namespace fs = std::filesystem;

namespace {

// Поиск import НЕ на верхнем уровне (внутри if/while/for/function).
const ImportStmt* findNestedImport(const Stmt& stmt);

// После склейки AST теряет границу файлов. Сохраняем происхождение statement,
// чтобы Analyzer мог назвать модуль и цепочку import в своей диагностике.
void annotateOriginIn(Block& block, const std::string& origin);
void annotateOrigin(Stmt& stmt, const std::string& origin) {
    if (stmt.origin.empty()) stmt.origin = origin;
    if (auto* node = std::get_if<IfStmt>(&stmt.value)) {
        for (IfBranch& branch : node->branches) annotateOriginIn(*branch.body, origin);
    } else if (auto* node = std::get_if<WhileStmt>(&stmt.value)) {
        annotateOriginIn(*node->body, origin);
    } else if (auto* node = std::get_if<ForStmt>(&stmt.value)) {
        annotateOriginIn(*node->body, origin);
    } else if (auto* node = std::get_if<FunctionStmt>(&stmt.value)) {
        annotateOriginIn(*node->body, origin);
    }
}
void annotateOriginIn(Block& block, const std::string& origin) {
    for (Stmt& stmt : block.statements) annotateOrigin(stmt, origin);
}
void annotateOrigin(Program& program, const std::string& origin) {
    for (Stmt& stmt : program.statements) annotateOrigin(stmt, origin);
}

const ImportStmt* findNestedImportIn(const Block& block) {
    for (const Stmt& stmt : block.statements)
        if (const ImportStmt* found = findNestedImport(stmt)) return found;
    return nullptr;
}
const ImportStmt* findNestedImport(const Stmt& stmt) {
    if (auto* node = std::get_if<IfStmt>(&stmt.value)) {
        for (const IfBranch& branch : node->branches)
            if (const ImportStmt* found = findNestedImportIn(*branch.body))
                return found;
    } else if (auto* node = std::get_if<WhileStmt>(&stmt.value)) {
        if (const ImportStmt* found = findNestedImportIn(*node->body)) return found;
    } else if (auto* node = std::get_if<ForStmt>(&stmt.value)) {
        if (const ImportStmt* found = findNestedImportIn(*node->body)) return found;
    } else if (auto* node = std::get_if<FunctionStmt>(&stmt.value)) {
        if (const ImportStmt* found = findNestedImportIn(*node->body)) return found;
    } else if (auto* import = std::get_if<ImportStmt>(&stmt.value)) {
        return import;  // для верхнеуровневых узлов вызывающий не зовёт этот ход
    }
    return nullptr;
}

class Importer {
public:
    explicit Importer(const fs::path& root) {
        chainCanonical_.push_back(fs::weakly_canonical(root).string());
        chainDisplay_.push_back(root.filename().string());
    }
    // display — имя файла для сообщений (корень: имя файла; модуль: путь,
    // как записан в import).
    Program process(Program program, const fs::path& filePath,
                    const std::string& display) {
        (void)display;  // у корневой программы нет import-контекста
        std::string origin;
        if (chainDisplay_.size() > 1) {
            for (size_t i = 0; i < chainDisplay_.size(); ++i) {
                if (i > 0) origin += " -> ";
                origin += chainDisplay_[i];
            }
            annotateOrigin(program, origin);
            for (Stmt& stmt : program.statements) stmt.sourceFile = filePath.string();
        }
        fs::path dir = filePath.parent_path();
        if (dir.empty()) dir = ".";

        Program result;
        for (Stmt& stmt : program.statements) {
            auto* import = std::get_if<ImportStmt>(&stmt.value);
            if (!import) {
                if (const ImportStmt* nested = findNestedImport(stmt))
                    throw LumaError(nested->line,
                                    "import is only allowed at the top level");
                result.statements.push_back(std::move(stmt));
                continue;
            }
            spliceImport(*import, dir, result);
        }
        return result;
    }

private:
    void spliceImport(const ImportStmt& import, const fs::path& dir,
                      Program& result) {
        if (chainCanonical_.size() >= 128)
            throw LumaError(import.line, "import nesting is too deep (maximum 128)");
        fs::path target = dir / fs::path(import.path);
        std::string canonical;
        try {
            canonical = fs::weakly_canonical(target).string();
        } catch (const fs::filesystem_error&) {
            canonical = target.string();
        }

        // Уже склеен (в т.ч. другим модулем) — повторная загрузка не нужна.
        if (loaded_.count(canonical)) return;

        // Файл сейчас в обработке → цикл: показать всю цепочку.
        for (size_t i = 0; i < chainCanonical_.size(); ++i) {
            if (chainCanonical_[i] != canonical) continue;
            std::string chain;
            for (const std::string& name : chainDisplay_) chain += name + " -> ";
            chain += import.path;
            throw LumaError(import.line, "circular import detected: " + chain);
        }

        std::ifstream in(target, std::ios::binary);
        if (!in) {
            throw LumaError(import.line, "imported file '" + import.path +
                                              "' not found (resolved to '" +
                                              canonical + "')");
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();

        Program moduleProgram;
        try {
            Lexer lexer(buffer.str());
            Parser parser(lexer.tokenize());
            moduleProgram = parser.parseProgram();
        } catch (const LumaError& error) {
            // Ошибка разбора внутри импортируемого файла — указать файл.
            LumaError contextual = error;
            contextual.file = target.string();
            LumaError result(error.line, "in imported file '" + import.path + "': " + error.what());
            result.file = contextual.file;
            result.column = error.column;
            throw result;
        }

        chainCanonical_.push_back(canonical);
        chainDisplay_.push_back(import.path);
        Program spliced;
        try {
            spliced = process(std::move(moduleProgram), target, import.path);
        } catch (const LumaError& error) {
            // Цикл имеет собственный формат — не заворачивать; остальное
            // (не найден файл, ошибка глубже) — снабдить именем файла.
            if (std::string(error.what()).rfind("circular import detected", 0) == 0)
                throw;
            LumaError result(error.line, "in imported file '" + import.path + "': " + error.what());
            result.file = error.file;
            result.column = error.column;
            throw result;
        }
        chainCanonical_.pop_back();
        chainDisplay_.pop_back();

        loaded_.insert(canonical);
        for (Stmt& stmt : spliced.statements)
            result.statements.push_back(std::move(stmt));
    }

    std::unordered_set<std::string> loaded_;    // полностью склеенные файлы
    std::vector<std::string> chainCanonical_;   // стек файлов в обработке
    std::vector<std::string> chainDisplay_;     // их имена для ошибки цикла
};

}  // namespace

Program resolveImports(Program program, const std::string& sourcePath) {
    fs::path root(sourcePath);
    std::string display = root.filename().string();
    if (display.empty()) display = sourcePath;

    Importer importer(root);
    return importer.process(std::move(program), root, display);
}
