#include "analyzer.h"

#include "error.h"
#include "natives.h"

void Analyzer::analyze(const Program& program) {
    scopes.clear();
    loopDepth = 0;
    functionDepth = 0;

    scopes.emplace_back();
    // Встроенные функции: фиксированная арность или вариадичность (print).
    // Должно совпадать с natives.h (регистрация рантайма).
    Scope& global = scopes.back();
    for (const BuiltinDefinition& builtin : builtinDefinitions())
        global.names.emplace(builtin.name, builtin.arity);

    for (const Stmt& stmt : program.statements) {
        try {
            exec(stmt);
        } catch (const LumaError& error) {
            if (stmt.origin.empty()) throw;
            LumaError result(error.line, "in " + stmt.origin + ": " + error.what());
            result.file = stmt.sourceFile;
            result.column = error.column;
            throw result;
        }
    }
}

void Analyzer::declare(const std::string& name, std::optional<size_t> arity, int line) {
    if (scopes.back().names.count(name))
        throw LumaError(line, "Variable '" + name + "' is already defined");
    scopes.back().names.emplace(name, arity);
}

const std::optional<size_t>* Analyzer::resolve(const std::string& name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->names.find(name);
        if (found != it->names.end()) return &found->second;
    }
    return nullptr;
}

void Analyzer::exec(const Stmt& stmt) {
    if (auto* node = std::get_if<LetStmt>(&stmt.value)) {
        eval(*node->initializer);
        declare(node->name.lexeme, std::nullopt, node->line);
        return;
    }
    if (auto* node = std::get_if<AssignStmt>(&stmt.value)) {
        // Цель: имя — проверяем объявление; индекс — проверяем подвыражения.
        if (auto* name = std::get_if<IdentifierExpr>(&node->target->value)) {
            if (!resolve(name->name)) {
                LumaError error(name->line, "Unknown variable '" + name->name + "'");
                error.column = name->column;
                throw error;
            }
        } else if (auto* index = std::get_if<IndexExpr>(&node->target->value)) {
            eval(*index->object);
            eval(*index->index);
        }
        eval(*node->value);
        return;
    }
    if (auto* node = std::get_if<ExprStmt>(&stmt.value)) {
        eval(*node->expression);
        return;
    }
    if (auto* node = std::get_if<IfStmt>(&stmt.value)) {
        for (const IfBranch& branch : node->branches) {
            if (branch.condition) eval(*branch.condition);
            scopes.emplace_back();
            for (const Stmt& inner : branch.body->statements) exec(inner);
            scopes.pop_back();
        }
        return;
    }
    if (auto* node = std::get_if<WhileStmt>(&stmt.value)) {
        eval(*node->condition);
        loopDepth++;
        scopes.emplace_back();
        for (const Stmt& inner : node->body->statements) exec(inner);
        scopes.pop_back();
        loopDepth--;
        return;
    }
    if (auto* node = std::get_if<ForStmt>(&stmt.value)) {
        eval(*node->from);
        eval(*node->to);
        if (node->step) eval(*node->step);

        scopes.emplace_back();  // область переменной цикла
        declare(node->variable.lexeme, std::nullopt, node->line);
        loopDepth++;
        scopes.emplace_back();  // тело
        for (const Stmt& inner : node->body->statements) exec(inner);
        scopes.pop_back();
        loopDepth--;
        scopes.pop_back();
        return;
    }
    if (auto* node = std::get_if<FunctionStmt>(&stmt.value)) {
        // Имя объявляется ДО анализа тела — как в рантайме, поэтому рекурсия
        // и вызов до объявления-в-теле корректны.
        declare(node->name.lexeme, node->params.size(), node->line);

        // break/continue не пересекают границу функции (как в рантайме).
        int savedLoopDepth = loopDepth;
        loopDepth = 0;
        functionDepth++;
        scopes.emplace_back();
        for (const Token& param : node->params) {
            if (scopes.back().names.count(param.lexeme))
                throw LumaError(param.line, "Duplicate parameter '" + param.lexeme +
                                                "' in function '" +
                                                node->name.lexeme + "'");
            scopes.back().names.emplace(param.lexeme, std::optional<size_t>{});
        }
        for (const Stmt& inner : node->body->statements) exec(inner);
        scopes.pop_back();
        functionDepth--;
        loopDepth = savedLoopDepth;
        return;
    }
    if (auto* node = std::get_if<ReturnStmt>(&stmt.value)) {
        if (functionDepth == 0)
            throw LumaError(node->line, "return is only allowed inside a function");
        if (node->value) eval(*node->value);
        return;
    }
    if (auto* node = std::get_if<BreakStmt>(&stmt.value)) {
        if (loopDepth == 0)
            throw LumaError(node->line, "break is only allowed inside a loop");
        return;
    }
    if (auto* node = std::get_if<ContinueStmt>(&stmt.value)) {
        if (loopDepth == 0)
            throw LumaError(node->line, "continue is only allowed inside a loop");
        return;
    }
    if (auto* node = std::get_if<ImportStmt>(&stmt.value)) {
        // resolveImports (Luma 0.1.1) подставляет модули до анализатора;
        // оставшийся узел — внутренняя ошибка, а не молчаливый пропуск.
        throw LumaError(node->line,
                        "internal: unresolved import '" + node->path + "'");
    }
}

void Analyzer::eval(const Expr& expr) {
    if (std::holds_alternative<LiteralExpr>(expr.value)) return;

    if (auto* node = std::get_if<IdentifierExpr>(&expr.value)) {
        if (!resolve(node->name)) {
            LumaError error(node->line, "Unknown variable '" + node->name + "'");
            error.column = node->column;
            throw error;
        }
        return;
    }
    if (auto* node = std::get_if<GroupingExpr>(&expr.value)) {
        eval(*node->expression);
        return;
    }
    if (auto* node = std::get_if<UnaryExpr>(&expr.value)) {
        eval(*node->operand);
        return;
    }
    if (auto* node = std::get_if<BinaryExpr>(&expr.value)) {
        eval(*node->left);
        eval(*node->right);
        return;
    }
    if (auto* node = std::get_if<CallExpr>(&expr.value)) {
        eval(*node->callee);
        for (const Expr& arg : node->args) eval(arg);

        // Статическая проверка арности: вызываемое имя — известная функция
        // с фиксированным числом параметров (не переменная, не print).
        if (auto* callee = std::get_if<IdentifierExpr>(&node->callee->value)) {
            const std::optional<size_t>* arity = resolve(callee->name);
            if (arity && arity->has_value() && *arity != node->args.size()) {
                throw LumaError(node->line,
                                "function '" + callee->name + "' expects " +
                                    std::to_string(**arity) + " arguments, got " +
                                    std::to_string(node->args.size()));
            }
        }
        return;
    }
    if (auto* node = std::get_if<ArrayExpr>(&expr.value)) {
        for (const Expr& element : node->elements) eval(element);
        return;
    }
    if (auto* node = std::get_if<IndexExpr>(&expr.value)) {
        eval(*node->object);
        eval(*node->index);
        return;
    }
}
