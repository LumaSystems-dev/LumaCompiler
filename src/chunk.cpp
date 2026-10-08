#include "chunk.h"

#include <cstdio>
#include <cstring>
#include <istream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "error.h"
#include "util.h"

const char* opcodeToString(OpCode op) {
    switch (op) {
        case OpCode::CONSTANT:      return "CONSTANT";
        case OpCode::NIL:           return "NIL";
        case OpCode::TRUE:          return "TRUE";
        case OpCode::FALSE:         return "FALSE";
        case OpCode::POP:           return "POP";
        case OpCode::DUP:           return "DUP";
        case OpCode::DEFINE_VAR:    return "DEFINE_VAR";
        case OpCode::GET_VAR:       return "GET_VAR";
        case OpCode::SET_VAR:       return "SET_VAR";
        case OpCode::ADD:           return "ADD";
        case OpCode::SUB:           return "SUB";
        case OpCode::MUL:           return "MUL";
        case OpCode::DIV:           return "DIV";
        case OpCode::MOD:           return "MOD";
        case OpCode::CONCAT:        return "CONCAT";
        case OpCode::EQUAL:         return "EQUAL";
        case OpCode::LESS:          return "LESS";
        case OpCode::GREATER:       return "GREATER";
        case OpCode::NOT:           return "NOT";
        case OpCode::NEGATE:        return "NEGATE";
        case OpCode::ARRAY:         return "ARRAY";
        case OpCode::GET_INDEX:     return "GET_INDEX";
        case OpCode::SET_INDEX:     return "SET_INDEX";
        case OpCode::JUMP:          return "JUMP";
        case OpCode::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case OpCode::LOOP:          return "LOOP";
        case OpCode::ENTER_SCOPE:   return "ENTER_SCOPE";
        case OpCode::EXIT_SCOPE:    return "EXIT_SCOPE";
        case OpCode::CLOSURE:       return "CLOSURE";
        case OpCode::CALL:          return "CALL";
        case OpCode::RETURN:        return "RETURN";
        case OpCode::FAIL:          return "FAIL";
    };
    return "?";  // недостижимо
}

size_t instructionLength(OpCode op) {
    switch (op) {
        case OpCode::CONSTANT:
        case OpCode::DEFINE_VAR:
        case OpCode::GET_VAR:
        case OpCode::SET_VAR:
        case OpCode::ARRAY:
        case OpCode::JUMP:
        case OpCode::JUMP_IF_FALSE:
        case OpCode::LOOP:
        case OpCode::CLOSURE:
        case OpCode::CALL:
        case OpCode::FAIL:
            return 3;  // опкод + 16-битный операнд
        default:
            return 1;
    }
}

void Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::writeOp(OpCode op, int line) {
    write(static_cast<uint8_t>(op), line);
}

void Chunk::writeOperand(uint16_t value, int line) {
    write(static_cast<uint8_t>(value & 0xFF), line);          // младший байт
    write(static_cast<uint8_t>((value >> 8) & 0xFF), line);   // старший
}

uint16_t Chunk::addConstant(Constant value) {
    constants.push_back(std::move(value));
    // Пул не превышает 65535: компилятор проверяет индекс при добавлении.
    if (constants.size() > 0xFFFF) {
        constants.pop_back();
        throw LumaError(0, "too many bytecode constants (maximum 65535)");
    }
    return static_cast<uint16_t>(constants.size() - 1);
}

namespace {

std::string constantToString(const Constant& value) {
    if (auto* number = std::get_if<double>(&value)) return formatNumber(*number);
    if (auto* string = std::get_if<std::string>(&value)) return "\"" + *string + "\"";
    if (auto* proto = std::get_if<std::shared_ptr<FunctionProto>>(&value)) {
        if (*proto) return "proto '" + (*proto)->name + "'";
        return "<proto>";
    }
    return "?";
}

uint16_t readOperand(const std::vector<uint8_t>& code, size_t pos) {
    return static_cast<uint16_t>(code[pos] | (code[pos + 1] << 8));
}

}  // namespace

void disassembleChunk(const Chunk& chunk, const std::string& name, std::ostream& out) {
    out << "== " << name << " ==\n";

    size_t pos = 0;
    while (pos < chunk.code.size()) {
        OpCode op = static_cast<OpCode>(chunk.code[pos]);
        char header[32];
        std::snprintf(header, sizeof(header), "%04zu  %3d  %-14s", pos,
                      chunk.lines[pos], opcodeToString(op));
        out << header;

        switch (op) {
            case OpCode::CONSTANT:
            case OpCode::CLOSURE:
            case OpCode::FAIL: {
                uint16_t idx = readOperand(chunk.code, pos + 1);
                out << "  " << idx << "    ; "
                    << (idx < chunk.constants.size()
                            ? constantToString(chunk.constants[idx])
                            : "?")
                    << "\n";
                break;
            }
            case OpCode::DEFINE_VAR:
            case OpCode::GET_VAR:
            case OpCode::SET_VAR: {
                uint16_t idx = readOperand(chunk.code, pos + 1);
                out << "  " << idx << "    ; "
                    << (idx < chunk.constants.size() &&
                                std::get_if<std::string>(&chunk.constants[idx])
                            ? std::get<std::string>(chunk.constants[idx])
                            : "?")
                    << "\n";
                break;
            }
            case OpCode::ARRAY:
            case OpCode::CALL: {
                out << "  " << readOperand(chunk.code, pos + 1) << "\n";
                break;
            }
            case OpCode::JUMP:
            case OpCode::JUMP_IF_FALSE: {
                int32_t offset = static_cast<int16_t>(readOperand(chunk.code, pos + 1));
                char target[16];
                std::snprintf(target, sizeof(target), "%04zu", pos + 3 + offset);
                out << "  " << offset << "   ; -> " << target << "\n";
                break;
            }
            case OpCode::LOOP: {
                uint16_t offset = readOperand(chunk.code, pos + 1);
                char target[16];
                std::snprintf(target, sizeof(target), "%04zu", pos + 3 - offset);
                out << "  " << offset << "   ; -> " << target << "\n";
                break;
            }
            default:
                out << "\n";
                break;
        }

        pos += instructionLength(op);
    }

    // Вложенные прототипы функций — после listing-а родителя.
    for (const Constant& constant : chunk.constants) {
        if (auto* proto = std::get_if<std::shared_ptr<FunctionProto>>(&constant)) {
            if (*proto && (*proto)->body) {
                std::string label = "function '" + (*proto)->name + "(";
                for (size_t i = 0; i < (*proto)->params.size(); ++i) {
                    if (i > 0) label += ", ";
                    label += (*proto)->params[i];
                }
                disassembleChunk(*(*proto)->body, label + ")'", out);
            }
        }
    }
}

// --- Проверка байткода перед исполнением ---

namespace {

constexpr uint8_t kLastOpcode = static_cast<uint8_t>(OpCode::FAIL);

[[noreturn]] void invalidBytecode(const std::string& detail) {
    throw LumaError(0, "corrupted bytecode: " + detail);
}

uint16_t checkedOperand(const Chunk& chunk, size_t pos) {
    // Вызывается только после проверки длины инструкции.
    return static_cast<uint16_t>(chunk.code[pos] | (chunk.code[pos + 1] << 8));
}

struct ValidationState {
    size_t stackDepth;
    size_t scopeDepth;
};

void validateChunkImpl(const Chunk& chunk, std::unordered_set<const Chunk*>& active,
                       size_t depth) {
    if (depth > 128) invalidBytecode("function nesting is too deep");
    if (!active.insert(&chunk).second)
        invalidBytecode("cyclic function prototype");

    const size_t size = chunk.code.size();
    if (size == 0) invalidBytecode("empty code section");
    if (chunk.lines.size() != size) invalidBytecode("line table mismatch");
    if (chunk.constants.size() > 0xFFFF) invalidBytecode("too many constants");

    std::vector<bool> starts(size, false);
    std::vector<size_t> instructions;
    for (size_t pc = 0; pc < size;) {
        uint8_t raw = chunk.code[pc];
        if (raw > kLastOpcode) invalidBytecode("unknown opcode at offset " + std::to_string(pc));
        OpCode op = static_cast<OpCode>(raw);
        size_t length = instructionLength(op);
        if (length > size - pc)
            invalidBytecode("truncated instruction at offset " + std::to_string(pc));
        starts[pc] = true;
        instructions.push_back(pc);
        pc += length;
    }
    for (int line : chunk.lines)
        if (line < 0) invalidBytecode("negative source line number");
    for (const Constant& constant : chunk.constants) {
        if (auto proto = std::get_if<std::shared_ptr<FunctionProto>>(&constant)) {
            if (!*proto || !(*proto)->body) invalidBytecode("null function prototype");
            std::unordered_set<std::string> params;
            for (const std::string& param : (*proto)->params)
                if (!params.insert(param).second) invalidBytecode("duplicate function parameter");
            if ((*proto)->params.size() > 65535) invalidBytecode("too many function parameters");
            validateChunkImpl(*(*proto)->body, active, depth + 1);
        }
    }

    auto requireConstant = [&](size_t pc, uint16_t index, const char* purpose) -> const Constant& {
        if (index >= chunk.constants.size())
            invalidBytecode(std::string(purpose) + " constant index out of range at offset " +
                            std::to_string(pc));
        return chunk.constants[index];
    };
    auto requireTarget = [&](size_t pc, int64_t target) {
        if (target < 0 || target >= static_cast<int64_t>(size) ||
            !starts[static_cast<size_t>(target)])
            invalidBytecode("jump target is not an instruction boundary at offset " +
                            std::to_string(pc));
    };

    for (size_t pc : instructions) {
        OpCode op = static_cast<OpCode>(chunk.code[pc]);
        if (instructionLength(op) == 1) continue;
        uint16_t operand = checkedOperand(chunk, pc + 1);
        switch (op) {
            case OpCode::CONSTANT: {
                const Constant& value = requireConstant(pc, operand, "value");
                if (!std::holds_alternative<double>(value) &&
                    !std::holds_alternative<std::string>(value))
                    invalidBytecode("CONSTANT requires a number or string at offset " +
                                    std::to_string(pc));
                break;
            }
            case OpCode::DEFINE_VAR:
            case OpCode::GET_VAR:
            case OpCode::SET_VAR:
            case OpCode::FAIL:
                if (!std::holds_alternative<std::string>(
                        requireConstant(pc, operand, "string")))
                    invalidBytecode("instruction requires a string constant at offset " +
                                    std::to_string(pc));
                break;
            case OpCode::CLOSURE: {
                const Constant& value = requireConstant(pc, operand, "function");
                auto proto = std::get_if<std::shared_ptr<FunctionProto>>(&value);
                if (!proto || !*proto || !(*proto)->body)
                    invalidBytecode("CLOSURE requires a function prototype at offset " +
                                    std::to_string(pc));
                break;
            }
            case OpCode::JUMP:
            case OpCode::JUMP_IF_FALSE: {
                int32_t offset = static_cast<int16_t>(operand);
                if (offset < 0)
                    invalidBytecode("forward jump has a negative offset at offset " +
                                    std::to_string(pc));
                requireTarget(pc, static_cast<int64_t>(pc + 3) + offset);
                break;
            }
            case OpCode::LOOP:
                if (operand == 0 || operand > pc + 3)
                    invalidBytecode("invalid loop offset at offset " + std::to_string(pc));
                requireTarget(pc, static_cast<int64_t>(pc + 3) - operand);
                break;
            case OpCode::ARRAY:
            case OpCode::CALL:
                break;
            default:
                invalidBytecode("invalid instruction encoding at offset " + std::to_string(pc));
        }
    }

    std::unordered_map<size_t, ValidationState> states;
    std::vector<size_t> pending;
    auto schedule = [&](size_t pc, ValidationState state) {
        auto found = states.find(pc);
        if (found == states.end()) {
            states.emplace(pc, state);
            pending.push_back(pc);
        } else if (found->second.stackDepth != state.stackDepth ||
                   found->second.scopeDepth != state.scopeDepth) {
            invalidBytecode("inconsistent stack or scope at offset " + std::to_string(pc));
        }
    };
    auto next = [&](size_t pc, OpCode op, ValidationState state) {
        size_t target = pc + instructionLength(op);
        if (target >= size)
            invalidBytecode("execution falls past code at offset " + std::to_string(pc));
        schedule(target, state);
    };
    auto requireStack = [&](const ValidationState& state, size_t values, size_t pc) {
        if (state.stackDepth < values)
            invalidBytecode("stack underflow at offset " + std::to_string(pc));
    };

    schedule(0, {0, 0});
    while (!pending.empty()) {
        size_t pc = pending.back();
        pending.pop_back();
        ValidationState state = states.at(pc);
        OpCode op = static_cast<OpCode>(chunk.code[pc]);
        uint16_t operand = instructionLength(op) == 3 ? checkedOperand(chunk, pc + 1) : 0;

        switch (op) {
            case OpCode::CONSTANT:
            case OpCode::NIL:
            case OpCode::TRUE:
            case OpCode::FALSE:
            case OpCode::GET_VAR:
            case OpCode::CLOSURE:
                state.stackDepth++;
                next(pc, op, state);
                break;
            case OpCode::POP:
            case OpCode::DEFINE_VAR:
            case OpCode::SET_VAR:
                requireStack(state, 1, pc);
                state.stackDepth--;
                next(pc, op, state);
                break;
            case OpCode::DUP:
                requireStack(state, 1, pc);
                state.stackDepth++;
                next(pc, op, state);
                break;
            case OpCode::ADD: case OpCode::SUB: case OpCode::MUL: case OpCode::DIV:
            case OpCode::MOD: case OpCode::CONCAT: case OpCode::EQUAL: case OpCode::LESS:
            case OpCode::GREATER:
                requireStack(state, 2, pc);
                state.stackDepth--;
                next(pc, op, state);
                break;
            case OpCode::NOT:
            case OpCode::NEGATE:
                requireStack(state, 1, pc);
                next(pc, op, state);
                break;
            case OpCode::ARRAY:
                requireStack(state, operand, pc);
                state.stackDepth = state.stackDepth - operand + 1;
                next(pc, op, state);
                break;
            case OpCode::GET_INDEX:
                requireStack(state, 2, pc);
                state.stackDepth--;
                next(pc, op, state);
                break;
            case OpCode::SET_INDEX:
                requireStack(state, 3, pc);
                state.stackDepth -= 3;
                next(pc, op, state);
                break;
            case OpCode::JUMP:
            case OpCode::LOOP: {
                int64_t target = op == OpCode::LOOP
                    ? static_cast<int64_t>(pc + 3) - operand
                    : static_cast<int64_t>(pc + 3) + static_cast<int16_t>(operand);
                schedule(static_cast<size_t>(target), state);
                break;
            }
            case OpCode::JUMP_IF_FALSE: {
                requireStack(state, 1, pc);
                state.stackDepth--;
                int64_t target = static_cast<int64_t>(pc + 3) + static_cast<int16_t>(operand);
                schedule(static_cast<size_t>(target), state);
                next(pc, op, state);
                break;
            }
            case OpCode::ENTER_SCOPE:
                if (state.scopeDepth >= 128)
                    invalidBytecode("scope nesting is too deep at offset " + std::to_string(pc));
                state.scopeDepth++;
                next(pc, op, state);
                break;
            case OpCode::EXIT_SCOPE:
                if (state.scopeDepth == 0)
                    invalidBytecode("scope stack underflow at offset " + std::to_string(pc));
                state.scopeDepth--;
                next(pc, op, state);
                break;
            case OpCode::CALL:
                requireStack(state, static_cast<size_t>(operand) + 1, pc);
                state.stackDepth -= operand;
                next(pc, op, state);
                break;
            case OpCode::RETURN:
                requireStack(state, 1, pc);
                break;
            case OpCode::FAIL:
                break;
        }
    }
    active.erase(&chunk);
}

}  // namespace

void validateChunk(const Chunk& chunk) {
    std::unordered_set<const Chunk*> active;
    validateChunkImpl(chunk, active, 0);
}

// --- Сериализация: формат .lbc (см. chunk.h) ---

namespace {

constexpr char kMagic[4] = {'L', 'B', 'C', '\x01'};
constexpr uint8_t kConstDouble = 1;
constexpr uint8_t kConstString = 2;
constexpr uint8_t kConstProto = 3;
constexpr size_t kMaxArtifactBytes = 64 * 1024 * 1024;
constexpr size_t kMaxCodeBytes = 16 * 1024 * 1024;
constexpr size_t kMaxStringBytes = 8 * 1024 * 1024;
constexpr size_t kMaxPrototypeDepth = 128;

struct ReadBudget {
    size_t remaining = kMaxArtifactBytes;

    void claim(size_t bytes) {
        if (bytes > remaining)
            throw LumaError(0, "corrupted bytecode file: artifact is too large");
        remaining -= bytes;
    }
};

void writeU32(std::ostream& out, uint32_t value) {
    char bytes[4] = {
        static_cast<char>(value & 0xFF), static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF), static_cast<char>((value >> 24) & 0xFF)};
    out.write(bytes, 4);
}

uint32_t readU32(std::istream& in) {
    unsigned char bytes[4];
    in.read(reinterpret_cast<char*>(bytes), 4);
    if (!in) throw LumaError(0, "corrupted bytecode file: unexpected end");
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

void writeString(std::ostream& out, const std::string& value) {
    writeU32(out, static_cast<uint32_t>(value.size()));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

std::string readString(std::istream& in, ReadBudget& budget) {
    uint32_t length = readU32(in);
    if (length > kMaxStringBytes)
        throw LumaError(0, "corrupted bytecode file: string is too large");
    budget.claim(static_cast<size_t>(length) + 4);
    std::string value(length, '\0');
    if (length > 0) in.read(value.data(), static_cast<std::streamsize>(length));
    if (!in) throw LumaError(0, "corrupted bytecode file: unexpected end");
    return value;
}

void writeDouble(std::ostream& out, double value) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(out, static_cast<uint32_t>(bits & 0xFFFFFFFF));
    writeU32(out, static_cast<uint32_t>(bits >> 32));
}

double readDouble(std::istream& in) {
    uint32_t low = readU32(in);
    uint32_t high = readU32(in);
    uint64_t bits = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void writeChunkBody(const Chunk& chunk, std::ostream& out) {
    writeU32(out, static_cast<uint32_t>(chunk.code.size()));
    if (!chunk.code.empty())
        out.write(reinterpret_cast<const char*>(chunk.code.data()),
                  static_cast<std::streamsize>(chunk.code.size()));

    writeU32(out, static_cast<uint32_t>(chunk.lines.size()));
    for (int line : chunk.lines) writeU32(out, static_cast<uint32_t>(line));

    writeU32(out, static_cast<uint32_t>(chunk.constants.size()));
    for (const Constant& constant : chunk.constants) {
        if (auto* number = std::get_if<double>(&constant)) {
            out.put(static_cast<char>(kConstDouble));
            writeDouble(out, *number);
        } else if (auto* string = std::get_if<std::string>(&constant)) {
            out.put(static_cast<char>(kConstString));
            writeString(out, *string);
        } else if (auto* proto = std::get_if<std::shared_ptr<FunctionProto>>(&constant)) {
            out.put(static_cast<char>(kConstProto));
            writeString(out, (*proto)->name);
            writeU32(out, static_cast<uint32_t>((*proto)->params.size()));
            for (const std::string& param : (*proto)->params) writeString(out, param);
            writeChunkBody(*(*proto)->body, out);  // тело функции — рекурсивно
        }
    }

    if (!out) throw LumaError(0, "failed to write bytecode file");
}

std::shared_ptr<Chunk> readChunkBody(std::istream& in, ReadBudget& budget, size_t depth) {
    if (depth > kMaxPrototypeDepth)
        throw LumaError(0, "corrupted bytecode file: function nesting is too deep");
    auto chunk = std::make_shared<Chunk>();
    budget.claim(12 + sizeof(Chunk));

    uint32_t codeSize = readU32(in);
    if (codeSize > kMaxCodeBytes)
        throw LumaError(0, "corrupted bytecode file: code section is too large");
    budget.claim(codeSize);
    chunk->code.resize(codeSize);
    if (codeSize > 0)
        in.read(reinterpret_cast<char*>(chunk->code.data()),
                static_cast<std::streamsize>(codeSize));

    uint32_t lineCount = readU32(in);
    if (lineCount != codeSize)
        throw LumaError(0, "corrupted bytecode file: line table mismatch");
    if (lineCount > budget.remaining / sizeof(uint32_t))
        throw LumaError(0, "corrupted bytecode file: artifact is too large");
    budget.claim(static_cast<size_t>(lineCount) * sizeof(uint32_t));
    chunk->lines.reserve(lineCount);
    for (uint32_t i = 0; i < lineCount; ++i)
        chunk->lines.push_back(static_cast<int>(readU32(in)));

    uint32_t constantCount = readU32(in);
    if (constantCount > 0xFFFF)
        throw LumaError(0, "corrupted bytecode file: too many constants");
    budget.claim(static_cast<size_t>(constantCount) * sizeof(Constant));
    chunk->constants.reserve(constantCount);
    for (uint32_t i = 0; i < constantCount; ++i) {
        budget.claim(1);
        int type = in.get();
        if (!in) throw LumaError(0, "corrupted bytecode file: unexpected end");
        switch (type) {
            case kConstDouble:
                budget.claim(sizeof(uint64_t));
                chunk->constants.push_back(Constant(readDouble(in)));
                break;
            case kConstString:
                chunk->constants.push_back(Constant(readString(in, budget)));
                break;
            case kConstProto: {
                budget.claim(sizeof(FunctionProto) + 4);
                auto proto = std::make_shared<FunctionProto>();
                proto->name = readString(in, budget);
                uint32_t paramCount = readU32(in);
                if (paramCount > 0xFFFF)
                    throw LumaError(0,
                                    "corrupted bytecode file: too many function parameters");
                budget.claim(static_cast<size_t>(paramCount) * sizeof(std::string));
                proto->params.reserve(paramCount);
                for (uint32_t p = 0; p < paramCount; ++p)
                    proto->params.push_back(readString(in, budget));
                proto->body = readChunkBody(in, budget, depth + 1);  // recursive body
                chunk->constants.push_back(Constant(std::move(proto)));
                break;
            }
            default:
                throw LumaError(0, "corrupted bytecode file: unknown constant type");
        }
    }
    return chunk;
}

}  // namespace

void writeChunk(const Chunk& chunk, std::ostream& out) {
    validateChunk(chunk);
    out.write(kMagic, 4);
    writeChunkBody(chunk, out);
    if (!out) throw LumaError(0, "failed to write bytecode file");
}

std::shared_ptr<Chunk> readChunk(std::istream& in) {
    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, kMagic, 4) != 0) {
        if (in && std::memcmp(magic, "LBC", 3) == 0)
            throw LumaError(0, "unsupported Luma bytecode format version");
        throw LumaError(0, "not a Luma bytecode file (bad magic)");
    }
    ReadBudget budget;
    std::shared_ptr<Chunk> chunk = readChunkBody(in, budget, 0);
    if (in.peek() != std::char_traits<char>::eof())
        throw LumaError(0, "corrupted bytecode file: trailing data");
    validateChunk(*chunk);
    return chunk;
}
