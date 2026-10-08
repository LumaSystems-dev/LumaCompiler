#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"

// Семантический анализ (Этап 9): проход по AST ДО исполнения.
//
// Модель областей повторяет рантайм (DESIGN.md 4.6), поэтому ошибки ловятся
// ровно там же, где они возникли бы при выполнении, — только раньше:
//   * чтение/запись необъявленной переменной;
//   * повторное объявление имени в одной области (теневание во вложенной — ок);
//   * арность вызовов, если вызываемая функция известна статически
//     (пользовательские функции и встроенные; print вариадичен);
//   * return вне функции; break/continue вне цикла и не сквозь границу
//     функции (переезжают из парсера: парсер — структура, анализатор — смысл).
//
// Сообщения и номера строк совпадают с рантайм-проверками — анализатор
// просто срабатывает раньше них.
class Analyzer {
public:
    void analyze(const Program& program);

private:
    struct Scope {
        // фиксированная арность, если имя — известная функция; nullopt —
        // обычная переменная или вариадичная функция (print)
        std::unordered_map<std::string, std::optional<size_t>> names;
    };

    void exec(const Stmt& stmt);
    void eval(const Expr& expr);

    void declare(const std::string& name, std::optional<size_t> arity, int line);
    const std::optional<size_t>* resolve(const std::string& name) const;

    std::vector<Scope> scopes;
    int loopDepth = 0;
    int functionDepth = 0;
};
