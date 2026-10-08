#include "parser.h"

#include <string>
#include <utility>

#include "error.h"

namespace {

// Человекочитаемое описание токена для сообщений об ошибках:
// "expected ')' but got end of file".
std::string tokenDesc(const Token& token) {
    switch (token.type) {
        case TokenKind::END_OF_FILE:
            return "end of file";
        case TokenKind::STRING:
            return "string literal";
        default:
            return "'" + token.lexeme + "'";
    }
}

}  // namespace

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

Program Parser::parseProgram() {
    Program program;
    while (!check(TokenKind::END_OF_FILE)) {
        // ";" — необязательный пустой оператор (DESIGN.md 2.3): узел не нужен.
        if (check(TokenKind::SEMICOLON)) {
            advance();
            continue;
        }
        try {
            program.statements.push_back(statement());
        } catch (LumaError& error) {
            if (error.column == 0) error.column = peek().column;
            throw;
        }
    }
    return program;
}

Expr Parser::parseSingleExpression() {
    Expr expr = expression();
    if (!check(TokenKind::END_OF_FILE))
        throw LumaError(peek().line,
                        "unexpected " + tokenDesc(peek()) + " after expression");
    return expr;
}

Expr Parser::parseExpression() {
    return expression();
}

Expr Parser::expression() {
    return orExpr();
}

// --- Стейтменты ---

// dispatch по первому токену; всё, что не ключевое слово, — присваивание
// или выражение-оператор (различаются после разбора выражения, см. ниже).
Stmt Parser::statement() {
    if (check(TokenKind::KW_LET)) return letStmt();
    if (check(TokenKind::KW_IF)) return ifStmt();
    if (check(TokenKind::KW_WHILE)) return whileStmt();
    if (check(TokenKind::KW_FOR)) return forStmt();
    if (check(TokenKind::KW_FUNCTION)) return functionStmt();
    if (check(TokenKind::KW_RETURN)) return returnStmt();
    if (check(TokenKind::KW_BREAK)) return breakStmt();
    if (check(TokenKind::KW_CONTINUE)) return continueStmt();
    if (check(TokenKind::KW_IMPORT)) return importStmt();

    // Присваивание или выражение-оператор: разбираем выражение; если дальше
    // '=', это присваивание, и цель обязана быть именем или индексом.
    // "x == 5" целиком съедается выражением как сравнение и сюда не попадает.
    int line = peek().line;
    Expr expr = expression();
    if (check(TokenKind::EQUAL)) {
        advance();
        Expr value = expression();
        if (!std::holds_alternative<IdentifierExpr>(expr.value) &&
            !std::holds_alternative<IndexExpr>(expr.value))
            throw LumaError(line, "invalid assignment target");

        AssignStmt node;
        node.target = std::make_unique<Expr>(std::move(expr));
        node.value = std::make_unique<Expr>(std::move(value));
        node.line = line;
        if (check(TokenKind::SEMICOLON)) advance();
        return Stmt(std::move(node));
    }

    if (!std::holds_alternative<CallExpr>(expr.value))
        throw LumaError(line, "only function calls can be used as statements");
    ExprStmt node;
    node.expression = std::make_unique<Expr>(std::move(expr));
    node.line = line;
    if (check(TokenKind::SEMICOLON)) advance();
    return Stmt(std::move(node));
}

Stmt Parser::letStmt() {
    Token let = advance();
    Token name = expect(TokenKind::IDENTIFIER, "variable name after 'let'");
    expect(TokenKind::EQUAL, "'=' after variable name");

    LetStmt node;
    node.name = name;
    node.initializer = std::make_unique<Expr>(expression());
    node.line = let.line;
    return Stmt(std::move(node));
}

// ifStmt → "if" expression "then" block ("elseif" expression "then" block)*
//          ("else" block)? "end"
// Цепочка elseif разбирается списком ветвей; else — ветвь без условия.
// Тела ветвей — blockBody(): всё до else/elseif/end.
Stmt Parser::ifStmt() {
    Token ifToken = advance();
    IfStmt node;
    node.line = ifToken.line;

    IfBranch first;
    first.condition = std::make_unique<Expr>(expression());
    expect(TokenKind::KW_THEN, "'then'");
    first.body = std::make_unique<Block>();
    first.body->statements = blockBody();
    node.branches.push_back(std::move(first));

    while (check(TokenKind::KW_ELSEIF)) {
        advance();
        IfBranch branch;
        branch.condition = std::make_unique<Expr>(expression());
        expect(TokenKind::KW_THEN, "'then'");
        branch.body = std::make_unique<Block>();
        branch.body->statements = blockBody();
        node.branches.push_back(std::move(branch));
    }

    if (check(TokenKind::KW_ELSE)) {
        advance();
        IfBranch branch;  // condition == nullptr → ветвь else
        branch.body = std::make_unique<Block>();
        branch.body->statements = blockBody();
        node.branches.push_back(std::move(branch));
    }

    expect(TokenKind::KW_END, "'end' to close 'if'");
    return Stmt(std::move(node));
}

std::vector<Stmt> Parser::blockBody() {
    std::vector<Stmt> statements;
    while (!check(TokenKind::KW_ELSEIF) && !check(TokenKind::KW_ELSE) &&
           !check(TokenKind::KW_END) && !check(TokenKind::END_OF_FILE)) {
        statements.push_back(statement());
    }
    return statements;
}

// whileStmt → "while" expression "do" block "end"
Stmt Parser::whileStmt() {
    Token whileToken = advance();
    WhileStmt node;
    node.line = whileToken.line;

    node.condition = std::make_unique<Expr>(expression());
    expect(TokenKind::KW_DO, "'do'");

    node.body = std::make_unique<Block>();
    node.body->statements = blockBody();

    expect(TokenKind::KW_END, "'end' to close 'while'");
    return Stmt(std::move(node));
}

// forStmt → "for" IDENT "in" bound ".." bound ("by" expression)? "do" block "end"
// Границы разбираются уровнем additive: арифметика без сравнений и без "..",
// поэтому ".." однозначно служит разделителем диапазона ("0..n-1" — границы
// 0 и n-1, а не конкатенация и не сравнение).
Stmt Parser::forStmt() {
    Token forToken = advance();
    Token variable = expect(TokenKind::IDENTIFIER, "loop variable after 'for'");
    expect(TokenKind::KW_IN, "'in' after loop variable");

    ForStmt node;
    node.line = forToken.line;
    node.variable = variable;
    node.from = std::make_unique<Expr>(additive());
    expect(TokenKind::DOTDOT, "'..' in for loop range");
    node.to = std::make_unique<Expr>(additive());

    if (check(TokenKind::KW_BY)) {
        advance();
        node.step = std::make_unique<Expr>(expression());
    }

    expect(TokenKind::KW_DO, "'do'");

    node.body = std::make_unique<Block>();
    node.body->statements = blockBody();

    expect(TokenKind::KW_END, "'end' to close 'for'");
    return Stmt(std::move(node));
}

// funcDecl → "function" IDENT "(" params? ")" block "end"
// Семантика (дубликаты параметров, return/break-границы) — в Analyzer (Этап 9).
Stmt Parser::functionStmt() {
    Token fnToken = advance();
    Token name = expect(TokenKind::IDENTIFIER, "function name after 'function'");
    expect(TokenKind::LEFT_PAREN, "'(' after function name");

    FunctionStmt node;
    node.line = fnToken.line;
    node.name = name;

    if (!check(TokenKind::RIGHT_PAREN)) {
        while (true) {
            Token param = expect(TokenKind::IDENTIFIER, "parameter name");
            node.params.push_back(param);
            if (!check(TokenKind::COMMA)) break;
            advance();
        }
    }
    expect(TokenKind::RIGHT_PAREN, "')' after parameters");

    node.body = std::make_unique<Block>();
    node.body->statements = blockBody();

    expect(TokenKind::KW_END, "'end' to close function");
    return Stmt(std::move(node));
}

// returnStmt → "return" expression?
// Значение опционально: если следующий токен не может начинать выражение
// (например, 'end' или 'else'), это return без значения.
// Проверка "только внутри функции" — в Analyzer (Этап 9).
Stmt Parser::returnStmt() {
    Token token = advance();
    ReturnStmt node;
    node.line = token.line;
    if (startsExpression())
        node.value = std::make_unique<Expr>(expression());
    return Stmt(std::move(node));
}

bool Parser::startsExpression() const {
    switch (peek().type) {
        case TokenKind::NUMBER:
        case TokenKind::STRING:
        case TokenKind::IDENTIFIER:
        case TokenKind::KW_TRUE:
        case TokenKind::KW_FALSE:
        case TokenKind::KW_NIL:
        case TokenKind::LEFT_PAREN:
        case TokenKind::LEFT_BRACKET:
        case TokenKind::MINUS:
        case TokenKind::KW_NOT:
            return true;
        default:
            return false;
    }
}

// importStmt → "import" STRING
// Путь — только строковый литерал ("import math;" — синтаксическая ошибка).
// Разрешение пути и подстановка модуля — проход resolveImports (imports.h);
// парсер только строит узел. Необязательный ';' пропускается на верхнем уровне.
Stmt Parser::importStmt() {
    Token keyword = advance();
    Token path = expect(TokenKind::STRING,
                         "a string literal file path after 'import'");

    ImportStmt node;
    node.path = path.str;
    node.line = keyword.line;
    return Stmt(std::move(node));
}

Stmt Parser::breakStmt() {
    Token token = advance();
    BreakStmt node;
    node.line = token.line;
    return Stmt(std::move(node));
}

Stmt Parser::continueStmt() {
    Token token = advance();
    ContinueStmt node;
    node.line = token.line;
    return Stmt(std::move(node));
}

// --- Выражения: цепочка приоритетов ---

// orExpr → andExpr ("or" andExpr)*   — самый слабый уровень.
// Цикл, а не рекурсия, даёт левую ассоциативность: накопление в левый операнд.
Expr Parser::orExpr() {
    Expr expr = andExpr();
    while (check(TokenKind::KW_OR)) {
        Token op = advance();
        BinaryExpr node;
        node.op = op;
        node.left = std::make_unique<Expr>(std::move(expr));
        node.right = std::make_unique<Expr>(andExpr());
        node.line = op.line;
        expr = Expr(std::move(node));
    }
    return expr;
}

// andExpr → notExpr ("and" notExpr)*
Expr Parser::andExpr() {
    Expr expr = notExpr();
    while (check(TokenKind::KW_AND)) {
        Token op = advance();
        BinaryExpr node;
        node.op = op;
        node.left = std::make_unique<Expr>(std::move(expr));
        node.right = std::make_unique<Expr>(notExpr());
        node.line = op.line;
        expr = Expr(std::move(node));
    }
    return expr;
}

// notExpr → "not" notExpr | comparison
// "not" стоит НИЖЕ сравнений (как в Python): not a == b == not (a == b).
Expr Parser::notExpr() {
    if (check(TokenKind::KW_NOT)) {
        Token op = advance();
        UnaryExpr node;
        node.op = op;
        node.operand = std::make_unique<Expr>(notExpr());
        node.line = op.line;
        return Expr(std::move(node));
    }
    return comparison();
}

// comparison → concat (("==" | "!=" | "<" | ">" | "<=" | ">=") concat)*
Expr Parser::comparison() {
    Expr expr = concat();
    while (check(TokenKind::EQUAL_EQUAL) || check(TokenKind::BANG_EQUAL) ||
           check(TokenKind::LESS) || check(TokenKind::LESS_EQUAL) ||
           check(TokenKind::GREATER) || check(TokenKind::GREATER_EQUAL)) {
        Token op = advance();
        BinaryExpr node;
        node.op = op;
        node.left = std::make_unique<Expr>(std::move(expr));
        node.right = std::make_unique<Expr>(concat());
        node.line = op.line;
        expr = Expr(std::move(node));
    }
    return expr;
}

// concat → additive (".." concat)?   — правоассоциативен (правая рекурсия).
Expr Parser::concat() {
    Expr expr = additive();
    if (check(TokenKind::DOTDOT)) {
        Token op = advance();
        BinaryExpr node;
        node.op = op;
        node.left = std::make_unique<Expr>(std::move(expr));
        node.right = std::make_unique<Expr>(concat());
        node.line = op.line;
        return Expr(std::move(node));
    }
    return expr;
}

// additive → multiplicative (("+" | "-") multiplicative)*
Expr Parser::additive() {
    Expr expr = multiplicative();
    while (check(TokenKind::PLUS) || check(TokenKind::MINUS)) {
        Token op = advance();
        BinaryExpr node;
        node.op = op;
        node.left = std::make_unique<Expr>(std::move(expr));
        node.right = std::make_unique<Expr>(multiplicative());
        node.line = op.line;
        expr = Expr(std::move(node));
    }
    return expr;
}

// multiplicative → unary (("*" | "/" | "%") unary)*
Expr Parser::multiplicative() {
    Expr expr = unary();
    while (check(TokenKind::STAR) || check(TokenKind::SLASH) ||
           check(TokenKind::PERCENT)) {
        Token op = advance();
        BinaryExpr node;
        node.op = op;
        node.left = std::make_unique<Expr>(std::move(expr));
        node.right = std::make_unique<Expr>(unary());
        node.line = op.line;
        expr = Expr(std::move(node));
    }
    return expr;
}

// unary → "-" unary | postfix
Expr Parser::unary() {
    if (check(TokenKind::MINUS)) {
        Token op = advance();
        UnaryExpr node;
        node.op = op;
        node.operand = std::make_unique<Expr>(unary());
        node.line = op.line;
        return Expr(std::move(node));
    }
    return postfix();
}

// postfix → primary ("(" args? ")" | "[" expression "]")*
// Цикл даёт цепочки: f(1)(2), a[i][j], f(x)[0]. Аргументы и индексы —
// полные выражения.
Expr Parser::postfix() {
    Expr expr = primary();
    while (check(TokenKind::LEFT_PAREN) || check(TokenKind::LEFT_BRACKET)) {
        if (check(TokenKind::LEFT_PAREN)) {
            Token paren = advance();
            CallExpr node;
            node.callee = std::make_unique<Expr>(std::move(expr));
            node.paren = paren;
            node.line = paren.line;

            if (!check(TokenKind::RIGHT_PAREN)) {
                node.args.push_back(expression());
                while (check(TokenKind::COMMA)) {
                    advance();
                    node.args.push_back(expression());
                }
            }
            expect(TokenKind::RIGHT_PAREN, "')'");
            expr = Expr(std::move(node));
        } else {
            Token bracket = advance();
            IndexExpr node;
            node.object = std::make_unique<Expr>(std::move(expr));
            node.index = std::make_unique<Expr>(expression());
            node.line = bracket.line;
            expect(TokenKind::RIGHT_BRACKET, "']'");
            expr = Expr(std::move(node));
        }
    }
    return expr;
}

Expr Parser::primary() {
    const Token& token = peek();
    switch (token.type) {
        case TokenKind::NUMBER:
        case TokenKind::STRING:
        case TokenKind::KW_TRUE:
        case TokenKind::KW_FALSE:
        case TokenKind::KW_NIL: {
            advance();
            LiteralExpr node;
            node.token = token;
            node.line = token.line;
            return Expr(std::move(node));
        }
        case TokenKind::IDENTIFIER: {
            advance();
            IdentifierExpr node;
            node.column = token.column;
            node.name = token.lexeme;
            node.line = token.line;
            return Expr(std::move(node));
        }
        case TokenKind::LEFT_PAREN: {
            advance();
            Expr inner = parseExpression();
            expect(TokenKind::RIGHT_PAREN, "')'");
            GroupingExpr node;
            node.expression = std::make_unique<Expr>(std::move(inner));
            node.line = token.line;
            return Expr(std::move(node));
        }
        case TokenKind::LEFT_BRACKET: {
            // Литерал массива: [a, b, c] или [].
            advance();
            ArrayExpr node;
            node.line = token.line;
            if (!check(TokenKind::RIGHT_BRACKET)) {
                node.elements.push_back(expression());
                while (check(TokenKind::COMMA)) {
                    advance();
                    node.elements.push_back(expression());
                }
            }
            expect(TokenKind::RIGHT_BRACKET, "']'");
            return Expr(std::move(node));
        }
        default:
            throw LumaError(token.line,
                            "expected expression but got " + tokenDesc(token));
    }
}

const Token& Parser::peek(int offset) const {
    size_t index = pos + static_cast<size_t>(offset);
    if (index >= tokens.size()) index = tokens.size() - 1;
    return tokens[index];
}

const Token& Parser::advance() {
    const Token& token = tokens[pos];
    if (token.type != TokenKind::END_OF_FILE) pos++;
    return token;
}

bool Parser::check(TokenKind kind) const {
    return peek().type == kind;
}

const Token& Parser::expect(TokenKind kind, const char* what) {
    if (check(kind)) return advance();
    throw LumaError(peek().line,
                    std::string("expected ") + what + " but got " + tokenDesc(peek()));
}
