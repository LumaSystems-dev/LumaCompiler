#pragma once
#include <memory>
#include <vector>

#include "ast.h"
#include "chunk.h"

// Компилятор AST → bytecode (Этап 10). Выполняется ПОСЛЕ Analyzer:
// компилятор вправе полагаться на корректность семантики.
//
// Стратегия обхода зеркальна tree-walking интерпретатору, поэтому байткод
// воспроизводит ту же семантику (пер-итерационный захват переменной цикла,
// свежие области на итерацию, короткое замыкание and/or):
//   * области видимости — ENTER_SCOPE/EXIT_SCOPE (скрытые переменные цикла
//     for имеют имена "@i"/"@to"/"@step" — '@' невозможен в идентификаторах);
//   * замыкания — CLOSURE: VM свяжет прототип с текущим окружением;
//   * break/continue — выход из открытых областей + переход, патчи адресов
//     собираются в LoopContext;
//   * имена переменных — индексы строковых констант.
class Compiler {
public:
    // Компилирует программу в чанк "<main>". Бросает LumaError только при
    // внутренних ошибках (семантика уже проверена анализатором).
    std::shared_ptr<Chunk> compile(const Program& program);

private:
    struct LoopContext {
        size_t startAddr;  // адрес начала условия — цель LOOP
        int scopeDepth;    // глубина областей на момент Lstart
        bool hasIncrement;  // for: continue прыгает на инкремент, а не на условие
        std::vector<size_t> endPatches;  // незаплатанные JUMP на Lend (break)
        std::vector<size_t> incPatches;  // незаплатанные JUMP на инкремент (continue)
    };

    void exec(const Stmt& stmt);
    void eval(const Expr& expr);
    void ifStmt(const IfStmt& node);
    void whileStmt(const WhileStmt& node);
    void forStmt(const ForStmt& node);
    void functionStmt(const FunctionStmt& node);

    uint16_t addConstant(Constant value);
    uint16_t varIndex(const std::string& name);
    void emitOp(OpCode op);
    void writeOp(OpCode op, int line);
    void writeOperand(uint16_t value, int line);
    void emitVarOp(OpCode op, uint16_t idx);
    size_t emitJump(OpCode op);   // позиция операнда для patchJump
    void patchJump(size_t operandPos);
    void emitLoop(size_t startAddr);

    Chunk* chunk_ = nullptr;  // текущий компилируемый чанк
    int line_ = 0;            // строка исходника для записи байтов
    int scopeDepth_ = 0;      // сколько ENTER_SCOPE открыто (для break/continue)
    std::vector<LoopContext> loops_;
};
