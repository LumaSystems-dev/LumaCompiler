#pragma once
#include <memory>
#include <ostream>
#include <vector>

#include "chunk.h"
#include "interpreter.h"  // Environment
#include "value.h"

// Стековая VM Luma (Этап 11): исполняет байткод компилятора.
//
// Устройство:
//   * стек значений — только для вычисления выражений (операнды опкодов);
//   * переменные живут в цепочке окружений: ENTER_SCOPE/EXIT_SCOPE создают
//     дочерние области, CLOSURE захватывает текущее окружение (замыкание);
//   * кадры вызовов — в куче: рекурсия Luma не съедает стек C++;
//   * встроенные функции (print/len/...) — нативные значения в окружении.
class VM {
public:
    explicit VM(std::ostream& output = std::cout);

    // Исполнить чанк (главную программу). Ошибки — LumaError со строкой
    // инструкции из трассировки чанка.
    void run(const Chunk& chunk);

private:
    struct Frame {
        const Chunk* chunk;
        size_t ip;  // индекс следующего байта
        std::shared_ptr<Environment> savedEnv;  // окружение вызывавшего
        size_t scopeStackSize;                  // размер scopes_ на входе в кадр
        size_t stackSize;  // глубина стека значений на входе в кадр:
                           // RETURN обрезает остатки (например, снятые
                           // JUMP_IF_FALSE условия прерванных ветвлений)
    };

    Value pop(int line);
    void callFunction(const Function& function, std::vector<Value> args, int line);

    std::vector<Value> stack_;
    std::vector<std::shared_ptr<Environment>> scopes_;  // стек входов в области
    std::shared_ptr<Environment> current_;
    std::vector<Frame> frames_;
    std::ostream& output_;
};
