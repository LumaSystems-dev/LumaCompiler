#pragma once
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

// Байткод Luma (Этап 10): чанк + пул констант + строки исходника.
//
// Формат: однобайтовые опкоды; операнды — 16-битные little-endian
// (индексы пула констант, счётчики, ОТНОСИТЕЛЬНЫЕ смещения переходов
// относительно конца инструкции).
//
// ВАЖНО о семантике: переменные адресуются ИМЕНЕМ (индекс строковой константы),
// а области видимости — инструкциями ENTER_SCOPE/EXIT_SCOPE. Это сознательное
// отклонение от черновика DESIGN.md (GET_LOCAL/GET_GLOBAL): замыкания Luma
// захватывают окружение целиком и захват переменной цикла происходит ПО
// ИТЕРАЦИЯМ — окружения в куче воспроизводят семантику tree-walking
// интерпретатора 1:1, что позволяет сверять VM этапа 11 с интерпретатором
// на всех тестах. Слотовая машина с upvalues — оптимизация после этапа 12.

enum class OpCode : uint8_t {
    CONSTANT,       // idx: push constants[idx]
    NIL,            // push nil
    TRUE,           // push true
    FALSE,          // push false
    POP,            // снять и отбросить вершину
    DUP,            // продублировать вершину (для and/or)

    DEFINE_VAR,     // idx: объявить имя constants[idx] в ТЕКУЩЕМ окружении (значение с вершины)
    GET_VAR,        // idx: прочитать имя constants[idx] (поиск по цепочке окружений)
    SET_VAR,        // idx: присвоить существующему имени constants[idx]

    ADD,            // снять b, a; push a + b      (только числа)
    SUB,            // снять b, a; push a - b
    MUL,            // снять b, a; push a * b
    DIV,            // снять b, a; push a / b      (IEEE 754)
    MOD,            // снять b, a; push a % b      (остаток со знаком делителя)
    CONCAT,         // снять b, a; push a .. b     (строки/числа)

    EQUAL,          // снять b, a; push a == b     (разные типы всегда неравны)
    LESS,           // снять b, a; push a < b      (числа или строки)
    GREATER,        // снять b, a; push a > b
    NOT,            // push !truthy(вершина)
    NEGATE,         // push -вершина (число)

    ARRAY,          // n: снять n элементов → массив (последний снятый — элемент [0])
    GET_INDEX,      // снять idx, obj; push obj[idx]   (с проверкой границ)
    SET_INDEX,      // снять value, idx, obj; obj[idx] = value

    JUMP,           // off: безусловный переход на (конец инструкции + off)
    JUMP_IF_FALSE,  // off: СНЯТЬ вершину; если была ложна — переход на (конец + off)
    LOOP,           // off: переход назад на (конец инструкции - off)

    ENTER_SCOPE,    // push дочернего окружения (новая область видимости)
    EXIT_SCOPE,     // pop окружения

    CLOSURE,        // idx: constants[idx] = прототип → push Function(closure = текущее окружение)
    CALL,           // n: снять n аргументов и вызываемое; push результат
    RETURN,         // снять значение; вернуться в вызывающий кадр
    FAIL,           // idx: runtime-ошибка constants[idx] (сообщение)
};
// Примечание: print/len/push/str/num — НЕ опкоды, а встроенные функции в
// окружении; их вызов компилируется в GET_VAR + CALL как у любых функций.

const char* opcodeToString(OpCode op);

// Длина инструкции в байтах: 1 байт опкод + (если есть) 2 байта операнда.
size_t instructionLength(OpCode op);

struct Chunk;

// Прототип функции: тело скомпилировано в отдельный чанк. CLOSURE превращает
// прототип в значение Function, захватывая текущее окружение (замыкание).
struct FunctionProto {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<Chunk> body;
};

using Constant = std::variant<double, std::string, std::shared_ptr<FunctionProto>>;

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Constant> constants;
    std::vector<int> lines;  // строка исходника для каждого байта кода

    void write(uint8_t byte, int line);
    void writeOp(OpCode op, int line);
    void writeOperand(uint16_t value, int line);
    uint16_t addConstant(Constant value);
    size_t size() const { return code.size(); }
};

// Человекочитаемый дамп чанка (и вложенных прототипов) в поток.
void disassembleChunk(const Chunk& chunk, const std::string& name, std::ostream& out);

// Проверяет структурную корректность чанка до исполнения. Нужен как для
// прочитанных .lbc, так и для Chunk, созданных внешним C++ кодом: VM не должна
// доверять опкодам, смещениям и индексам констант.
void validateChunk(const Chunk& chunk);

// --- Сериализация (Этап 12): скомпилированная программа как файл-артефакт ---

// Формат .lbc: magic "LBC\x01", затем чанк рекурсивно (код, строки исходника,
// константы; прототип функции несёт имя, параметры и своё тело-чанк).
// Ошибки записи/чтения — LumaError (line == 0: не привязано к исходнику).
void writeChunk(const Chunk& chunk, std::ostream& out);
std::shared_ptr<Chunk> readChunk(std::istream& in);
