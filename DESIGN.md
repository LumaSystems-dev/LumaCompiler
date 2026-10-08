# Архитектура Luma 0.2.0

Технический документ для разработчиков компилятора. Синтаксис и пользовательский
API описаны в [Language Guide](docs/LANGUAGE.md), [Builtins](docs/BUILTINS.md)
и [Stdlib](docs/STDLIB.md). Здесь зафиксирована реализация, не roadmap.

## 1. Конвейер и точки входа

```text
Source → Lexer → tokens → Parser → AST → ImportResolver → expanded AST → Analyzer
                                                                       │
                             ┌─────────────────────────────────────────┤
                             ↓                                         ↓
                        Interpreter                               Compiler
                           (run)                                       ↓
                                                                    Chunk
                                                               ┌───────┴───────┐
                                                               ↓               ↓
                                                       validate → VM       writeChunk
                                                           (vm)               ↓
                                                                             .lbc
                                                                               ↓
                                                                  readChunk → validate
                                                                               ↓
                                                                            VM
                                                                         (execute)
```

`main.cpp` управляет порядком фаз, читает файлы, ловит LumaError/std::exception.
Compiler сам не выполняет resolveImports или Analyzer: C++ вызывающая сторона
обязана подготовить AST. Interpreter также не разворачивает import самостоятельно.
Bytecode для source проходит frontend, но не запускает код.
Tokens и ast только лексируют/парсят свой файл; eval парсит единственное
выражение и вычисляет его Interpreter без Analyzer и resolver.

## 2. Исходники, токены и AST

`lexer.cpp`: побайтовый сканер, # comments, десятичные литералы,
строки с пятью escapes, keywords и операторы.
Token хранит вид, lexeme, number/string, line и байтовую column.
Unicode содержимое строк допускается; Unicode-идентификаторы не реализованы.
BOM не пропускается.

`parser.cpp`: рекурсивный спуск, отдельные уровни приоритета.
Не precedence-climbing. AST в `ast.h` использует std::variant и unique_ptr
для дочерних выражений/блоков. Стейтменты имеют данные происхождения для Analyzer.
Parser строит ImportStmt, но не открывает импортируемый файл.
Переносы строк не являются разделителями инструкций.
Не поддержаны function expressions, maps и самостоятельные range-значения.

## 3. ImportResolver

`imports.cpp` разворачивает верхнеуровневые import до Analyzer.
Пути относительно директории импортирующего файла, идентичность по
filesystem weakly_canonical. Набор loaded дедуплицирует уже обработанные
модули; стек активных canonical путей обнаруживает цикл, включая корень.
Нормализация сама по себе не является собственной системой
регистронезависимой идентичности Windows-путей.
Глубина ограничена 128 с учётом корня.
Вложенные в блоки import отвергаются.

AST модулей вставляется на месте import. Исполнение верхнеуровневого кода
происходит позже в обычном порядке. Отдельного объекта Module, exports,
namespace, package registry или поиска стандартной библиотеки нет.
Глобальные имена общие. Повторный import random не переинициализирует seed.

Ошибки разбора сохраняют доступные file/column; Analyzer добавляет цепочку
импортов через origin/sourceFile. В runtime-представлении полной source map
ещё нет: ошибок импортированных функций нельзя считать точно привязанными
к их исходным файлам. Chunk хранит номера строк, не file/column.

## 4. Семантический анализ

`analyzer.cpp`: стек областей и таблицы имён с optional arity.
В глобальной области заранее объявлены все builtinDefinitions.
Проверяются имена, redeclaration, дубликаты параметров, контекст
return/break/continue и арность известного identifier-callee.
Function объявляется перед анализом тела для собственной рекурсии;
forward declarations и автоматического hoisting нет.
Анализируются все тела/ветви, включая неисполняемые.

Analyzer не является системой статических типов или полноценным dataflow.
Переменная с функцией не наследует её arity; динамический вызов проверяет runtime.
Арность объявления не пересчитывается при последующих переназначениях функции:
не используйте переназначение функций/builtins как способ смены API.
Текущий Analyzer считает параметры и верхний уровень тела одной областью;
runtime создаёт две. Поэтому let с именем параметра прямо в теле отвергается
frontend, несмотря на потенциальное runtime-теневание. Это известное ограничение.

## 5. Значения, окружения и память

`value.h`:
`std::variant<monostate, bool, double, std::string, shared_ptr<NativeFunction>,
shared_ptr<Function>, shared_ptr<Array>>`.
Пользовательских type names шесть: nil, boolean, number, string, function, array.
Строка хранится **по значению как std::string**, не shared_ptr<string>.
Array содержит vector<Value>; присваивание массива разделяет ссылку.
Function содержит имя, параметры, невладеющий AST body или Chunk body
и shared_ptr<Environment> окружения определения.
AST/Chunk должны жить до конца исполнения функций.

Environment в `interpreter.h/.cpp` — таблица имён и ссылка на parent.
Поиск/assign идут по цепочке; define запрещает дубликат в текущей области.
Замыкания захватывают окружение целиком. Новые окружения тела и итерации
сохраняют блочную семантику и пер-итерационный захват for-переменной.

Составные объекты используют shared_ptr, tracing GC отсутствует.
Циклы array→array и function→environment→function могут удерживать память.
ОС возвращает память после завершения процесса; долгоживущая интеграция
не может считать такую модель сборкой циклического мусора.
Stringify обнаруживает массивные циклы и ограничивает глубину 128.

## 6. Общий runtime и native boundary

`runtime.h` содержит общие арифметические проверки, сравнения, modulo,
конкатенацию и арность. Value определяет truthiness и форматирование.
Числа — double; деление/modulo на ноль дают inf/nan, не LumaError.
Конкатенация принимает string/number, equality массивов/функций — идентичность.
Array index проверяется на конечность и диапазон, затем приводится к size_t:
положительная дробная часть усекается.

`natives.h`: builtinDefinitions — общий каталог 11 имён, NativeFn и optional
arity для Analyzer и registerBuiltins обоих бэкендов.
Сам native также обязан вызвать requireArity, поскольку вызов может быть
динамическим. NativeFn получает output, уже вычисленные vector<Value> args
и номер строки. inputStream — тестовый шов для stdin.
Обычные builtins: print, len, push, str, num, input, assert, type.
Низкоуровневые: _luma_sqrt, _luma_pow, _luma_slice.
Они публично достижимы; приватность — соглашение, не механизм языка.
Нельзя добавлять builtin только в runtime или только в Analyzer.

Stdlib — обычные файлы Luma, 19 public и 3 helper функции.
Поиск, trim/join, RNG и консольная композиция используют существующий язык.
Native нужны для извлечения байтов непроиндексируемой строки, корректных
sqrt/pow, проверки типа и сообщения о доменных ошибках.
Для random не нужны часы, OS API или новый native.
Нового синтаксиса, типа, opcode или механизма модулей в 0.2.0 нет.

## 7. Compiler и Chunk

`compiler.cpp` переводит AST в стековые инструкции; функции — отдельные
FunctionProto с Chunk тела. Chunk: vector<uint8_t> code, vector<int> lines
(номер строки на каждый байт), пул double/string/FunctionProto.
Операнд 16-bit little-endian. JUMP/JUMP_IF_FALSE имеют signed relative offset
от конца инструкции; LOOP — unsigned обратное смещение.
Счётчики аргументов, элементов и пул констант ограничены 65535;
forward jump до 32767, обратный LOOP до 65535 байтов.

Группы инструкций:
constant/literal, POP/DUP, именованные DEFINE_VAR/GET_VAR/SET_VAR,
арифметика/CONCAT/сравнения, массивы/индексы,
переходы, ENTER_SCOPE/EXIT_SCOPE, CLOSURE/CALL/RETURN и FAIL.
Слотов, GET_LOCAL/upvalue machine, optimizer и native codegen нет.

JUMP_IF_FALSE **снимает условие**. Для and/or Compiler сначала эмитит DUP,
сохраняя исходный операнд; при необходимости POP и правая часть.
!= реализуется EQUAL+NOT; <=/>= — упорядоченное сравнение или equality,
с однократным вычислением операндов и скрытыми именами.
Это сохраняет ложный результат ordered comparison при NaN.
For использует скрытый счётчик/границу/шаг; continue проходит через инкремент.
Return VM восстанавливает окружения и стек кадра.
Неявный return nil добавляется с учётом последней **инструкции**,
не последнего байта, который мог быть операндом.

## 8. Валидация и десериализация

`chunk.cpp`: validateChunk вызывается перед сериализацией, после чтения
и на входе VM. Проверяет opcode/длину, operand/constants, boundary переходов,
таблицу строк, прототипы (в том числе невызванные), stack/scope flow.
Циклические/null прототипы запрещены; вложенность функций и областей ограничена 128.
Проверки структуры не равнозначны проверке типов будущих runtime-значений.

Сигнатура `LBC\x01`; double сериализуется через 64-bit представление,
числовые поля little-endian. Сохраняются код, строки и константы рекурсивно,
но не исходный текст, file/column, runtime-состояние или builtin-реализация.
После корневого Chunk не должно быть trailing data.

ReadBudget 64 MiB расходуется на сериализованные данные и часть
аллокаций структур. Секция кода максимум 16 MiB, строка 8 MiB,
prototype depth 128; до resize/reserve проверяются размеры и бюджет.
Размер файла <=64 MiB сам по себе не гарантирует принятия.
Формат внутренний, не stable ABI и не security boundary.
Пользовательский workflow: [BYTECODE](docs/BYTECODE.md).

## 9. VM и интерпретатор

VM хранит value stack, scope stack и vector<Frame>; вызовы Luma-функций
не рекурсируют по стеку C++. Frame содержит Chunk, ip, сохранённое Environment
и размеры стеков при входе. Native вызывается непосредственно через NativeFn.
Лимиты цикла VM: стек значений 1048576, scope stack и frames 16384;
проверка превышения производится между инструкциями.
Нет instruction budget, timeout или общего heap limit.

Interpreter рекурсивно обходит AST; RAII восстанавливает области при
сигналах break/continue/return. Он нужен как простой альтернативный бэкенд
и эталон регрессионных тестов, не для произвольно глубокой рекурсии.

Позитивные/negative тесты сверяют два бэкенда, но это не доказательство
эквивалентности всех возможных программ. Известные нюансы:

- индексное assign: Interpreter сначала RHS, VM сначала object/index;
  избегайте побочных эффектов в цели такого присваивания;
- for с некорректными типами границ/шага: Interpreter проверяет их явно,
  VM обнаруживает ошибки через сгенерированные сравнения/арифметику;
  сообщения и момент ошибки могут отличаться;
- обе реализации не гарантируют завершение for с NaN/inf либо шагом,
  который из-за double precision не меняет счётчик;
- Analyzer заранее анализирует имена, но runtime ищет их в текущем
  содержимом захваченного окружения; не полагайтесь на позднее теневание
  имён после определения замыкания.

## 10. Диагностика и CLI

LumaError хранит line, optional column/file и сообщение.
CLI добавляет доступный источник и байтовый caret. Все фазы используют
`Error at line ...`, а не отдельный префикс Runtime error.
Не все compiler/runtime/import ошибки имеют точную column/file.
Artifact execution сохраняет только line; ошибки loader часто line == 0.

Коды 0/64/65/66 определены в main.cpp: успех/usage/program-or-bytecode/open-file.
Missing import — LumaError/65. --help пишет stderr и возвращает 0.
Работа с консолью Windows включает UTF-8 через SetConsoleCP/SetConsoleOutputCP.
[Полный CLI](docs/CLI.md).

## 11. Структура и проверка изменений

| Путь | Назначение |
| --- | --- |
| src/ | Lexer, Parser, AST, Analyzer, imports, Interpreter, Compiler, Chunk, VM, runtime |
| stdlib/ | Luma-модули |
| tests/ | C++ unit suite, PowerShell CLI и documentation проверки |
| examples/ | Программы, библиотечные fixtures и отладочные примеры |
| docs/ | Пользовательские руководства |
| build.bat | C++17 MinGW-w64, -Wall -Wextra -static; compiler и unit executable |
| CHANGELOG.md | История выпусков |
| CONTRIBUTING.md | Правила изменения проекта |

build.bat не включает -O2/-O3, CMake или CI; не называйте такую сборку
оптимизированной release-сборкой. Она создаёт статически линкованный Windows exe.
Статический exe компилятора не означает native compilation программ Luma.
Версия 0.2.0 проверяется 244 unit- и 57 CLI-тестами; отдельный docs suite
проверяет текущие примеры и ссылки. Правила — [CONTRIBUTING](CONTRIBUTING.md).
