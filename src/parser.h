#pragma once
#include <cstddef>
#include <vector>

#include "ast.h"
#include "token.h"

// Синтаксический анализатор: рекурсивный спуск по токенам.
//
// Стейтменты (с Этапа 4):
//   program    → statement* EOF
//   statement  → letStmt | ifStmt | whileStmt | forStmt | funcDecl | returnStmt
//              | breakStmt | continueStmt | assignment | exprStmt | ";"
//   letStmt    → "let" IDENT "=" expression
//   assignment → target "=" expression        (парсер валидирует цель)
//   target     → IDENT | postfix-индексное выражение: a[i], m[i][j]
//   exprStmt   → expression ";"?   с проверкой: только вызов функции (DESIGN.md 4.9)
//   ifStmt     → "if" expression "then" block ("elseif" expression "then" block)*
//                ("else" block)? "end"
//   whileStmt  → "while" expression "do" block "end"
//   forStmt    → "for" IDENT "in" bound ".." bound ("by" expression)? "do" block "end"
//   funcDecl   → "function" IDENT "(" params? ")" block "end"
//   returnStmt → "return" expression?
//   breakStmt  → "break"
//   continueStmt → "continue"
//   importStmt → "import" STRING         (Luma 0.1.1; путь — только строковый
//                                         литерал; ';' необязателен; разрешение
//                                         путей — отдельный проход imports.h)
//
// Семантические ограничения (return/break/continue — где разрешены, дубликаты
// параметров, объявления переменных, арность) проверяет Analyzer (Этап 9):
// парсер отвечает за структуру, анализатор — за смысл.
//
// Присваивание узнаётся ПОСЛЕ разбора выражения: expression() останавливается
// на '=', дальше цель проверяется на имя/индекс. Поэтому "x == 5" целиком
// съедается выражением как сравнение, а "x = 5" — присваивание.
//
// bound — выражение уровня additive (арифметика без сравнений и ".."):
// поэтому ".." однозначно служит разделителем диапазона — "0..n-1" даёт
// границы 0 и n-1, а не конкатенацию.
//
// Приоритет выражений (DESIGN.md 3.1, от слабого к сильному):
//   orExpr         → andExpr ("or" andExpr)*
//   andExpr        → notExpr ("and" notExpr)*
//   notExpr        → "not" notExpr | comparison        not слабее сравнений!
//   comparison     → concat (("=="|"<"|">"|"<="|">="|"!=") concat)*
//   concat         → additive (".." concat)?           правоассоциативный
//   additive       → multiplicative (("+"|"-") multiplicative)*
//   multiplicative → unary (("*"|"/"|"%") unary)*
//   unary          → "-" unary | postfix
//   postfix        → primary ("(" args? ")" | "[" expression "]")*   вызовы и a[i]
//   primary        → NUMBER | STRING | true | false | nil | IDENT
//                  | "(" expression ")" | "[" args? "]"               массив (Этап 8)
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Разобрать файл целиком: последовательность стейтментов до конца файла.
    Program parseProgram();

    // Разобрать одно выражение и потребовать конец файла.
    // Старое поведение parseProgram; используется luma eval и тестами.
    Expr parseSingleExpression();

    // Разобрать одно выражение с текущей позиции (используется тестами).
    Expr parseExpression();

private:
    Stmt statement();
    Stmt letStmt();
    Stmt ifStmt();
    Stmt whileStmt();
    Stmt forStmt();
    Stmt functionStmt();
    Stmt returnStmt();
    Stmt breakStmt();
    Stmt continueStmt();
    Stmt importStmt();

    // Может ли токен начинать выражение (для "return" без значения).
    bool startsExpression() const;

    // Стейтменты тела блока — до else/elseif/end (или EOF: ошибка позже,
    // в expect 'end', с точным сообщением).
    std::vector<Stmt> blockBody();

    Expr expression();  // вход в цепочку приоритетов (= orExpr)

    Expr orExpr();
    Expr andExpr();
    Expr notExpr();
    Expr comparison();
    Expr concat();
    Expr additive();
    Expr multiplicative();
    Expr unary();
    Expr postfix();
    Expr primary();

    // Токен на offset позиций вперёд (за концом потока — EOF).
    const Token& peek(int offset = 0) const;
    const Token& advance();
    bool check(TokenKind kind) const;
    const Token& expect(TokenKind kind, const char* what);

    std::vector<Token> tokens;
    size_t pos = 0;
};
