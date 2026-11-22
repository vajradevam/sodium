# Cyan modules and cyanlib conventions (D-034)

## Composition: textual `#include`

Cyan does not have an `import` keyword. Source files are composed by the preprocessor with:

```
#include "relative_or_local_name.cyan"   // containing file's directory first
#include <name.cyan>                     // -I search paths, then the cyanlib directory
```

Search paths come from `-I <dir>` flags; the compiler's cyanlib directory is searched last.
Included files are expanded in place before tokenization, so all declarations live in one global
namespace.

## Header guards

Every reusable header must be safe to include more than once. cyanlib uses `#pragma once`:

```
#pragma once
// declarations ...
```

The classic guard form is also supported (and is portable to tools that ignore `#pragma once`):

```
#ifndef CY_EXAMPLE_H
#define CY_EXAMPLE_H
// declarations ...
#endif
```

`#ifdef`, `#ifndef`, `#else`, `#endif`, object/function-like `#define` macros and `#if` constant
expressions are available (D-033). Inactive branches emit no tokens at all.

## cyanlib naming convention

Because there is no namespace construct, module ownership is expressed by prefixes:

| Module | File | Prefix / names |
| --- | --- | --- |
| Containers | `cyanlib/containers.cyan` | `Vec`, `Str`, `Map`, `Slot` types; `vec_*`, `str_*`, `map_*` functions |
| Formatting | `cyanlib/fmt.cyan` | `fmt_*`, `itoa`, `binstr`, `octstr`, `hexstr*`, `atoi`, `str_to_i64`, `ParseInt` |
| I/O | `cyanlib/io.cyan` | `IO_*` constants; `io_*`, `eprint*`, `fprint`, `assert` |
| Memory | `cyanlib/mem.cyan` | `mem_*`, `calloc`, `realloc`, `aligned_alloc/free`, `SODIUM_ALLOC_DEBUG` |
| Strings | `cyanlib/string.cyan` | C-compatible `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strchr`, `strstr`, `memcpy`, `memmove`, `memset`, `memcmp` |

Rules for new cyanlib APIs:

1. Prefix module-specific functions and constants with the module prefix (`foo_*`, `FOO_*`).
2. Reserve CamelCase names for struct types.
3. Only C-compatible shims may be unprefixed; adding one must not collide with an existing name.
4. Structs are passed by pointer and use the `*_init` / operations / `*_free` lifecycle.
5. Duplicate declarations across headers are compile errors — never rely on silent overriding.

`#pragma once` and guards make duplicate *inclusion* harmless, but not duplicate *declarations*
from two different headers using the same name.
