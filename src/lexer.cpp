#include "lexer.h"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

#include "error.h"

namespace {

const std::unordered_map<std::string, TokenKind>& keywords() {
    static const std::unordered_map<std::string, TokenKind> map = {
        {"and", TokenKind::KW_AND},         {"break", TokenKind::KW_BREAK},
        {"by", TokenKind::KW_BY},           {"continue", TokenKind::KW_CONTINUE},
        {"do", TokenKind::KW_DO},           {"else", TokenKind::KW_ELSE},
        {"elseif", TokenKind::KW_ELSEIF},   {"end", TokenKind::KW_END},
        {"false", TokenKind::KW_FALSE},     {"for", TokenKind::KW_FOR},
        {"function", TokenKind::KW_FUNCTION}, {"if", TokenKind::KW_IF},
        {"import", TokenKind::KW_IMPORT},   {"in", TokenKind::KW_IN},
        {"let", TokenKind::KW_LET},
        {"nil", TokenKind::KW_NIL},         {"not", TokenKind::KW_NOT},
        {"or", TokenKind::KW_OR},           {"return", TokenKind::KW_RETURN},
        {"then", TokenKind::KW_THEN},       {"true", TokenKind::KW_TRUE},
        {"while", TokenKind::KW_WHILE},
    };
    return map;
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

}  // namespace

Lexer::Lexer(std::string source) : source(std::move(source)) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        skipWhitespaceAndComments();
        if (pos >= source.size()) {
            tokens.push_back(makeToken(TokenKind::END_OF_FILE, pos));
            return tokens;
        }
        try {
            tokens.push_back(scanToken());
        } catch (LumaError& error) {
            size_t start = source.rfind('\n', pos == 0 ? 0 : pos - 1);
            error.column = static_cast<int>(pos - (start == std::string::npos ? 0 : start + 1));
            if (error.column < 1) error.column = 1;
            throw;
        }
    }
}

char Lexer::peek(int offset) const {
    size_t index = pos + static_cast<size_t>(offset);
    return index < source.size() ? source[index] : '\0';
}

char Lexer::advance() {
    return source[pos++];
}

bool Lexer::match(char expected) {
    if (peek() == expected) {
        pos++;
        return true;
    }
    return false;
}

void Lexer::skipWhitespaceAndComments() {
    while (true) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            pos++;
        } else if (c == '\n') {
            pos++;
            line++;
        } else if (c == '#') {
            // комментарий до конца строки (перевод строки обработает следующий виток)
            while (peek() != '\n' && peek() != '\0') pos++;
        } else {
            return;
        }
    }
}

Token Lexer::scanToken() {
    size_t start = pos;
    char c = advance();
    switch (c) {
        case '(':  return makeToken(TokenKind::LEFT_PAREN, start);
        case ')':  return makeToken(TokenKind::RIGHT_PAREN, start);
        case '[':  return makeToken(TokenKind::LEFT_BRACKET, start);
        case ']':  return makeToken(TokenKind::RIGHT_BRACKET, start);
        case ',':  return makeToken(TokenKind::COMMA, start);
        case ';':  return makeToken(TokenKind::SEMICOLON, start);
        case '+':  return makeToken(TokenKind::PLUS, start);
        case '-':  return makeToken(TokenKind::MINUS, start);
        case '*':  return makeToken(TokenKind::STAR, start);
        case '/':  return makeToken(TokenKind::SLASH, start);
        case '%':  return makeToken(TokenKind::PERCENT, start);
        case '=':  return makeToken(match('=') ? TokenKind::EQUAL_EQUAL : TokenKind::EQUAL, start);
        case '<':  return makeToken(match('=') ? TokenKind::LESS_EQUAL : TokenKind::LESS, start);
        case '>':  return makeToken(match('=') ? TokenKind::GREATER_EQUAL : TokenKind::GREATER, start);
        case '!':
            if (match('=')) return makeToken(TokenKind::BANG_EQUAL, start);
            throw LumaError(line, "Unexpected character '!'");
        case '.':
            // Одиночной точки в языке нет: либо "..", либо ошибка.
            if (match('.')) return makeToken(TokenKind::DOTDOT, start);
            throw LumaError(line, "Unexpected character '.'");
        case '"':  return string(start);
        default:
            if (isDigit(c)) return number(start);
            if (isIdentStart(c)) return identifier(start);
            throw LumaError(line, std::string("Unexpected character '") + c + "'");
    }
}

Token Lexer::number(size_t start) {
    while (isDigit(peek())) advance();

    // Точка входит в число ТОЛЬКО если за ней цифра. Поэтому "0..10" — это
    // NUMBER(0) DOTDOT NUMBER(10), а не сломанное число "0." (DESIGN.md 2.2).
    if (peek() == '.' && isDigit(peek(1))) {
        advance();
        while (isDigit(peek())) advance();
    }

    Token token = makeToken(TokenKind::NUMBER, start);
    token.number = std::strtod(token.lexeme.c_str(), nullptr);
    return token;
}

Token Lexer::identifier(size_t start) {
    while (isIdentChar(peek())) advance();

    Token token = makeToken(TokenKind::IDENTIFIER, start);
    auto it = keywords().find(token.lexeme);
    if (it != keywords().end()) token.type = it->second;
    return token;
}

Token Lexer::string(size_t start) {
    std::string value;
    while (true) {
        char c = peek();
        if (c == '\0' || c == '\n')
            throw LumaError(line, "Unterminated string");
        if (c == '"') {
            advance();
            break;
        }
        if (c == '\\') {
            advance();  // съели '\'
            if (pos >= source.size())
                throw LumaError(line, "Unterminated string");
            char escape = advance();
            switch (escape) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case 'r':  value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '\0': throw LumaError(line, "Unterminated string");
                default:
                    throw LumaError(line, std::string("Unknown escape '\\") + escape + "'");
            }
        } else {
            value += advance();
        }
    }

    Token token = makeToken(TokenKind::STRING, start);
    token.str = value;
    return token;
}

// Лексема берётся срезом исходника от start до текущей позиции — поэтому
// её текст есть у КАЖДОГО токена (парсер использует его в сообщениях).
Token Lexer::makeToken(TokenKind type, size_t start) const {
    Token token;
    token.type = type;
    token.line = line;
    size_t newline = start == 0 ? std::string::npos : source.rfind('\n', start - 1);
    token.column = static_cast<int>(start - (newline == std::string::npos ? 0 : newline + 1) + 1);
    token.lexeme = source.substr(start, pos - start);
    return token;
}
