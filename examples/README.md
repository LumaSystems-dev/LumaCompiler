# Примеры Luma 0.2.0

Команды из корня проекта после build.bat. Обычно подходит
`.\luma.exe vm examples\NAME.luma` или run.
Исторические комментарии «Этап»/«0.1.1» обозначают момент появления
конструкций; примеры проверяются текущей 0.2.0.

## Первые программы

```powershell
.\luma.exe vm examples\first_program.luma
.\luma.exe vm examples\hello.luma
.\luma.exe vm examples\stdlib_demo.luma
```

stdlib_demo просит имя и seed: например Ada и 1, на разных строках.
Workflow: compile с отдельным output, затем execute. Не коммитьте артефакты.

## Каталог

| Файл | Назначение / запуск / ввод |
| --- | --- |
| [first_program.luma](first_program.luma) | Переменные, присваивания и print; vm/run |
| [hello.luma](hello.luma) | Витрина синтаксиса, замыкания, массивы и циклы; vm/run |
| [functions_demo.luma](functions_demo.luma) | Рекурсия и замыкания; vm/run |
| [array_demo.luma](array_demo.luma) | push, изменения и вложенный массив; vm/run |
| [if_demo.luma](if_demo.luma) | Условия и области; vm/run |
| [loop_demo.luma](loop_demo.luma) | while/for/break/continue; vm/run |
| [input_demo.luma](input_demo.luma) | Имя и возраст; vm/run, ввод Ada и 21 |
| [import_demo.luma](import_demo.luma) | Относительный import; vm/run; нужен lib/math.luma |
| [stdlib_demo.luma](stdlib_demo.luma) | Экспедиция со всеми stdlib-модулями; vm/run, имя и seed |
| [rpg_demo.luma](rpg_demo.luma) | «Тени у Каменного моста»; имя, warrior/mage, затем attack/heal/flee |
| [rpg_adventure/main.luma](rpg_adventure/main.luma) | Модульная RPG; имя, класс, attack/special/heal/inspect/flee |
| [HELLOWORLD.luma](HELLOWORLD.luma) | Историческая игра «Подземелье», не Hello World; vm/run, 3 для выхода |
| [lex_demo.luma](lex_demo.luma) | tokens; также исполняется vm/run |
| [ast_demo.luma](ast_demo.luma) | ast; также исполняется vm/run |
| [bytecode_demo.luma](bytecode_demo.luma) | bytecode; также исполняется vm/run |
| [eval_demo.luma](eval_demo.luma) | **Только eval**, одно выражение; результат 9! |
| [analyzer_demo.luma](analyzer_demo.luma) | **Намеренная ошибка**; vm/run/compile, код 65 и Unknown variable 'score' |

analyzer_demo не печатает «проверка началась»: Analyzer отклоняет AST
до исполнения. Это negative fixture, не сломанный рабочий пример.
eval_demo не является program-файлом для vm/run.
HELLOWORLD не обрабатывает EOF: в автоматическом тесте обязательно
подайте 3, иначе цикл может повторяться бесконечно.
input_demo требует обе строки: EOF вместо имени/числа вызовет ошибку типов.

## Библиотечные fixtures

| Файл | Роль |
| --- | --- |
| [lib/math.luma](lib/math.luma) | add, square и moduleTag для import_demo; **не stdlib/math** |
| [rpg_adventure/lib/ui.luma](rpg_adventure/lib/ui.luma) | Консольный интерфейс RPG |
| [rpg_adventure/lib/characters.luma](rpg_adventure/lib/characters.luma) | Массивы характеристик и способности |
| [rpg_adventure/lib/combat.luma](rpg_adventure/lib/combat.luma) | Враги; вложенные повторные импорты ui/characters |

Файлы можно разобрать/скомпилировать отдельно; основной сценарий — import
из main. Они не являются самостоятельными интерактивными приложениями.
Пути отсчитываются от импортирующего файла, независимо от текущей консоли.

## Проверки

cli_tests.ps1 проверяет победу RPG из source/.lbc и stdlib_demo.
documentation_tests.ps1 дополнительно проверяет все .luma здесь
с соответствующей командой, вводом или ожидаемой ошибкой.
[Язык](../docs/LANGUAGE.md), [CLI](../docs/CLI.md).
