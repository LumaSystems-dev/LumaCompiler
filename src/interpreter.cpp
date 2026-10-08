#include "interpreter.h"

#include <utility>

#include "error.h"
#include "natives.h"
#include "runtime.h"

// --- Environment ---

Environment::Environment(std::shared_ptr<Environment> parentEnv)
    : parent(std::move(parentEnv)) {}

void Environment::define(const std::string& name, Value value, int line) {
    if (variables.count(name))
        throw LumaError(line, "Variable '" + name + "' is already defined");
    variables.emplace(name, std::move(value));
}

void Environment::assign(const std::string& name, Value value, int line) {
    for (Environment* scope = this; scope; scope = scope->parent.get()) {
        auto it = scope->variables.find(name);
        if (it != scope->variables.end()) {
            it->second = std::move(value);
            return;
        }
    }
    throw LumaError(line, "Unknown variable '" + name + "'");
}

Value Environment::get(const std::string& name, int line) const {
    for (const Environment* scope = this; scope; scope = scope->parent.get()) {
        auto it = scope->variables.find(name);
        if (it != scope->variables.end()) return it->second;
    }
    throw LumaError(line, "Unknown variable '" + name + "'");
}

// --- Interpreter ---

namespace {

// Сигналы прерывания потока управления (приём jlox). При раскрутке
// ScopedEnvironment корректно восстанавливает цепочку областей.
struct BreakSignal {};
struct ContinueSignal {};
struct ReturnSignal {
    Value value;
};

}  // namespace

// --- ScopedEnvironment: RAII-вход в дочернюю область ---

Interpreter::ScopedEnvironment::ScopedEnvironment(Interpreter& interp,
                                                  std::shared_ptr<Environment> next)
    : interpreter(interp), previous(interp.environment) {
    interpreter.environment = std::move(next);
}

Interpreter::ScopedEnvironment::~ScopedEnvironment() {
    interpreter.environment = std::move(previous);
}

Interpreter::Interpreter(std::ostream& output) : output(output) {
    environment = std::make_shared<Environment>();
    registerBuiltins(*environment);
}

void Interpreter::run(const Program& program) {
    for (const Stmt& stmt : program.statements) exec(stmt);
}

// Присваивание по цели: имя — через окружение, индекс — через массив.
// Цель-выражение уже валидирована парсером (имя или индекс).
void Interpreter::assignTo(const Expr& target, Value value, int line) {
    if (auto* name = std::get_if<IdentifierExpr>(&target.value)) {
        environment->assign(name->name, std::move(value), line);
        return;
    }
    if (auto* index = std::get_if<IndexExpr>(&target.value)) {
        Value object = eval(*index->object);
        Value indexValue = eval(*index->index);
        double position = requireNumber(indexValue, line, "array index");

        auto* array = asArray(object);
        if (!array || !*array)
            throw LumaError(line, "value of type " + valueTypeToString(object) +
                                      " is not indexable");
        std::vector<Value>& elements = (*array)->elements;
        if (!std::isfinite(position) || position < 0 || position >= static_cast<double>(elements.size()))
            throw LumaError(line, "array index " + formatNumber(position) +
                                      " is out of bounds (length " +
                                      std::to_string(elements.size()) + ")");
        elements[static_cast<size_t>(position)] = std::move(value);
        return;
    }
    throw LumaError(line, "invalid assignment target");  // недостижимо: парсер проверил
}

void Interpreter::exec(const Stmt& stmt) {
    if (auto* node = std::get_if<LetStmt>(&stmt.value)) {
        Value value = eval(*node->initializer);
        environment->define(node->name.lexeme, std::move(value), node->line);
        return;
    }
    if (auto* node = std::get_if<AssignStmt>(&stmt.value)) {
        Value value = eval(*node->value);
        assignTo(*node->target, std::move(value), node->line);
        return;
    }
    if (auto* node = std::get_if<ExprStmt>(&stmt.value)) {
        (void)eval(*node->expression);  // значение выражения-оператора отбрасывается
        return;
    }
    if (auto* node = std::get_if<IfStmt>(&stmt.value)) {
        // Первая ветвь с истинным условием; else — ветвь без условия.
        for (const IfBranch& branch : node->branches) {
            if (branch.condition && !isTruthy(eval(*branch.condition))) continue;
            ScopedEnvironment guard(*this, std::make_shared<Environment>(environment));
            for (const Stmt& inner : branch.body->statements) exec(inner);
            return;
        }
        return;
    }
    if (auto* node = std::get_if<WhileStmt>(&stmt.value)) {
        // Тело — новая область на каждой итерации: let в теле не конфликтует
        // между итерациями.
        while (isTruthy(eval(*node->condition))) {
            try {
                ScopedEnvironment bodyScope(*this, std::make_shared<Environment>(environment));
                for (const Stmt& inner : node->body->statements) exec(inner);
            } catch (const BreakSignal&) {
                break;
            } catch (const ContinueSignal&) {
                // область уже раскрыта гвардом; переход к проверке условия
            }
        }
        return;
    }
    if (auto* node = std::get_if<ForStmt>(&stmt.value)) {
        // Границы и шаг вычисляются ОДИН раз до начала цикла.
        double from = requireNumber(eval(*node->from), node->line, "for loop bound");
        double to = requireNumber(eval(*node->to), node->line, "for loop bound");
        double step = node->step
                          ? requireNumber(eval(*node->step), node->line, "for loop step")
                          : 1.0;
        if (step == 0) throw LumaError(node->line, "for loop step cannot be zero");

        // Полуоткрытый диапазон: from включительно, to не входит.
        // Шаг вниз идёт, пока i > to. Переменная цикла — копия во вложенной
        // области: изменение i в теле не влияет на внутренний счётчик.
        for (double i = from; step > 0 ? i < to : i > to; i += step) {
            try {
                ScopedEnvironment iterationScope(*this,
                                        std::make_shared<Environment>(environment));
                environment->define(node->variable.lexeme, i, node->line);
                ScopedEnvironment bodyScope(*this, std::make_shared<Environment>(environment));
                for (const Stmt& inner : node->body->statements) exec(inner);
            } catch (const BreakSignal&) {
                break;
            } catch (const ContinueSignal&) {
            }
        }
        return;
    }
    if (auto* node = std::get_if<FunctionStmt>(&stmt.value)) {
        auto function = std::make_shared<Function>();
        function->name = node->name.lexeme;
        function->params.reserve(node->params.size());
        for (const Token& param : node->params) function->params.push_back(param.lexeme);
        function->body = node->body.get();
        function->closure = environment;  // окружение места определения
        environment->define(node->name.lexeme, Value(std::move(function)), node->line);
        return;
    }
    if (auto* node = std::get_if<ReturnStmt>(&stmt.value)) {
        throw ReturnSignal{node->value ? eval(*node->value) : Value{}};
    }
    if (std::get_if<BreakStmt>(&stmt.value)) throw BreakSignal{};
    if (std::get_if<ContinueStmt>(&stmt.value)) throw ContinueSignal{};
    if (auto* node = std::get_if<ImportStmt>(&stmt.value)) {
        throw LumaError(node->line,
                        "internal: unresolved import '" + node->path + "'");
    }
}

Value Interpreter::eval(const Expr& expr) {
    if (auto* node = std::get_if<LiteralExpr>(&expr.value)) {
        switch (node->token.type) {
            case TokenKind::NUMBER:   return node->token.number;
            case TokenKind::STRING:   return node->token.str;
            case TokenKind::KW_TRUE:  return true;
            case TokenKind::KW_FALSE: return false;
            case TokenKind::KW_NIL:   return Value{};  // Value{} == nil
            default: break;
        }
        throw LumaError(node->line, "invalid literal");  // недостижимо
    }

    if (auto* node = std::get_if<IdentifierExpr>(&expr.value))
        return environment->get(node->name, node->line);

    if (auto* node = std::get_if<GroupingExpr>(&expr.value))
        return eval(*node->expression);

    if (auto* node = std::get_if<UnaryExpr>(&expr.value))
        return evalUnary(*node);

    if (auto* node = std::get_if<BinaryExpr>(&expr.value))
        return evalBinary(*node);

    if (auto* node = std::get_if<CallExpr>(&expr.value))
        return evalCall(*node);

    if (auto* node = std::get_if<ArrayExpr>(&expr.value)) {
        auto elements = std::make_shared<Array>();
        elements->elements.reserve(node->elements.size());
        for (const Expr& element : node->elements) elements->elements.push_back(eval(element));
        return Value(std::move(elements));
    }

    if (auto* node = std::get_if<IndexExpr>(&expr.value)) {
        Value object = eval(*node->object);
        Value indexValue = eval(*node->index);
        double position = requireNumber(indexValue, node->line, "array index");

        auto* array = asArray(object);
        if (!array || !*array)
            throw LumaError(node->line, "value of type " + valueTypeToString(object) +
                                            " is not indexable");
        const std::vector<Value>& elements = (*array)->elements;
        if (!std::isfinite(position) || position < 0 || position >= static_cast<double>(elements.size()))
            throw LumaError(node->line, "array index " + formatNumber(position) +
                                            " is out of bounds (length " +
                                            std::to_string(elements.size()) + ")");
        return elements[static_cast<size_t>(position)];
    }

    throw LumaError(0, "unknown expression node");  // недостижимо
}

Value Interpreter::evalUnary(const UnaryExpr& node) {
    Value operand = eval(*node.operand);
    if (node.op.type == TokenKind::MINUS) {
        if (const double* number = std::get_if<double>(&operand)) return -*number;
        throw LumaError(node.line, "cannot negate " + valueTypeToString(operand));
    }
    if (node.op.type == TokenKind::KW_NOT) return !isTruthy(operand);
    throw LumaError(node.line, "unsupported unary operator");  // недостижимо
}

Value Interpreter::evalBinary(const BinaryExpr& node) {
    TokenKind op = node.op.type;

    // and/or: короткое замыкание и возврат значения операнда, а не boolean.
    // Правый операнд может не вычисляться вовсе (DESIGN.md 4.1).
    if (op == TokenKind::KW_AND || op == TokenKind::KW_OR) {
        Value left = eval(*node.left);
        if (op == TokenKind::KW_AND && !isTruthy(left)) return left;
        if (op == TokenKind::KW_OR && isTruthy(left)) return left;
        return eval(*node.right);
    }

    Value left = eval(*node.left);
    Value right = eval(*node.right);

    switch (op) {
        case TokenKind::PLUS:
        case TokenKind::MINUS:
        case TokenKind::STAR:
        case TokenKind::SLASH:
        case TokenKind::PERCENT:
            return arithmetic(op, node.line, left, right);
        case TokenKind::EQUAL_EQUAL:
            // Равенство любых типов: разные альтернативы variant всегда неравны.
            return left == right;
        case TokenKind::BANG_EQUAL:
            return !(left == right);
        case TokenKind::LESS:
        case TokenKind::LESS_EQUAL:
        case TokenKind::GREATER:
        case TokenKind::GREATER_EQUAL:
            return ordered(op, node.line, left, right);
        case TokenKind::DOTDOT:
            return concatenate(node.line, left, right);
        default:
            break;
    }
    throw LumaError(node.line, "unsupported binary operator");  // недостижимо
}

Value Interpreter::evalCall(const CallExpr& node) {
    Value callee = eval(*node.callee);

    // Аргументы вычисляются слева направо до передачи в функцию.
    std::vector<Value> args;
    args.reserve(node.args.size());
    for (const Expr& arg : node.args) args.push_back(eval(arg));

    if (auto* native = std::get_if<std::shared_ptr<NativeFunction>>(&callee)) {
        if (!*native)
            throw LumaError(node.line, "value of type nil is not callable");
        return (*native)->fn(output, std::move(args), node.line);
    }
    if (auto* function = std::get_if<std::shared_ptr<Function>>(&callee)) {
        if (!*function)
            throw LumaError(node.line, "value of type nil is not callable");
        return callFunction(**function, std::move(args), node.line);
    }
    throw LumaError(node.line,
                    "value of type " + valueTypeToString(callee) + " is not callable");
}

Value Interpreter::callFunction(const Function& function, std::vector<Value> args,
                                int line) {
    // Арность проверяется в рантайме (Этап 9 сделает это статически,
    // когда вызываемая функция известна на этапе компиляции).
    if (args.size() != function.params.size()) {
        throw LumaError(line, "function '" + function.name + "' expects " +
                                  std::to_string(function.params.size()) +
                                  " arguments, got " + std::to_string(args.size()));
    }

    // Область параметров (родитель — замыкание), затем область тела:
    // let в теле может затенить параметр.
    ScopedEnvironment callGuard(*this, std::make_shared<Environment>(function.closure));
    for (size_t i = 0; i < args.size(); ++i)
        environment->define(function.params[i], std::move(args[i]), line);
    ScopedEnvironment bodyGuard(*this, std::make_shared<Environment>(environment));

    try {
        for (const Stmt& inner : function.body->statements) exec(inner);
    } catch (ReturnSignal& signal) {
        return std::move(signal.value);
    }
    return Value{};  // return не встретился → nil
}
