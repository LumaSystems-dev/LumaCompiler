#pragma once
#include <memory>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "ast.h"   // Block — тело пользовательской функции (tree-walking)
#include "chunk.h" // Chunk — тело пользовательской функции (VM)

class Environment;  // замыкание функции (определена в interpreter.h)

// Все составные значения языка — кучевые объекты, Value держит их через
// shared_ptr на неполный тип. Это обходит цикл "Value содержит контейнер
// значений Value" (алиас не может ссылаться на себя в собственном определении).
struct NativeFunction;
struct Function;
struct Array;

using Value = std::variant<std::monostate, bool, double, std::string,
                           std::shared_ptr<NativeFunction>, std::shared_ptr<Function>,
                           std::shared_ptr<Array>>;

// Реализация нативной функции. Аргументы уже вычислены; out — поток вывода
// (нужен print); line — строка вызова для ошибок.
using NativeFn = Value (*)(std::ostream& out, std::vector<Value> args, int line);

struct NativeFunction {
    std::string name;  // для сообщений об ошибках
    NativeFn fn;
};

// Пользовательская функция (Этап 7). Два бэкенда кладут тело в разные поля:
// tree-walking интерпретатор — body (поддерево AST), VM — chunk (байткод).
// Ровно один из указателей установлен. body — невладеющий указатель: AST
// живёт дольше исполнения.
// closure — окружение МЕСТА ОПРЕДЕЛЕНИЯ: вызов выполняется в дочерней области
// этого окружения (замыкание), даже когда определивший блок завершился.
struct Function {
    std::string name;
    std::vector<std::string> params;
    const Block* body = nullptr;
    const Chunk* chunk = nullptr;
    std::shared_ptr<Environment> closure;
};

// Массив (Этап 8): гибкий список значений любых типов. Ссылочная семантика:
// присваивание копирует shared_ptr — все "копии" разделяют один список.
struct Array {
    std::vector<Value> elements;
};

// Имя типа для сообщений об ошибках: "nil" | "boolean" | "number" | "string" |
// "array" | "function".
std::string valueTypeToString(const Value& value);

// Текстовое представление как его печатает print():
// nil / true / false / число без ".0" / строка без кавычек / [1, 2] / <function>.
std::string valueToString(const Value& value);

// Истинность (DESIGN.md 4.2): ложны только false и nil.
// Всё остальное — включая 0 и "" — истинно (семантика Lua, не Python/C).
bool isTruthy(const Value& value);
