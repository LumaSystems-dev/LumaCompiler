#include <string>

#include "../src/ast.h"
#include "../src/error.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "test.h"

// ВАЖНО: узлы AST указывают внутрь владеющего их Expr.
// Результат parse(...) всегда привязываем к именованной переменной — указатель
// на узел внутри временного объекта прожил бы только до конца строки (UB).
namespace {

Expr parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    return parser.parseSingleExpression();
}

const LiteralExpr* asLiteral(const Expr& expr) {
    return std::get_if<LiteralExpr>(&expr.value);
}

const IdentifierExpr* asIdentifier(const Expr& expr) {
    return std::get_if<IdentifierExpr>(&expr.value);
}

const GroupingExpr* asGrouping(const Expr& expr) {
    return std::get_if<GroupingExpr>(&expr.value);
}

const UnaryExpr* asUnary(const Expr& expr) {
    return std::get_if<UnaryExpr>(&expr.value);
}

const BinaryExpr* asBinary(const Expr& expr) {
    return std::get_if<BinaryExpr>(&expr.value);
}

}  // namespace

TEST(number_literal) {
    Expr expr = parse("42");
    const LiteralExpr* node = asLiteral(expr);
    CHECK(node != nullptr);
    CHECK_EQ(node->token.number, 42.0);
    CHECK_EQ(node->line, 1);
}

TEST(string_literal) {
    Expr expr = parse("\"hi\"");
    const LiteralExpr* node = asLiteral(expr);
    CHECK(node != nullptr);
    CHECK_EQ(node->token.str, "hi");
}

TEST(bool_and_nil_literals) {
    Expr tExpr = parse("true");
    const LiteralExpr* t = asLiteral(tExpr);
    CHECK(t != nullptr);
    CHECK(t->token.type == TokenKind::KW_TRUE);

    Expr fExpr = parse("false");
    const LiteralExpr* f = asLiteral(fExpr);
    CHECK(f != nullptr);
    CHECK(f->token.type == TokenKind::KW_FALSE);

    Expr nExpr = parse("nil");
    const LiteralExpr* n = asLiteral(nExpr);
    CHECK(n != nullptr);
    CHECK(n->token.type == TokenKind::KW_NIL);
}

TEST(identifier) {
    Expr expr = parse("x");
    const IdentifierExpr* node = asIdentifier(expr);
    CHECK(node != nullptr);
    CHECK_EQ(node->name, "x");
    CHECK_EQ(node->line, 1);
}

TEST(grouping) {
    Expr expr = parse("(x)");
    const GroupingExpr* node = asGrouping(expr);
    CHECK(node != nullptr);
    CHECK(asIdentifier(*node->expression) != nullptr);
}

TEST(unary_minus_on_number) {
    Expr expr = parse("-5");
    const UnaryExpr* node = asUnary(expr);
    CHECK(node != nullptr);
    CHECK_EQ(node->op.lexeme, "-");
    CHECK(node->operand != nullptr);
    const LiteralExpr* operand = asLiteral(*node->operand);
    CHECK(operand != nullptr);
    CHECK_EQ(operand->token.number, 5.0);
}

TEST(unary_minus_on_identifier_and_nested) {
    // -x
    Expr identExpr = parse("-x");
    const UnaryExpr* onIdent = asUnary(identExpr);
    CHECK(onIdent != nullptr);
    CHECK(onIdent->operand != nullptr);
    CHECK(asIdentifier(*onIdent->operand) != nullptr);

    // --5: унарный минус правоассоциативен через рекурсию
    Expr nestedExpr = parse("--5");
    const UnaryExpr* outer = asUnary(nestedExpr);
    CHECK(outer != nullptr);
    CHECK(outer->operand != nullptr);
    const UnaryExpr* inner = asUnary(*outer->operand);
    CHECK(inner != nullptr);
    CHECK(inner->operand != nullptr);
    CHECK(asLiteral(*inner->operand) != nullptr);
}

TEST(simple_binary) {
    Expr expr = parse("10 + 20");
    const BinaryExpr* node = asBinary(expr);
    CHECK(node != nullptr);
    CHECK_EQ(node->op.lexeme, "+");

    CHECK(node->left != nullptr);
    const LiteralExpr* left = asLiteral(*node->left);
    CHECK(left != nullptr);
    CHECK_EQ(left->token.number, 10.0);

    CHECK(node->right != nullptr);
    const LiteralExpr* right = asLiteral(*node->right);
    CHECK(right != nullptr);
    CHECK_EQ(right->token.number, 20.0);
}

// КЛЮЧЕВОЙ тест приоритета: 2 + 3 * 4 == 2 + (3 * 4)
TEST(precedence_mul_binds_tighter_than_add) {
    Expr expr = parse("2 + 3 * 4");
    const BinaryExpr* plus = asBinary(expr);
    CHECK(plus != nullptr);
    CHECK_EQ(plus->op.lexeme, "+");

    CHECK(plus->left != nullptr);
    CHECK(asLiteral(*plus->left) != nullptr);  // слева — вся "2"

    CHECK(plus->right != nullptr);
    const BinaryExpr* times = asBinary(*plus->right);
    CHECK(times != nullptr);
    CHECK_EQ(times->op.lexeme, "*");
    CHECK(times->left != nullptr && times->right != nullptr);
    CHECK(asLiteral(*times->left) != nullptr);
    CHECK(asLiteral(*times->right) != nullptr);
}

TEST(precedence_div_binds_tighter_than_sub) {
    Expr expr = parse("10 - 4 / 2");
    const BinaryExpr* minus = asBinary(expr);
    CHECK(minus != nullptr);
    CHECK_EQ(minus->op.lexeme, "-");
    CHECK(asLiteral(*minus->left) != nullptr);

    const BinaryExpr* slash = asBinary(*minus->right);
    CHECK(slash != nullptr);
    CHECK_EQ(slash->op.lexeme, "/");
}

// 1 - 2 - 3 == (1 - 2) - 3, а не 1 - (2 - 3)
TEST(left_associativity_of_subtraction) {
    Expr expr = parse("1 - 2 - 3");
    const BinaryExpr* outer = asBinary(expr);
    CHECK(outer != nullptr);
    CHECK_EQ(outer->op.lexeme, "-");

    CHECK(outer->left != nullptr);
    const BinaryExpr* inner = asBinary(*outer->left);
    CHECK(inner != nullptr);
    CHECK_EQ(inner->op.lexeme, "-");
    CHECK(asLiteral(*outer->right) != nullptr);
}

// + и - на одном уровне, тоже слева направо: 1 + 2 - 3 == (1 + 2) - 3
TEST(same_level_operators_are_left_associative) {
    Expr expr = parse("1 + 2 - 3");
    const BinaryExpr* outer = asBinary(expr);
    CHECK(outer != nullptr);
    CHECK_EQ(outer->op.lexeme, "-");

    const BinaryExpr* inner = asBinary(*outer->left);
    CHECK(inner != nullptr);
    CHECK_EQ(inner->op.lexeme, "+");
}

TEST(parens_override_precedence) {
    Expr expr = parse("(2 + 3) * 4");
    const BinaryExpr* times = asBinary(expr);
    CHECK(times != nullptr);
    CHECK_EQ(times->op.lexeme, "*");

    CHECK(times->left != nullptr);
    const GroupingExpr* group = asGrouping(*times->left);
    CHECK(group != nullptr);
    const BinaryExpr* plus = asBinary(*group->expression);
    CHECK(plus != nullptr);
    CHECK_EQ(plus->op.lexeme, "+");

    CHECK(asLiteral(*times->right) != nullptr);
}

TEST(showcase_expression_from_examples) {
    // (x + 5) * 3 — пример из постановки этапа
    Expr expr = parse("(x + 5) * 3");
    const BinaryExpr* times = asBinary(expr);
    CHECK(times != nullptr);
    CHECK_EQ(times->op.lexeme, "*");

    const GroupingExpr* group = asGrouping(*times->left);
    CHECK(group != nullptr);
    const BinaryExpr* plus = asBinary(*group->expression);
    CHECK(plus != nullptr);
    CHECK_EQ(plus->op.lexeme, "+");
    CHECK(asIdentifier(*plus->left) != nullptr);
    CHECK(asLiteral(*plus->right) != nullptr);

    CHECK(asLiteral(*times->right) != nullptr);
}

TEST(identifiers_with_numbers) {
    // x + 5
    Expr plusExpr = parse("x + 5");
    const BinaryExpr* plus = asBinary(plusExpr);
    CHECK(plus != nullptr);
    const IdentifierExpr* x = asIdentifier(*plus->left);
    CHECK(x != nullptr);
    CHECK_EQ(x->name, "x");

    // x * 2
    Expr timesExpr = parse("x * 2");
    const BinaryExpr* times = asBinary(timesExpr);
    CHECK(times != nullptr);
    CHECK_EQ(times->op.lexeme, "*");
    CHECK(asIdentifier(*times->left) != nullptr);
}

TEST(unary_binds_tighter_than_multiplication) {
    // -2 * 3 == (-2) * 3
    Expr aExpr = parse("-2 * 3");
    const BinaryExpr* a = asBinary(aExpr);
    CHECK(a != nullptr);
    CHECK_EQ(a->op.lexeme, "*");
    CHECK(asUnary(*a->left) != nullptr);
    CHECK(asLiteral(*a->right) != nullptr);

    // 2 * -3 == 2 * (-3)
    Expr bExpr = parse("2 * -3");
    const BinaryExpr* b = asBinary(bExpr);
    CHECK(b != nullptr);
    CHECK_EQ(b->op.lexeme, "*");
    CHECK(asLiteral(*b->left) != nullptr);
    CHECK(asUnary(*b->right) != nullptr);
}

TEST(expression_spanning_lines) {
    Expr expr = parse("1 +\n2");
    const BinaryExpr* node = asBinary(expr);
    CHECK(node != nullptr);
    CHECK_EQ(node->line, 1);  // строка оператора

    const LiteralExpr* right = asLiteral(*node->right);
    CHECK(right != nullptr);
    CHECK_EQ(right->line, 2);  // строка операнда
}

TEST(modulo_level) {
    Expr expr = parse("7 % 3");
    const BinaryExpr* node = asBinary(expr);
    CHECK(node != nullptr);
    CHECK_EQ(node->op.lexeme, "%");
}

TEST(concat_is_right_associative) {
    Expr expr = parse("\"a\" .. \"b\" .. \"c\"");
    const BinaryExpr* outer = asBinary(expr);
    CHECK(outer != nullptr);
    CHECK_EQ(outer->op.lexeme, "..");
    CHECK(asLiteral(*outer->left) != nullptr);  // левый — вся "a"

    CHECK(outer->right != nullptr);
    const BinaryExpr* inner = asBinary(*outer->right);
    CHECK(inner != nullptr);
    CHECK_EQ(inner->op.lexeme, "..");
}

TEST(not_binds_looser_than_comparison) {
    // not 1 == 2 == not (1 == 2), а не (not 1) == 2
    Expr expr = parse("not 1 == 2");
    const UnaryExpr* notNode = asUnary(expr);
    CHECK(notNode != nullptr);
    CHECK_EQ(notNode->op.lexeme, "not");

    CHECK(notNode->operand != nullptr);
    const BinaryExpr* eq = asBinary(*notNode->operand);
    CHECK(eq != nullptr);
    CHECK_EQ(eq->op.lexeme, "==");
}

TEST(or_is_weaker_than_and) {
    // 1 or 2 and 3 == 1 or (2 and 3)
    Expr expr = parse("1 or 2 and 3");
    const BinaryExpr* orNode = asBinary(expr);
    CHECK(orNode != nullptr);
    CHECK_EQ(orNode->op.lexeme, "or");
    CHECK(asLiteral(*orNode->left) != nullptr);

    CHECK(orNode->right != nullptr);
    const BinaryExpr* andNode = asBinary(*orNode->right);
    CHECK(andNode != nullptr);
    CHECK_EQ(andNode->op.lexeme, "and");
}

TEST(comparisons_share_one_level) {
    Expr expr = parse("1 < 2 == true");
    const BinaryExpr* eq = asBinary(expr);
    CHECK(eq != nullptr);
    CHECK_EQ(eq->op.lexeme, "==");  // верхний узел — ==, значит (1 < 2) == true

    CHECK(eq->left != nullptr);
    const BinaryExpr* less = asBinary(*eq->left);
    CHECK(less != nullptr);
    CHECK_EQ(less->op.lexeme, "<");
}

TEST(parser_error_messages) {
    CHECK_THROWS(parse("1 +"));      // expected expression but got end of file
    CHECK_THROWS(parse("(1 + 2"));   // expected ')' but got end of file
    CHECK_THROWS(parse(""));         // пустой файл — не выражение
    CHECK_THROWS(parse(")"));
    CHECK_THROWS(parse("*3"));
    CHECK_THROWS(parse("1 2"));      // лишний токен после выражения
    CHECK_THROWS(parse("1 2 3"));
    CHECK_THROWS(parse("1;"));       // ';' пока не выражение
    CHECK_THROWS(parse("not"));      // not без операнда
    CHECK_THROWS(parse("1 %"));      // % без правого операнда
    CHECK_THROWS(parse("1 <"));      // сравнение без правого операнда
}

TEST(error_message_format_and_line) {
    bool threw = false;
    try {
        (void)parse("(1 + 2");
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(e.line, 1);
        CHECK_EQ(std::string(e.what()), std::string("expected ')' but got end of file"));
    }
    CHECK(threw);

    threw = false;
    try {
        (void)parse("1 +\n(2\n");  // EOF на строке 3
    } catch (const LumaError& e) {
        threw = true;
        CHECK_EQ(e.line, 3);
    }
    CHECK(threw);
}
