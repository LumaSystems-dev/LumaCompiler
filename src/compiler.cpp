#include "compiler.h"

#include "error.h"

namespace {

// Максимальное смещение 16-битного операнда со знаком.
constexpr int32_t kMaxJumpOffset = 32767;

bool endsWithReturn(const Chunk& chunk) {
    size_t pc = 0;
    OpCode last = OpCode::NIL;
    while (pc < chunk.code.size()) {
        last = static_cast<OpCode>(chunk.code[pc]);
        pc += instructionLength(last);
    }
    return !chunk.code.empty() && last == OpCode::RETURN;
}

// Маппинг бинарных операторов на опкоды. Производные сравнения выражаются
// `!=` кодируется EQUAL NOT. `<=` и `>=` обрабатываются отдельно,
// через упорядоченное сравнение или равенство (корректно для NaN).
OpCode simpleBinaryOp(TokenKind op) {
    switch (op) {
        case TokenKind::PLUS:          return OpCode::ADD;
        case TokenKind::MINUS:         return OpCode::SUB;
        case TokenKind::STAR:          return OpCode::MUL;
        case TokenKind::SLASH:         return OpCode::DIV;
        case TokenKind::PERCENT:       return OpCode::MOD;
        case TokenKind::DOTDOT:        return OpCode::CONCAT;
        case TokenKind::EQUAL_EQUAL:   return OpCode::EQUAL;
        case TokenKind::BANG_EQUAL:    return OpCode::EQUAL;
        case TokenKind::LESS:          return OpCode::LESS;
        case TokenKind::LESS_EQUAL:    return OpCode::GREATER;
        case TokenKind::GREATER:       return OpCode::GREATER;
        case TokenKind::GREATER_EQUAL: return OpCode::LESS;
        default:                       return OpCode::POP;  // недостижимо после парсера
    }
}

}  // namespace

std::shared_ptr<Chunk> Compiler::compile(const Program& program) {
    auto chunk = std::make_shared<Chunk>();
    chunk_ = chunk.get();
    scopeDepth_ = 0;
    loops_.clear();

    for (const Stmt& stmt : program.statements) exec(stmt);

    // Неявный `return nil` в конце, если тело не завершается RETURN.
    if (!endsWithReturn(*chunk_)) {
        writeOp(OpCode::NIL, 0);
        writeOp(OpCode::RETURN, 0);
    }
    return chunk;
}

// --- Низкоуровневая эмиссия ---

void Compiler::emitOp(OpCode op) {
    writeOp(op, line_);
}

void Compiler::writeOp(OpCode op, int line) {
    chunk_->write(static_cast<uint8_t>(op), line);
}

void Compiler::writeOperand(uint16_t value, int line) {
    chunk_->writeOperand(value, line);
}

uint16_t Compiler::addConstant(Constant value) {
    return chunk_->addConstant(std::move(value));
}

uint16_t Compiler::varIndex(const std::string& name) {
    return addConstant(Constant(name));
}

void Compiler::emitVarOp(OpCode op, uint16_t idx) {
    writeOp(op, line_);
    writeOperand(idx, line_);
}

size_t Compiler::emitJump(OpCode op) {
    writeOp(op, line_);
    size_t operandPos = chunk_->size();
    writeOperand(0, line_);  // placeholder, заплатится patchJump
    return operandPos;
}

void Compiler::patchJump(size_t operandPos) {
    // Смещение считается от КОНЦА инструкции (operandPos + 2).
    int32_t offset = static_cast<int32_t>(chunk_->size()) - static_cast<int32_t>(operandPos + 2);
    if (offset > kMaxJumpOffset)
        throw LumaError(line_, "too much code to jump over");
    chunk_->code[operandPos] = static_cast<uint8_t>(offset & 0xFF);
    chunk_->code[operandPos + 1] = static_cast<uint8_t>((offset >> 8) & 0xFF);
}

void Compiler::emitLoop(size_t startAddr) {
    writeOp(OpCode::LOOP, line_);
    uint32_t offset = static_cast<uint32_t>(chunk_->size() + 2 - startAddr);
    if (offset > 0xFFFF)
        throw LumaError(line_, "loop body too large");
    writeOperand(static_cast<uint16_t>(offset), line_);
}

// --- Стейтменты ---

void Compiler::exec(const Stmt& stmt) {
    if (auto* node = std::get_if<LetStmt>(&stmt.value)) {
        line_ = node->line;
        eval(*node->initializer);
        emitVarOp(OpCode::DEFINE_VAR, varIndex(node->name.lexeme));
        return;
    }
    if (auto* node = std::get_if<AssignStmt>(&stmt.value)) {
        line_ = node->line;
        if (auto* name = std::get_if<IdentifierExpr>(&node->target->value)) {
            eval(*node->value);
            emitVarOp(OpCode::SET_VAR, varIndex(name->name));
            return;
        }
        if (auto* index = std::get_if<IndexExpr>(&node->target->value)) {
            eval(*index->object);
            eval(*index->index);
            eval(*node->value);
            emitOp(OpCode::SET_INDEX);
            return;
        }
        throw LumaError(node->line, "invalid assignment target");  // недостижимо
    }
    if (auto* node = std::get_if<ExprStmt>(&stmt.value)) {
        line_ = node->line;
        eval(*node->expression);
        emitOp(OpCode::POP);  // значение выражения-оператора отбрасывается
        return;
    }
    if (auto* node = std::get_if<IfStmt>(&stmt.value)) {
        ifStmt(*node);
        return;
    }
    if (auto* node = std::get_if<WhileStmt>(&stmt.value)) {
        whileStmt(*node);
        return;
    }
    if (auto* node = std::get_if<ForStmt>(&stmt.value)) {
        forStmt(*node);
        return;
    }
    if (auto* node = std::get_if<FunctionStmt>(&stmt.value)) {
        functionStmt(*node);
        return;
    }
    if (auto* node = std::get_if<ReturnStmt>(&stmt.value)) {
        line_ = node->line;
        if (node->value)
            eval(*node->value);
        else
            emitOp(OpCode::NIL);
        emitOp(OpCode::RETURN);  // VM восстановит окружение кадра целиком
        return;
    }
    if (auto* node = std::get_if<BreakStmt>(&stmt.value)) {
        line_ = node->line;
        if (loops_.empty())
            throw LumaError(node->line, "break is only allowed inside a loop");
        LoopContext& loop = loops_.back();
        for (int i = 0; i < scopeDepth_ - loop.scopeDepth; ++i) emitOp(OpCode::EXIT_SCOPE);
        loop.endPatches.push_back(emitJump(OpCode::JUMP));
        return;
    }
    if (auto* node = std::get_if<ContinueStmt>(&stmt.value)) {
        line_ = node->line;
        if (loops_.empty())
            throw LumaError(node->line, "continue is only allowed inside a loop");
        LoopContext& loop = loops_.back();
        for (int i = 0; i < scopeDepth_ - loop.scopeDepth; ++i) emitOp(OpCode::EXIT_SCOPE);
        if (loop.hasIncrement) {
            // for: continue обязан пройти через инкремент счётчика,
            // иначе цикл зацикливается (расхождение с интерпретатором).
            loop.incPatches.push_back(emitJump(OpCode::JUMP));
        } else {
            emitLoop(loop.startAddr);  // while: достаточно перепроверить условие
        }
        return;
    }
    if (auto* node = std::get_if<ImportStmt>(&stmt.value)) {
        throw LumaError(node->line,
                        "internal: unresolved import '" + node->path + "'");
    }
}

void Compiler::ifStmt(const IfStmt& node) {
    line_ = node.line;
    std::vector<size_t> endPatches;

    for (size_t i = 0; i < node.branches.size(); ++i) {
        const IfBranch& branch = node.branches[i];
        size_t condJump = 0;
        if (branch.condition) {
            eval(*branch.condition);
            // JUMP_IF_FALSE снимает условие со стека — обе ветви сбалансированы,
            // в том числе при раннем return из тела.
            condJump = emitJump(OpCode::JUMP_IF_FALSE);
        }
        // Тело ветви — своя область видимости (как в интерпретаторе): let
        // внутри ветви не утекает наружу и может затенять внешние имена.
        emitOp(OpCode::ENTER_SCOPE);
        scopeDepth_++;
        for (const Stmt& inner : branch.body->statements) exec(inner);
        emitOp(OpCode::EXIT_SCOPE);
        scopeDepth_--;
        if (branch.condition) {
            if (i + 1 < node.branches.size()) {
                endPatches.push_back(emitJump(OpCode::JUMP));
                patchJump(condJump);  // ложный путь: следующая ветвь
            } else {
                patchJump(condJump);  // ложный путь последней ветви: сразу Lend
            }
        }
    }
    for (size_t site : endPatches) patchJump(site);
}

void Compiler::whileStmt(const WhileStmt& node) {
    line_ = node.line;
    size_t startAddr = chunk_->size();
    loops_.push_back({startAddr, scopeDepth_, false, {}, {}});

    eval(*node.condition);
    size_t exitJump = emitJump(OpCode::JUMP_IF_FALSE);  // снимает условие

    emitOp(OpCode::ENTER_SCOPE);  // тело — новая область на каждую итерацию
    scopeDepth_++;
    for (const Stmt& inner : node.body->statements) exec(inner);
    emitOp(OpCode::EXIT_SCOPE);
    scopeDepth_--;

    emitLoop(startAddr);
    patchJump(exitJump);  // Lend
    // break'и прыгают сюда же — заплатить их переходы на Lend.
    for (size_t site : loops_.back().endPatches) patchJump(site);

    loops_.pop_back();
}

void Compiler::forStmt(const ForStmt& node) {
    line_ = node.line;
    emitOp(OpCode::ENTER_SCOPE);  // область скрытых переменных цикла
    scopeDepth_++;
    uint16_t iIdx = varIndex("@i");
    uint16_t toIdx = varIndex("@to");
    uint16_t stepIdx = varIndex("@step");

    // Границы и шаг вычисляются один раз (семантика интерпретатора).
    eval(*node.from);
    emitVarOp(OpCode::DEFINE_VAR, iIdx);
    eval(*node.to);
    emitVarOp(OpCode::DEFINE_VAR, toIdx);
    if (node.step) {
        eval(*node.step);
    } else {
        writeOp(OpCode::CONSTANT, node.line);
        writeOperand(addConstant(Constant(1.0)), node.line);
    }
    emitVarOp(OpCode::DEFINE_VAR, stepIdx);

    // Шаг не может быть нулём: скрытая проверка при входе в цикл.
    // JUMP_IF_FALSE снимает результат EQUAL с обеих путей — компенсации не нужны.
    emitVarOp(OpCode::GET_VAR, stepIdx);
    writeOp(OpCode::CONSTANT, node.line);
    writeOperand(addConstant(Constant(0.0)), node.line);
    emitOp(OpCode::EQUAL);
    size_t okJump = emitJump(OpCode::JUMP_IF_FALSE);
    line_ = node.line;
    writeOp(OpCode::FAIL, node.line);
    writeOperand(addConstant(Constant(std::string("for loop step cannot be zero"))), node.line);
    patchJump(okJump);

    size_t startAddr = chunk_->size();  // Lstart
    loops_.push_back({startAddr, scopeDepth_, true, {}, {}});

    // Условие: (шаг > 0) ? (i < to) : (i > to) — полуоткрытый диапазон.
    emitVarOp(OpCode::GET_VAR, stepIdx);
    writeOp(OpCode::CONSTANT, node.line);
    writeOperand(addConstant(Constant(0.0)), node.line);
    emitOp(OpCode::GREATER);
    size_t negJump = emitJump(OpCode::JUMP_IF_FALSE);
    emitVarOp(OpCode::GET_VAR, iIdx);
    emitVarOp(OpCode::GET_VAR, toIdx);
    emitOp(OpCode::LESS);
    size_t checkJump = emitJump(OpCode::JUMP);
    patchJump(negJump);
    emitVarOp(OpCode::GET_VAR, iIdx);
    emitVarOp(OpCode::GET_VAR, toIdx);
    emitOp(OpCode::GREATER);
    patchJump(checkJump);

    size_t exitJump = emitJump(OpCode::JUMP_IF_FALSE);  // Lend (снимает условие)

    // Итерация: область переменной i, затем область тела.
    emitOp(OpCode::ENTER_SCOPE);
    scopeDepth_++;
    emitVarOp(OpCode::GET_VAR, iIdx);
    emitVarOp(OpCode::DEFINE_VAR, varIndex(node.variable.lexeme));
    emitOp(OpCode::ENTER_SCOPE);
    scopeDepth_++;
    for (const Stmt& inner : node.body->statements) exec(inner);
    emitOp(OpCode::EXIT_SCOPE);
    scopeDepth_--;
    emitOp(OpCode::EXIT_SCOPE);
    scopeDepth_--;

    // Linc: инкремент счётчика. Сюда приходят и нормальный путь, и continue —
    // иначе continue пропускал бы инкремент и цикл зацикливался.
    for (size_t site : loops_.back().incPatches) patchJump(site);
    // @i += step
    emitVarOp(OpCode::GET_VAR, iIdx);
    emitVarOp(OpCode::GET_VAR, stepIdx);
    emitOp(OpCode::ADD);
    emitVarOp(OpCode::SET_VAR, iIdx);

    emitLoop(startAddr);
    patchJump(exitJump);  // Lend
    // break'и прыгают на Lend: их EXIT_SCOPE'ы (выход из тела) уже выпущены,
    // а скрытая область цикла закрывается общим EXIT_SCOPE ниже.
    for (size_t site : loops_.back().endPatches) patchJump(site);
    emitOp(OpCode::EXIT_SCOPE);
    scopeDepth_--;        // скрытая область цикла

    loops_.pop_back();
}

void Compiler::functionStmt(const FunctionStmt& node) {
    line_ = node.line;
    auto proto = std::make_shared<FunctionProto>();
    proto->name = node.name.lexeme;
    proto->params.reserve(node.params.size());
    for (const Token& param : node.params) proto->params.push_back(param.lexeme);
    proto->body = std::make_shared<Chunk>();

    // Переключаемся на чанк тела: циклы и глубина областей не пересекают
    // границу функции.
    Chunk* savedChunk = chunk_;
    auto savedLoops = std::move(loops_);
    int savedDepth = scopeDepth_;
    chunk_ = proto->body.get();
    loops_.clear();
    scopeDepth_ = 0;

    emitOp(OpCode::ENTER_SCOPE);  // тело — своя область (параметры затеняемы let'ом)
    scopeDepth_++;
    for (const Stmt& inner : node.body->statements) exec(inner);
    // Неявный `return nil`, только если тело не завершается явным RETURN.
    if (!endsWithReturn(*chunk_)) {
        emitOp(OpCode::NIL);
        emitOp(OpCode::RETURN);
    }

    chunk_ = savedChunk;
    loops_ = std::move(savedLoops);
    scopeDepth_ = savedDepth;

    uint16_t protoIdx = addConstant(Constant(std::move(proto)));
    emitVarOp(OpCode::CLOSURE, protoIdx);
    emitVarOp(OpCode::DEFINE_VAR, varIndex(node.name.lexeme));
}

// --- Выражения ---

void Compiler::eval(const Expr& expr) {
    if (auto* node = std::get_if<LiteralExpr>(&expr.value)) {
        line_ = node->line;
        switch (node->token.type) {
            case TokenKind::NUMBER:
                writeOp(OpCode::CONSTANT, node->line);
                writeOperand(addConstant(Constant(node->token.number)), node->line);
                break;
            case TokenKind::STRING:
                writeOp(OpCode::CONSTANT, node->line);
                writeOperand(addConstant(Constant(node->token.str)), node->line);
                break;
            case TokenKind::KW_TRUE:
                emitOp(OpCode::TRUE);
                break;
            case TokenKind::KW_FALSE:
                emitOp(OpCode::FALSE);
                break;
            case TokenKind::KW_NIL:
                emitOp(OpCode::NIL);
                break;
            default:
                throw LumaError(node->line, "invalid literal");  // недостижимо
        }
        return;
    }

    if (auto* node = std::get_if<IdentifierExpr>(&expr.value)) {
        line_ = node->line;
        emitVarOp(OpCode::GET_VAR, varIndex(node->name));
        return;
    }

    if (auto* node = std::get_if<GroupingExpr>(&expr.value)) {
        eval(*node->expression);
        return;
    }

    if (auto* node = std::get_if<UnaryExpr>(&expr.value)) {
        line_ = node->line;
        eval(*node->operand);
        if (node->op.type == TokenKind::MINUS)
            emitOp(OpCode::NEGATE);
        else if (node->op.type == TokenKind::KW_NOT)
            emitOp(OpCode::NOT);
        else
            throw LumaError(node->line, "unsupported unary operator");
        return;
    }

    if (auto* node = std::get_if<BinaryExpr>(&expr.value)) {
        line_ = node->line;
        // Короткое замыкание and/or: DUP сохраняет левый операнд как
        // потенциальный результат, JUMP_IF_FALSE снимает копию.
        if (node->op.type == TokenKind::KW_AND) {
            eval(*node->left);              // [a]
            emitOp(OpCode::DUP);            // [a, a]
            size_t jump = emitJump(OpCode::JUMP_IF_FALSE);  // снимает копию
            emitOp(OpCode::POP);            // a истинно: убрать a
            eval(*node->right);             // [b]
            patchJump(jump);                // a ложно: [a] → результат a
            return;
        }
        if (node->op.type == TokenKind::KW_OR) {
            eval(*node->left);              // [a]
            emitOp(OpCode::DUP);            // [a, a]
            size_t jumpFalse = emitJump(OpCode::JUMP_IF_FALSE);  // снимает копию
            size_t jumpEnd = emitJump(OpCode::JUMP);             // a истинно: [a]
            patchJump(jumpFalse);
            emitOp(OpCode::POP);            // a ложно: убрать a
            eval(*node->right);             // [b]
            patchJump(jumpEnd);
            return;
        }

        eval(*node->left);
        eval(*node->right);
        if (node->op.type == TokenKind::LESS_EQUAL || node->op.type == TokenKind::GREATER_EQUAL) {
            // !(a > b) is not a <= b when either operand is NaN.
            // Evaluate operands once, then (a < b) or (a == b), using
            // existing instructions and private names unavailable in source.
            emitOp(OpCode::ENTER_SCOPE);
            uint16_t left = varIndex("$compare_left");
            uint16_t right = varIndex("$compare_right");
            emitVarOp(OpCode::DEFINE_VAR, right);
            emitVarOp(OpCode::DEFINE_VAR, left);
            emitVarOp(OpCode::GET_VAR, left);
            emitVarOp(OpCode::GET_VAR, right);
            emitOp(node->op.type == TokenKind::LESS_EQUAL ? OpCode::LESS : OpCode::GREATER);
            emitOp(OpCode::DUP);
            size_t falseJump = emitJump(OpCode::JUMP_IF_FALSE);
            size_t endJump = emitJump(OpCode::JUMP);
            patchJump(falseJump);
            emitOp(OpCode::POP);
            emitVarOp(OpCode::GET_VAR, left);
            emitVarOp(OpCode::GET_VAR, right);
            emitOp(OpCode::EQUAL);
            patchJump(endJump);
            emitOp(OpCode::EXIT_SCOPE);
            return;
        }
        OpCode op = simpleBinaryOp(node->op.type);
        emitOp(op);
        // != → EQUAL NOT.
        if (node->op.type == TokenKind::BANG_EQUAL)
            emitOp(OpCode::NOT);
        return;
    }

    if (auto* node = std::get_if<CallExpr>(&expr.value)) {
        if (node->args.size() > 65535) throw LumaError(node->line, "too many call arguments");
        line_ = node->line;
        eval(*node->callee);
        for (const Expr& arg : node->args) eval(arg);
        writeOp(OpCode::CALL, node->line);
        writeOperand(static_cast<uint16_t>(node->args.size()), node->line);
        return;
    }

    if (auto* node = std::get_if<ArrayExpr>(&expr.value)) {
        if (node->elements.size() > 65535) throw LumaError(node->line, "too many array elements");
        line_ = node->line;
        for (const Expr& element : node->elements) eval(element);
        writeOp(OpCode::ARRAY, node->line);
        writeOperand(static_cast<uint16_t>(node->elements.size()), node->line);
        return;
    }

    if (auto* node = std::get_if<IndexExpr>(&expr.value)) {
        line_ = node->line;
        eval(*node->object);
        eval(*node->index);
        emitOp(OpCode::GET_INDEX);
        return;
    }

    throw LumaError(0, "unknown expression node");  // недостижимо
}
