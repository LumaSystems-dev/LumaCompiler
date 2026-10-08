# Стандартная библиотека Luma 0.2.0

Обычные Luma-модули, без автозагрузки или специального поиска.
Из программы в `examples/`:

```luma
import "../stdlib/math.luma"
import "../stdlib/string.luma"
import "../stdlib/random.luma"
import "../stdlib/io.luma"
print(abs(-3), string_length("Luma"))
```

| Модуль | Публичные функции |
| --- | --- |
| math | abs, min, max, clamp, pow, sqrt |
| string | string_length, substr, find, contains, starts_with, ends_with, trim, join |
| random | random_seed, random, random_range |
| io | prompt, read_number |

Всего 19 публичных и 3 вспомогательные Luma-функции.
`pow`, `sqrt`, `substr` вызывают native primitives; RNG целиком на Luma.
Строковые позиции — байты UTF-8; RNG не криптографический.
Импорты делят глобальные имена; внутренние helpers доступны, но не являются API.

[Полный API, аргументы, результаты, ошибки и примеры](../docs/STDLIB.md).
[Встроенные функции](../docs/BUILTINS.md).
[Демонстрационная программа](../examples/stdlib_demo.luma).
