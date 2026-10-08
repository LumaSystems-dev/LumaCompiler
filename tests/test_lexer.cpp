#include <string>
#include <utility>
#include <vector>

#include "../src/error.h"
#include "../src/lexer.h"
#include "test.h"

TEST(lexer_trailing_escape_is_safe) {
    Lexer lexer(std::string("\"unfinished\\"));
    CHECK_THROWS(lexer.tokenize());
}

TEST(lexer_tracks_byte_columns) {
    Lexer lexer("let x = 1\n  print(x)");
    auto tokens = lexer.tokenize();
    CHECK_EQ(tokens[4].line, 2);
    CHECK_EQ(tokens[4].column, 3);
}

namespace {

std::vector<Token> lex(const std::string& source) {
    return Lexer(source).tokenize();
}

// Короткое имя для проверки типа i-го токена строкой ("LET", "NUMBER", ...).
std::string typeOf(const std::vector<Token>& tokens, size_t i) {
    return tokenTypeToString(tokens[i].type);
}

}  // namespace

TEST(empty_source_produces_only_eof) {
    auto tokens = lex("");
    CHECK_EQ(tokens.size(), 1u);
    CHECK_EQ(typeOf(tokens, 0), "END_OF_FILE");
    CHECK_EQ(tokens[0].line, 1);
}

TEST(whitespace_and_comments_only_produces_only_eof) {
    auto tokens = lex("  \t\r\n# комментарий\n\n###\n");
    CHECK_EQ(tokens.size(), 1u);
    CHECK_EQ(typeOf(tokens, 0), "END_OF_FILE");
}

TEST(let_statement_sequence) {
    auto tokens = lex("let x = 10 + 20");
    CHECK_EQ(tokens.size(), 7u);
    CHECK_EQ(typeOf(tokens, 0), "LET");
    CHECK_EQ(typeOf(tokens, 1), "IDENTIFIER");
    CHECK_EQ(tokens[1].lexeme, "x");
    CHECK_EQ(typeOf(tokens, 2), "EQUAL");
    CHECK_EQ(typeOf(tokens, 3), "NUMBER");
    CHECK_EQ(tokens[3].number, 10.0);
    CHECK_EQ(typeOf(tokens, 4), "PLUS");
    CHECK_EQ(typeOf(tokens, 5), "NUMBER");
    CHECK_EQ(tokens[5].number, 20.0);
    CHECK_EQ(typeOf(tokens, 6), "END_OF_FILE");
}

TEST(numbers_integer_and_fractional) {
    auto tokens = lex("0 42 3.14 0.25 100.5");
    CHECK_EQ(tokens.size(), 6u);
    CHECK_EQ(tokens[0].number, 0.0);
    CHECK_EQ(tokens[1].number, 42.0);
    CHECK_EQ(tokens[2].number, 3.14);
    CHECK_EQ(tokens[3].number, 0.25);
    CHECK_EQ(tokens[4].number, 100.5);
}

TEST(range_dotdot_does_not_break_numbers) {
    // Ключевой случай из DESIGN.md 2.2: "0..10" не должно читаться как "0." + ".10"
    auto tokens = lex("0..10");
    CHECK_EQ(tokens.size(), 4u);  // NUMBER DOTDOT NUMBER EOF
    CHECK_EQ(typeOf(tokens, 0), "NUMBER");
    CHECK_EQ(tokens[0].number, 0.0);
    CHECK_EQ(typeOf(tokens, 1), "DOTDOT");
    CHECK_EQ(typeOf(tokens, 2), "NUMBER");
    CHECK_EQ(tokens[2].number, 10.0);
}

TEST(range_with_spaces_and_fractional_bound) {
    auto tokens = lex("1 .. 2.5");
    CHECK_EQ(tokens.size(), 4u);  // NUMBER DOTDOT NUMBER EOF
    CHECK_EQ(typeOf(tokens, 0), "NUMBER");
    CHECK_EQ(typeOf(tokens, 1), "DOTDOT");
    CHECK_EQ(typeOf(tokens, 2), "NUMBER");
    CHECK_EQ(tokens[2].number, 2.5);
}

TEST(number_glued_to_identifier_is_two_tokens) {
    auto tokens = lex("10x");
    CHECK_EQ(tokens.size(), 3u);
    CHECK_EQ(typeOf(tokens, 0), "NUMBER");
    CHECK_EQ(tokens[0].number, 10.0);
    CHECK_EQ(typeOf(tokens, 1), "IDENTIFIER");
    CHECK_EQ(tokens[1].lexeme, "x");
    CHECK_EQ(typeOf(tokens, 2), "END_OF_FILE");
}

TEST(all_keywords_recognized) {
    const std::vector<std::pair<std::string, std::string>> keywords = {
        {"and", "AND"}, {"break", "BREAK"}, {"by", "BY"}, {"continue", "CONTINUE"},
        {"do", "DO"}, {"else", "ELSE"}, {"elseif", "ELSEIF"}, {"end", "END"},
        {"false", "FALSE"}, {"for", "FOR"}, {"function", "FUNCTION"}, {"if", "IF"},
        {"import", "IMPORT"},
        {"in", "IN"}, {"let", "LET"}, {"nil", "NIL"}, {"not", "NOT"},
        {"or", "OR"}, {"return", "RETURN"}, {"then", "THEN"}, {"true", "TRUE"},
        {"while", "WHILE"},
    };
    std::string source;
    for (const auto& keyword : keywords) source += keyword.first + " ";

    auto tokens = lex(source);
    CHECK_EQ(tokens.size(), keywords.size() + 1);
    for (size_t i = 0; i < keywords.size(); ++i) {
        CHECK_EQ(typeOf(tokens, i), keywords[i].second);
    }
}

TEST(identifiers_are_case_sensitive) {
    // Ключевые слова только строчные: "Let" и "LET" — обычные идентификаторы.
    auto tokens = lex("Let LET let _x x1 _1");
    CHECK_EQ(tokens.size(), 7u);
    CHECK_EQ(typeOf(tokens, 0), "IDENTIFIER");
    CHECK_EQ(typeOf(tokens, 1), "IDENTIFIER");
    CHECK_EQ(typeOf(tokens, 2), "LET");
    CHECK_EQ(typeOf(tokens, 3), "IDENTIFIER");
    CHECK_EQ(tokens[3].lexeme, "_x");
    CHECK_EQ(typeOf(tokens, 4), "IDENTIFIER");
    CHECK_EQ(tokens[4].lexeme, "x1");
    CHECK_EQ(typeOf(tokens, 5), "IDENTIFIER");
    CHECK_EQ(tokens[5].lexeme, "_1");
}

TEST(all_operators_with_spaces) {
    auto tokens = lex("( ) [ ] , ; + - * / % == != <= >= .. = < >");
    const std::vector<std::string> expected = {
        "LEFT_PAREN", "RIGHT_PAREN", "LEFT_BRACKET", "RIGHT_BRACKET",
        "COMMA", "SEMICOLON", "PLUS", "MINUS", "STAR", "SLASH", "PERCENT",
        "EQUAL_EQUAL", "BANG_EQUAL", "LESS_EQUAL", "GREATER_EQUAL", "DOTDOT",
        "EQUAL", "LESS", "GREATER", "END_OF_FILE",
    };
    CHECK_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK_EQ(typeOf(tokens, i), expected[i]);
}

TEST(glued_operators) {
    auto tokens = lex("a==b x<=y 1..2 i!=j k>=m n<p>q");
    const std::vector<std::string> expected = {
        "IDENTIFIER", "EQUAL_EQUAL", "IDENTIFIER",
        "IDENTIFIER", "LESS_EQUAL", "IDENTIFIER",
        "NUMBER", "DOTDOT", "NUMBER",
        "IDENTIFIER", "BANG_EQUAL", "IDENTIFIER",
        "IDENTIFIER", "GREATER_EQUAL", "IDENTIFIER",
        "IDENTIFIER", "LESS", "IDENTIFIER", "GREATER", "IDENTIFIER",
        "END_OF_FILE",
    };
    CHECK_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK_EQ(typeOf(tokens, i), expected[i]);
}

TEST(dotdot_between_identifiers) {
    auto tokens = lex("a..b");
    CHECK_EQ(tokens.size(), 4u);  // IDENTIFIER DOTDOT IDENTIFIER EOF
    CHECK_EQ(typeOf(tokens, 0), "IDENTIFIER");
    CHECK_EQ(typeOf(tokens, 1), "DOTDOT");
    CHECK_EQ(typeOf(tokens, 2), "IDENTIFIER");
}

TEST(strings_and_escapes) {
    auto tokens = lex("\"привет\" \"a\\nb\" \"\\t\" \"\\\"q\\\"\" \"\\\\\" \"crlf:\\r\"");
    CHECK_EQ(tokens.size(), 7u);
    CHECK_EQ(tokens[0].str, "привет");
    CHECK_EQ(tokens[1].str, std::string("a\nb"));
    CHECK_EQ(tokens[2].str, std::string("\t"));
    CHECK_EQ(tokens[3].str, std::string("\"q\""));
    CHECK_EQ(tokens[4].str, std::string("\\"));
    CHECK_EQ(tokens[5].str, std::string("crlf:\r"));
}

TEST(string_errors) {
    CHECK_THROWS(lex("\"unterminated"));
    CHECK_THROWS(lex("\"line\nbreak\""));  // перенос строки внутри строки
    CHECK_THROWS(lex("\"bad \\q escape\""));
    CHECK_THROWS(lex("\"ends with backslash \\"));
}

TEST(unexpected_characters) {
    CHECK_THROWS(lex("let x = !y"));   // одиночный '!'
    CHECK_THROWS(lex("a . b"));        // одиночная '.'
    CHECK_THROWS(lex("let email = a@b"));
    CHECK_THROWS(lex("1."));           // точка не входит в число без цифры после неё
    CHECK_THROWS(lex("€"));
}

TEST(line_tracking_across_statements) {
    auto tokens = lex("let x = 1\n# комментарий\n\nprint(\"hi\")\n");
    // LET IDENT EQUAL NUMBER | IDENT(print) LEFT_PAREN STRING RIGHT_PAREN | EOF
    CHECK_EQ(tokens.size(), 9u);
    CHECK_EQ(tokens[0].line, 1);
    CHECK_EQ(tokens[3].line, 1);
    CHECK_EQ(tokens[4].line, 4);
    CHECK_EQ(tokens[6].line, 4);
    CHECK_EQ(tokens[8].line, 5);
}

TEST(comment_at_eof_without_newline) {
    auto tokens = lex("let x = 1 # комментарий до конца файла");
    CHECK_EQ(tokens.size(), 5u);
    CHECK_EQ(typeOf(tokens, 4), "END_OF_FILE");
}
