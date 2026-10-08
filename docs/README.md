# Документация Luma 0.2.0

Начните с [главного README](../README.md): требования сборки и Quick Start.
Документация на русском, имена API и сообщения компилятора — на английском.

| Документ | Для чего читать |
| --- | --- |
| [Language Guide](LANGUAGE.md) | Синтаксис, значения, области, функции, массивы, строки, import, ошибки |
| [Standard Library](STDLIB.md) | Все 19 public-функций math/string/random/io, параметры и примеры |
| [Builtins](BUILTINS.md) | Все 11 встроенных функций, типы, результаты и ошибки |
| [CLI](CLI.md) | Все команды, аргументы, stdout/stderr и коды выхода |
| [Bytecode](BYTECODE.md) | Создание, запуск, диагностика и ограничения .lbc |
| [Architecture](../DESIGN.md) | Реализация frontend, resolver, runtime, Compiler и VM |
| [Contributing](../CONTRIBUTING.md) | Сборка, тесты, правила изменений и проверки перед PR |
| [Examples](../examples/README.md) | Назначение каждого примера и требуемый ввод |
| [Changelog](../CHANGELOG.md) | История версий, включая совместимость 0.2.0 |
| [License](../LICENSE) | MIT |

## Порядок знакомства

README → LANGUAGE → BUILTINS/STDLIB → CLI/BYTECODE.
Для изменения компилятора: DESIGN → CONTRIBUTING → tests.

Каждый блок `luma` — отдельный исполняемый пример, если не обозначена
ожидаемая ошибка. Пути import зависят от расположения файла:
руководства обычно предполагают программу в `examples/`; README явно
показывает программу в корне. Markdown не передаётся компилятору:
копируйте в .luma только содержимое блока, без ограждений.
