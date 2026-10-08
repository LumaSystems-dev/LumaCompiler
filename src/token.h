#pragma once
#include <string>

// Типы токенов. Каждое ключевое слово — отдельный тип: парсеру потом
// достаточно сравнить тип, не сверяя текст.
//
// Перечислители ключевых слов несут префикс KW_: голые TRUE/FALSE конфликтуют
// с макросами <windows.h> на Windows (как EOF из <cstdio>).
enum class TokenKind {
    // Односимвольные разделители и операторы
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACKET, RIGHT_BRACKET,
    COMMA, SEMICOLON,
    PLUS, MINUS, STAR, SLASH, PERCENT,

    // Операторы сравнения и присваивания
    EQUAL, EQUAL_EQUAL, BANG_EQUAL,
    LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,

    // Диапазон в for
    DOTDOT,

    // Литералы
    NUMBER, STRING, IDENTIFIER,

    // Ключевые слова (порядок алфавитный, как в DESIGN.md 2.1)
    KW_AND, KW_BREAK, KW_BY, KW_CONTINUE, KW_DO, KW_ELSE, KW_ELSEIF, KW_END,
    KW_FALSE, KW_FOR, KW_FUNCTION, KW_IF, KW_IMPORT, KW_IN, KW_LET, KW_NIL,
    KW_NOT, KW_OR, KW_RETURN, KW_THEN, KW_TRUE, KW_WHILE,

    // Конец файла (EOF нельзя — это макрос из <cstdio>)
    END_OF_FILE,
};

// Отображаемое имя без KW_: дамп токенов читается как "LET", "TRUE", ...
inline const char* tokenTypeToString(TokenKind type) {
    switch (type) {
        case TokenKind::LEFT_PAREN:     return "LEFT_PAREN";
        case TokenKind::RIGHT_PAREN:    return "RIGHT_PAREN";
        case TokenKind::LEFT_BRACKET:   return "LEFT_BRACKET";
        case TokenKind::RIGHT_BRACKET:  return "RIGHT_BRACKET";
        case TokenKind::COMMA:          return "COMMA";
        case TokenKind::SEMICOLON:      return "SEMICOLON";
        case TokenKind::PLUS:           return "PLUS";
        case TokenKind::MINUS:          return "MINUS";
        case TokenKind::STAR:           return "STAR";
        case TokenKind::SLASH:          return "SLASH";
        case TokenKind::PERCENT:        return "PERCENT";
        case TokenKind::EQUAL:          return "EQUAL";
        case TokenKind::EQUAL_EQUAL:    return "EQUAL_EQUAL";
        case TokenKind::BANG_EQUAL:     return "BANG_EQUAL";
        case TokenKind::LESS:           return "LESS";
        case TokenKind::LESS_EQUAL:     return "LESS_EQUAL";
        case TokenKind::GREATER:        return "GREATER";
        case TokenKind::GREATER_EQUAL:  return "GREATER_EQUAL";
        case TokenKind::DOTDOT:         return "DOTDOT";
        case TokenKind::NUMBER:         return "NUMBER";
        case TokenKind::STRING:         return "STRING";
        case TokenKind::IDENTIFIER:     return "IDENTIFIER";
        case TokenKind::KW_AND:         return "AND";
        case TokenKind::KW_BREAK:       return "BREAK";
        case TokenKind::KW_BY:          return "BY";
        case TokenKind::KW_CONTINUE:    return "CONTINUE";
        case TokenKind::KW_DO:          return "DO";
        case TokenKind::KW_ELSE:        return "ELSE";
        case TokenKind::KW_ELSEIF:      return "ELSEIF";
        case TokenKind::KW_END:         return "END";
        case TokenKind::KW_FALSE:       return "FALSE";
        case TokenKind::KW_FOR:         return "FOR";
        case TokenKind::KW_FUNCTION:    return "FUNCTION";
        case TokenKind::KW_IF:          return "IF";
        case TokenKind::KW_IMPORT:      return "IMPORT";
        case TokenKind::KW_IN:          return "IN";
        case TokenKind::KW_LET:         return "LET";
        case TokenKind::KW_NIL:         return "NIL";
        case TokenKind::KW_NOT:         return "NOT";
        case TokenKind::KW_OR:          return "OR";
        case TokenKind::KW_RETURN:      return "RETURN";
        case TokenKind::KW_THEN:        return "THEN";
        case TokenKind::KW_TRUE:        return "TRUE";
        case TokenKind::KW_WHILE:       return "WHILE";
        case TokenKind::END_OF_FILE:    return "END_OF_FILE";
    }
    return "?";  // недостижимо, но глушит предупреждение компилятора
}

// Один токен. Поля-значения заполнены только для соответствующих типов:
//   IDENTIFIER -> lexeme (имя), NUMBER -> lexeme + number, STRING -> str.
struct Token {
    TokenKind type;
    int line = 0;
    int column = 0; // UTF-8 byte column, one-based
    std::string lexeme;   // исходный текст лексемы
    double number = 0.0;  // значение NUMBER
    std::string str;      // декодированное содержимое STRING
};
