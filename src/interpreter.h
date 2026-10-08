#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"
#include "value.h"

// Область видимости: словарь переменных + ссылка на внешнюю область.
// Цепочка областей образует лексическое окружение: define() объявляет в
// ТЕКУЩЕЙ области (повтор в ней — ошибка; теневание во вложенной разрешено —
// DESIGN.md 4.6), assign()/get() ищут имя наружу по цепочке родителей.
//
// Области живут в куче (shared_ptr): замыкание хранит окружение места
// определения и переживает завершение блока (Этап 7).
class Environment {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr);

    // Объявить новую переменную в ТЕКУЩЕЙ области.
    // На Этапе 9 эту проверку заберёт себе анализатор — до выполнения.
    void define(const std::string& name, Value value, int line);

    // Присвоить существующей переменной (поиск по цепочке). Необъявленная — ошибка.
    void assign(const std::string& name, Value value, int line);

    // Прочитать переменную (поиск по цепочке). Необъявленная — ошибка.
    Value get(const std::string& name, int line) const;

private:
    std::unordered_map<std::string, Value> variables;
    std::shared_ptr<Environment> parent;
};

// Tree-walking интерпретатор.
class Interpreter {
public:
    // RAII-вход в дочернюю область: подменяет текущую область интерпретатора
    // и восстанавливает прежнюю при любом выходе (включая исключения).
    // Вложенный класс — чтобы трогать приватное состояние.
    class ScopedEnvironment {
    public:
        ScopedEnvironment(Interpreter& interp, std::shared_ptr<Environment> next);
        ~ScopedEnvironment();
        ScopedEnvironment(const ScopedEnvironment&) = delete;
        ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    private:
        Interpreter& interpreter;
        std::shared_ptr<Environment> previous;
    };

    // Вывод print() идёт в поток; тесты подставляют ostringstream.
    explicit Interpreter(std::ostream& output = std::cout);

    void run(const Program& program);

    // Вычислить выражение. Ошибки типов — LumaError со строкой узла-оператора.
    Value eval(const Expr& expr);

private:
    void exec(const Stmt& stmt);
    void assignTo(const Expr& target, Value value, int line);

    Value evalUnary(const UnaryExpr& node);
    Value evalBinary(const BinaryExpr& node);
    Value evalCall(const CallExpr& node);
    Value callFunction(const Function& function, std::vector<Value> args, int line);

    // Текущая область. Вход в блок/функцию подменяет её дочерней областью
    // (ScopedEnvironment восстанавливает при любом выходе).
    std::shared_ptr<Environment> environment;
    std::ostream& output;
};
