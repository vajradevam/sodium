# Cyan Language — Feature Roadmap

## High Priority (core language completeness)

- **`else if` chaining** — currently only `else { if ... }` is possible
- **`break` / `continue`** — essential for loop control
- **Compound assignment** — `+=`, `-=`, `*=`, `/=`, `%=`
- **Increment/decrement** — `++i`, `i++`, `--i`, `i--`
- **Modulo operator** — `%`
- **Bitwise operators** — `&`, `|`, `^`, `~`, `<<`, `>>`
- **Ternary conditional** — `cond ? a : b`
- **Variable initializers** — `var x = 5;` for scalars
- **More integer types** — `char`, `bool`, `byte`
- **`do-while` loops**

## Medium Priority (expressiveness)

- **Arrays as function parameters** (pass pointer/reference)
- **Array literal initializers** — `var arr[] = {1, 2, 3};`
- **String operations** — escape sequences, concatenation, `.length`
- **Global/static variables** (`.data` section)
- **Input built-in** — `read()` for stdin
- **Type annotations** — optional explicit typing
- **Multi-dimensional arrays**
- **`switch`/`case` statements**

## Lower Priority (advanced features)

- **Structs** — compound data types
- **Pointers** — `*` dereference, `&` address-of
- **Enum types**
- **Type aliases** (`typedef`)
- **Include mechanism** — multi-file compilation
- **Function pointers**
- **Inline assembly**
- **Basic optimization** — constant folding, dead code elimination
- **Error recovery** — report multiple errors instead of aborting on first
- **Self-hosting** — compiler written in Cyan itself

## Infrastructure

- **Source locations in errors** — file, line, column for all error messages
- **IR / intermediate representation** — enables optimization and alternative backends
- **Runtime library** — pre-compiled helpers (`memcpy`, `printf`-style formatting, etc.)
- **Better test framework** — parameterized tests, expected-failure tests
