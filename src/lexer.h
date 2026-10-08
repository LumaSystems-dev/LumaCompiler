#pragma once
#include <string>
#include <vector>

#include "token.h"

// Лексический анализатор: исходный текст -> вектор токенов, завершённый END_OF_FILE.
// Ошибки (неизвестный символ, незакрытая строка, неизвестный escape)
// сообщаются исключением LumaError с номером строки.
class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> tokenize();

private:
    // Символ на offset позиций вперёд (0 — текущий); за концом — '\0'.
    char peek(int offset = 0) const;
    char advance();
    bool match(char expected);

    void skipWhitespaceAndComments();
    Token scanToken();
    Token number(size_t start);    // первая цифра уже прочитана
    Token identifier(size_t start); // первый символ уже прочитан
    Token string(size_t start);    // открывающая кавычка уже прочитана

    // Лексема заполняется срезом исходника [start, pos) — у любого токена.
    Token makeToken(TokenKind type, size_t start) const;

    std::string source;
    size_t pos = 0;
    int line = 1;
};
