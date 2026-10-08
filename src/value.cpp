#include "value.h"

#include "util.h"
#include <unordered_set>
#include "error.h"

std::string valueTypeToString(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "nil";
    if (std::holds_alternative<bool>(value)) return "boolean";
    if (std::holds_alternative<double>(value)) return "number";
    if (std::holds_alternative<std::string>(value)) return "string";
    if (std::holds_alternative<std::shared_ptr<Array>>(value)) return "array";
    return "function";  // NativeFunction | Function
}

namespace {
std::string stringify(const Value& value, std::unordered_set<const Array*>& active, size_t depth) {
    if (depth > 128) throw LumaError(0, "array nesting is too deep to print");
    if (auto* boolean = std::get_if<bool>(&value)) return *boolean ? "true" : "false";
    if (auto* number = std::get_if<double>(&value)) return formatNumber(*number);
    if (auto* string = std::get_if<std::string>(&value)) return *string;
    if (std::holds_alternative<std::monostate>(value)) return "nil";
    if (auto* array = std::get_if<std::shared_ptr<Array>>(&value)) {
        std::string result = "[";
        if (*array) {
            if (!active.insert(array->get()).second) return "[<cycle>]";
            const std::vector<Value>& elements = (*array)->elements;
            for (size_t i = 0; i < elements.size(); ++i) {
                if (i > 0) result += ", ";
                result += stringify(elements[i], active, depth + 1);
            }
            active.erase(array->get());
        }
        return result + "]";
    }
    return "<function>";
}
}

std::string valueToString(const Value& value) {
    std::unordered_set<const Array*> active;
    return stringify(value, active, 0);
}

bool isTruthy(const Value& value) {
    if (auto* boolean = std::get_if<bool>(&value)) return *boolean;
    if (std::holds_alternative<std::monostate>(value)) return false;
    return true;
}
