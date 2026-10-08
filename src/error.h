#pragma once
#include <stdexcept>
#include <string>

// Единая ошибка для всех фаз Luma (лексер, парсер, анализ, рантайм).
// Бросается с номером строки; main ловит и печатает:
//   Error at line 5: Unknown variable 'score'
struct LumaError : std::runtime_error {
    int line;
    int column = 0;
    std::string file;

    LumaError(int line, const std::string& message)
        : std::runtime_error(message), line(line) {}
};
