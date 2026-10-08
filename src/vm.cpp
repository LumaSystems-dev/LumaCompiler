#include "vm.h"

#include "error.h"
#include "natives.h"
#include "runtime.h"
#include "util.h"

namespace {

// Опкоды арифметики → токены для общих рантайм-правил (runtime.h).
TokenKind tokenFor(OpCode op) {
    switch (op) {
        case OpCode::ADD: return TokenKind::PLUS;
        case OpCode::SUB: return TokenKind::MINUS;
        case OpCode::MUL: return TokenKind::STAR;
        case OpCode::DIV: return TokenKind::SLASH;
        case OpCode::MOD: return TokenKind::PERCENT;
        default:          return TokenKind::PLUS;  // недостижимо
    }
}

uint16_t readOperand(const Chunk& chunk, size_t ip) {
    return static_cast<uint16_t>(chunk.code[ip] | (chunk.code[ip + 1] << 8));
}

}  // namespace

VM::VM(std::ostream& output) : output_(output) {}

Value VM::pop(int line) {
    if (stack_.empty())
        throw LumaError(line, "stack underflow");  // защита от битого байткода
    Value value = std::move(stack_.back());
    stack_.pop_back();
    return value;
}

void VM::run(const Chunk& chunk) {
    // Chunk может прийти не только от Compiler, но и из .lbc или внешнего
    // C++ кода. Проверяем его до первого доступа к операндам и стеку.
    validateChunk(chunk);
    stack_.clear();
    scopes_.clear();
    frames_.clear();

    current_ = std::make_shared<Environment>();
    registerBuiltins(*current_);

    frames_.push_back(Frame{&chunk, 0, nullptr, 0, 0});

    while (!frames_.empty()) {
        if (stack_.size() > 1048576 || scopes_.size() > 16384 || frames_.size() > 16384)
            throw LumaError(0, "VM resource limit exceeded");
        Frame& frame = frames_.back();
        if (frame.ip >= frame.chunk->code.size())
            break;  // защита: RETURN обязан завершить каждый чанк

        const Chunk& code = *frame.chunk;
        int line = code.lines[frame.ip];
        OpCode op = static_cast<OpCode>(code.code[frame.ip]);
        frame.ip++;

        switch (op) {
            case OpCode::CONSTANT: {
                uint16_t idx = readOperand(code, frame.ip);
                frame.ip += 2;
                if (auto* number = std::get_if<double>(&code.constants[idx]))
                    stack_.push_back(*number);
                else if (auto* string = std::get_if<std::string>(&code.constants[idx]))
                    stack_.push_back(*string);
                else
                    throw LumaError(line, "internal: unexpected constant kind");
                break;
            }
            case OpCode::NIL:   stack_.push_back(Value{}); break;
            case OpCode::TRUE:  stack_.push_back(true); break;
            case OpCode::FALSE: stack_.push_back(false); break;
            case OpCode::POP:   (void)pop(line); break;
            case OpCode::DUP:   stack_.push_back(stack_.back()); break;

            case OpCode::DEFINE_VAR: {
                uint16_t idx = readOperand(code, frame.ip);
                frame.ip += 2;
                const std::string& name = std::get<std::string>(code.constants[idx]);
                current_->define(name, pop(line), line);
                break;
            }
            case OpCode::GET_VAR: {
                uint16_t idx = readOperand(code, frame.ip);
                frame.ip += 2;
                const std::string& name = std::get<std::string>(code.constants[idx]);
                stack_.push_back(current_->get(name, line));
                break;
            }
            case OpCode::SET_VAR: {
                uint16_t idx = readOperand(code, frame.ip);
                frame.ip += 2;
                const std::string& name = std::get<std::string>(code.constants[idx]);
                current_->assign(name, pop(line), line);
                break;
            }

            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV:
            case OpCode::MOD: {
                Value right = pop(line);
                Value left = pop(line);
                stack_.push_back(arithmetic(tokenFor(op), line, left, right));
                break;
            }
            case OpCode::CONCAT: {
                Value right = pop(line);
                Value left = pop(line);
                stack_.push_back(concatenate(line, left, right));
                break;
            }
            case OpCode::EQUAL: {
                Value right = pop(line);
                Value left = pop(line);
                stack_.push_back(left == right);
                break;
            }
            case OpCode::LESS:
            case OpCode::GREATER: {
                TokenKind kind =
                    op == OpCode::LESS ? TokenKind::LESS : TokenKind::GREATER;
                Value right = pop(line);
                Value left = pop(line);
                stack_.push_back(ordered(kind, line, left, right));
                break;
            }
            case OpCode::NOT: {
                stack_.push_back(!isTruthy(pop(line)));
                break;
            }
            case OpCode::NEGATE: {
                Value value = pop(line);
                if (const double* number = asNumber(value))
                    stack_.push_back(-*number);
                else
                    throw LumaError(line, "cannot negate " + valueTypeToString(value));
                break;
            }

            case OpCode::ARRAY: {
                uint16_t count = readOperand(code, frame.ip);
                frame.ip += 2;
                if (stack_.size() < count)
                    throw LumaError(line, "stack underflow");
                auto elements = std::make_shared<Array>();
                elements->elements.assign(std::make_move_iterator(stack_.end() - count),
                                          std::make_move_iterator(stack_.end()));
                stack_.resize(stack_.size() - count);
                stack_.push_back(Value(std::move(elements)));
                break;
            }
            case OpCode::GET_INDEX: {
                Value indexValue = pop(line);
                Value object = pop(line);
                double position = requireNumber(indexValue, line, "array index");
                auto* array = std::get_if<std::shared_ptr<Array>>(&object);
                if (!array || !*array)
                    throw LumaError(line, "value of type " + valueTypeToString(object) +
                                              " is not indexable");
                const std::vector<Value>& elements = (*array)->elements;
                if (!std::isfinite(position) || position < 0 || position >= static_cast<double>(elements.size()))
                    throw LumaError(line, "array index " + formatNumber(position) +
                                              " is out of bounds (length " +
                                              std::to_string(elements.size()) + ")");
                stack_.push_back(elements[static_cast<size_t>(position)]);
                break;
            }
            case OpCode::SET_INDEX: {
                Value value = pop(line);
                Value indexValue = pop(line);
                Value object = pop(line);
                double position = requireNumber(indexValue, line, "array index");
                auto* array = std::get_if<std::shared_ptr<Array>>(&object);
                if (!array || !*array)
                    throw LumaError(line, "value of type " + valueTypeToString(object) +
                                              " is not indexable");
                std::vector<Value>& elements = (*array)->elements;
                if (!std::isfinite(position) || position < 0 || position >= static_cast<double>(elements.size()))
                    throw LumaError(line, "array index " + formatNumber(position) +
                                              " is out of bounds (length " +
                                              std::to_string(elements.size()) + ")");
                elements[static_cast<size_t>(position)] = std::move(value);
                break;
            }

            case OpCode::JUMP: {
                int32_t offset = static_cast<int16_t>(readOperand(code, frame.ip));
                frame.ip += 2;
                frame.ip += static_cast<size_t>(offset);
                break;
            }
            case OpCode::JUMP_IF_FALSE: {
                int32_t offset = static_cast<int16_t>(readOperand(code, frame.ip));
                frame.ip += 2;
                // Условие снимается со стека — ветви остаются сбалансированными
                // даже при раннем return из тела.
                if (!isTruthy(pop(line))) frame.ip += static_cast<size_t>(offset);
                break;
            }
            case OpCode::LOOP: {
                uint16_t offset = readOperand(code, frame.ip);
                frame.ip += 2;
                frame.ip -= offset;
                break;
            }

            case OpCode::ENTER_SCOPE: {
                scopes_.push_back(current_);
                current_ = std::make_shared<Environment>(current_);
                break;
            }
            case OpCode::EXIT_SCOPE: {
                current_ = scopes_.back();
                scopes_.pop_back();
                break;
            }

            case OpCode::CLOSURE: {
                uint16_t idx = readOperand(code, frame.ip);
                frame.ip += 2;
                auto* proto = std::get_if<std::shared_ptr<FunctionProto>>(
                    &code.constants[idx]);
                if (!proto || !*proto)
                    throw LumaError(line, "internal: closure without prototype");
                auto function = std::make_shared<Function>();
                function->name = (*proto)->name;
                function->params = (*proto)->params;
                function->chunk = (*proto)->body.get();
                function->closure = current_;  // замыкание: окружение определения
                stack_.push_back(Value(std::move(function)));
                break;
            }
            case OpCode::CALL: {
                uint16_t argc = readOperand(code, frame.ip);
                frame.ip += 2;
                if (stack_.size() < static_cast<size_t>(argc) + 1)
                    throw LumaError(line, "stack underflow");
                std::vector<Value> args(std::make_move_iterator(stack_.end() - argc),
                                        std::make_move_iterator(stack_.end()));
                stack_.resize(stack_.size() - argc);
                Value callee = pop(line);

                if (auto* native = std::get_if<std::shared_ptr<NativeFunction>>(&callee)) {
                    if (!*native)
                        throw LumaError(line, "value of type nil is not callable");
                    stack_.push_back((*native)->fn(output_, std::move(args), line));
                    break;
                }
                if (auto* fn = std::get_if<std::shared_ptr<Function>>(&callee)) {
                    if (!*fn)
                        throw LumaError(line, "value of type nil is not callable");
                    callFunction(**fn, std::move(args), line);
                    // После возврата из callFunction результат уже на стеке.
                    break;
                }
                throw LumaError(line, "value of type " + valueTypeToString(callee) +
                                          " is not callable");
            }
            case OpCode::RETURN: {
                Value result = pop(line);
                Frame& returning = frames_.back();
                scopes_.resize(returning.scopeStackSize);
                current_ = returning.savedEnv;
                // Стек значений обрезается до глубины входа в кадр: мусор
                // прерванных ветвлений (условия if) не утекает в вызывающий кадр.
                stack_.resize(returning.stackSize);
                frames_.pop_back();
                if (!frames_.empty()) stack_.push_back(std::move(result));
                break;
            }
            case OpCode::FAIL: {
                uint16_t idx = readOperand(code, frame.ip);
                frame.ip += 2;
                auto* message = std::get_if<std::string>(&code.constants[idx]);
                throw LumaError(line, message ? *message : "internal failure");
            }
        }
    }
}

void VM::callFunction(const Function& function, std::vector<Value> args, int line) {
    if (args.size() != function.params.size()) {
        throw LumaError(line, "function '" + function.name + "' expects " +
                                  std::to_string(function.params.size()) +
                                  " arguments, got " + std::to_string(args.size()));
    }
    if (!function.chunk)
        throw LumaError(line, "internal: function without bytecode");

    // Область параметров (родитель — замыкание); тело откроет свою область
    // первым ENTER_SCOPE чанка.
    auto callEnv = std::make_shared<Environment>(function.closure);
    for (size_t i = 0; i < args.size(); ++i)
        callEnv->define(function.params[i], std::move(args[i]), line);

    frames_.push_back(Frame{function.chunk, 0, current_, scopes_.size(), stack_.size()});
    current_ = std::move(callEnv);
}
