#pragma once
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "token.h"

// Узлы дерева разбора (AST).
//
// Представление: каждый узел — маленькая структура, а Expr/Stmt — обёртки над
// std::variant из узлов. Потребители (дамп, вычислитель, анализатор,
// компилятор bytecode) разбирают вариант через std::get_if — это явный и
// читаемый dispatch без visitor-шаблонов.
//
// Обход рекурсии: узлы ссылаются на детей через unique_ptr<Expr>, где Expr ещё
// не определён (forward declaration ниже). Это безопасно: деструкторы
// инстанцируются уже после полного определения Expr. Каждый узел хранит
// строку исходника — для сообщений об ошибках.

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

// Литерал: число, строка, true, false или nil (различаем по token.type).
struct LiteralExpr {
    Token token;
    int line = 0;
};

// Ссылка на переменную.
struct IdentifierExpr {
    std::string name;
    int line = 0;
    int column = 0;
};

// Скобки вокруг выражения. Сохраняем их как отдельный узел, чтобы дамп AST
// показывал явную группировку; на вычисление скобки не влияют.
struct GroupingExpr {
    ExprPtr expression;
    int line = 0;
};

// Унарный оператор: "-" или "not".
struct UnaryExpr {
    Token op;
    ExprPtr operand;
    int line = 0;
};

// Бинарный оператор: арифметика, сравнения, "..", "and"/"or".
struct BinaryExpr {
    Token op;
    ExprPtr left;
    ExprPtr right;
    int line = 0;
};

// Вызов функции: callee(arg, ...). Постфиксный уровень.
struct CallExpr {
    ExprPtr callee;
    std::vector<Expr> args;
    Token paren;  // '(' — строка для ошибок вызова
    int line = 0;
};

// Литерал массива: [a, b, c] или [] (Этап 8).
struct ArrayExpr {
    std::vector<Expr> elements;
    int line = 0;
};

// Индексация: object[index]. Постфиксный уровень, можно в цепочке: m[i][j].
struct IndexExpr {
    ExprPtr object;
    ExprPtr index;
    int line = 0;
};

// Обёртка: Expr = ровно один из узлов выше.
struct Expr {
    std::variant<LiteralExpr, IdentifierExpr, GroupingExpr, UnaryExpr, BinaryExpr,
                 CallExpr, ArrayExpr, IndexExpr>
        value;

    Expr() : value(LiteralExpr{}) {}
    template <typename Node>
    Expr(Node node) : value(std::move(node)) {}
};

// --- Стейтменты. Ветвления появились на Этапе 5; циклы — на Этапе 6. ---

struct Stmt;  // обёртка стейтмента, определена ниже

// Тело блока = последовательность стейтментов. Каждый Block — единица
// области видимости: переменные, объявленные в нём, наружу не видны.
// Stmt здесь ещё неполный; std::vector поддерживает это в C++17, а деструкторы
// инстанцируются позже — когда Stmt уже полон (тот же приём, что у Expr).
struct Block {
    std::vector<Stmt> statements;
};

// Ветвь if/elseif/else. У ветви else условие == nullptr.
struct IfBranch {
    ExprPtr condition;
    std::unique_ptr<Block> body;
};

// if cond then ... elseif cond then ... else ... end
// Цепочка elseif хранится явно списком ветвей — в точности как в грамматике.
struct IfStmt {
    std::vector<IfBranch> branches;
    int line = 0;  // строка ключевого слова 'if'
};

// Выражение-оператор. По спецификации (DESIGN.md 4.9) разрешены только вызовы:
// "x + 1" отдельным оператором — ошибка парсера.
struct ExprStmt {
    ExprPtr expression;
    int line = 0;
};

// let name = initializer
struct LetStmt {
    Token name;  // IDENTIFIER
    ExprPtr initializer;
    int line = 0;
};

// Присваивание: target = value. Цель — выражение-имя (x = ...) или индекс
// (a[i] = ..., m[i][j] = ...) — валидируется парсером (DESIGN.md: target).
struct AssignStmt {
    ExprPtr target;
    ExprPtr value;
    int line = 0;
};

// while cond do ... end
struct WhileStmt {
    ExprPtr condition;
    std::unique_ptr<Block> body;
    int line = 0;
};

// for i in from..to (by step) do ... end — полуоткрытый диапазон:
// from включается, to не входит. step == nullptr означает шаг +1.
// Переменная цикла живёт в своей области на каждую итерацию; изменение i
// в теле не влияет на ход цикла (счётчик внутренний, как в Lua и Python).
struct ForStmt {
    Token variable;  // IDENTIFIER — имя переменной цикла
    ExprPtr from;
    ExprPtr to;
    ExprPtr step;
    std::unique_ptr<Block> body;
    int line = 0;
};

// break / continue — только внутри циклов (проверяет парсер, Этап 6).
struct BreakStmt {
    int line = 0;
};

struct ContinueStmt {
    int line = 0;
};

// function name(params) ... end — именованное объявление функции (Этап 7).
// Анонимные функции-выражения отложены (DESIGN.md 9).
struct FunctionStmt {
    Token name;  // IDENTIFIER
    std::vector<Token> params;
    std::unique_ptr<Block> body;
    int line = 0;
};

// return [value] — только внутри функции (проверяет парсер).
// value == nullptr → return nil.
struct ReturnStmt {
    ExprPtr value;
    int line = 0;
};

// import "path.luma"; — Luma 0.1.1. Путь — только строковый литерал.
// Обрабатывается ДО анализатора отдельным проходом (imports.h): стейтменты
// модуля подставляются в точку импорта, и узлов import в дереве не остаётся.
// Разрешён только на верхнем уровне модуля.
struct ImportStmt {
    std::string path;  // путь как записан в исходнике
    int line = 0;
};

// Обёртка: Stmt = ровно один из стейтментов выше.
struct Stmt {
    std::variant<ExprStmt, LetStmt, AssignStmt, IfStmt, WhileStmt, ForStmt, BreakStmt,
                 ContinueStmt, FunctionStmt, ReturnStmt, ImportStmt>
        value;
    // Заполняется ImportResolver'ом для диагностик: исходный файл либо
    // цепочка импортов, из которой попал statement. На семантику не влияет.
    std::string origin;
    std::string sourceFile;

    Stmt() : value(ExprStmt{}) {}
    template <typename Node>
    Stmt(Node node) : value(std::move(node)) {}
};

// Программа = последовательность стейтментов.
struct Program {
    std::vector<Stmt> statements;
};
