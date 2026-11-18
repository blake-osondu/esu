# Esu

**Esu is a programming language you can read out loud.** You write programs in plain English sentences, and the compiler turns them into fast native binaries.

```esu
a person has a name and an age.

to greet a person
    display "Hi, I'm " + self.name + ".".

alice is a person named "Alice" with age 30
alice.greet()

if alice's age is at least 18 then
    display "Alice is an adult"
end
```

Esu has no AI or LLM inside it. The grammar is fully deterministic: the same sentence always means the same thing. The language accepts many natural ways to say each thing, so you can write in the phrasing that feels right to you.

## Goals

- **Expressive, not restrictive.** Most languages make you learn their one correct spelling for every idea. Esu aims to accept the phrasings people already use. `x is 5`, `let x be 5` and `set the value of x to 5` all mean the same thing.
- **A small core with natural-language sugar.** Underneath the English is a small core that reads like math (`x is 5`, `a + b`, `x > y`). A *normalizer* stage rewrites the English forms into that core before parsing. This keeps the parser simple and every phrasing consistent. New syntax always gets both a short core form and natural-language variants.
- **Deterministic and predictable.** No guessing, no models, no fuzzy matching. The grammar is written by hand, and errors point to a line and column.
- **Native performance.** Esu compiles to C, and your system C compiler then builds a standalone native binary.
- **Batteries included for real programs.** HTTP servers and clients, JSON, SQLite, WebSockets, file I/O and HTML templates are built into the language, so a working web app is a short program.
- **Approachable for beginners.** A REPL with an `:explain` tutorial mode and a project-based tutorial help new programmers start without first learning symbol-heavy syntax.

## A taste of the language

**Many ways to say the same thing**

```esu
display 3 plus 2
display 3 added to 2
display 10 take away 3
display 20 divided by 4
display x is greater than y
display x exceeds y
```

**Functions and functional style**

```esu
double is a function of x that returns x * 2
is-even is a function of x that returns x % 2 == 0
adder is a function of x and y that returns x + y
numbers is [1, 2, 3, 4, 5]

display map double over numbers
display keep items from numbers where is-even
total is reduce numbers using adder starting at 0
```

**Errors**

```esu
try
    raise "something went wrong"
catch error
    display "Caught: {error}"
end
```

**A web server**

```esu
server is a new http server on port 3000

when someone visits "/" then
    respond with "Hello from Esu!".

when someone visits "/api/health" then
    respond with {"status": "ok"} as json.

start the server
```

**A database**

```esu
db is open database ":memory:"
run "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)" on db
run "INSERT INTO users (name, age) VALUES (?, ?)" on db with ["Alice", 30]
```

The [examples/](examples/) folder has more than 40 runnable programs that cover every feature.

## Features

- Dynamic types: numbers, strings, booleans, `nothing`, lists and maps
- String interpolation (`"Hello, {name}"`)
- Variables, conditionals and loops, each with many natural phrasings
- Functions, lambdas, `map` / `filter` / `reduce`
- Structs, methods, possessives (`alice's name`) and single inheritance
- Error handling with `try` / `catch` / `raise`
- Modules and imports (`use`, `bring in the math lib`, `include the models`)
- File I/O and JSON
- HTTP server (routes, path and query params, headers, cookies, redirects, static files, before-hooks) and HTTP client
- SQLite with parameters and transactions
- WebSocket server handlers and client
- HTML templates for server-side rendering
- An interactive REPL with line editing, history and `:explain`

## Getting started

You need CMake 3.16 or newer and a C11 compiler (clang or gcc).

```sh
git clone https://github.com/blake-osondu/esu.git
cd esu
cmake -B build -S .
cmake --build build
```

The compiler links each program against the runtime in `build/`. To find it, it reads `ESU_HOME`, which should point at the repository root. If `ESU_HOME` is unset, it uses the current directory.

```sh
export ESU_HOME="$(pwd)"

./build/esu run examples/hello_world.esu     # compile, run, clean up
./build/esu build examples/fizzbuzz.esu      # produce a native binary ./fizzbuzz
./build/esu build app.esu -o myapp           # choose the output name
./build/esu emit examples/functions.esu      # print the generated C
./build/esu repl                             # interactive REPL
```

## How it works

```
source.esu → lexer → normalizer → parser → resolver → codegen → C → cc → native binary
```

| Stage | Location | Role |
| --- | --- | --- |
| Lexer | [src/lexer.c](src/lexer.c) | Turns source text into tokens |
| Normalizer | [src/normalizer.c](src/normalizer.c) | Rewrites natural-language sugar into canonical forms |
| Parser | [src/parser.c](src/parser.c) | Builds the AST from the canonical token stream |
| Resolver / modules | [src/resolver.c](src/resolver.c), [src/module.c](src/module.c) | Resolves names and imports |
| Codegen | [src/codegen.c](src/codegen.c) | Emits C code |
| Runtime | [runtime/](runtime/) | Dynamic values, memory, strings, collections, HTTP, JSON, SQLite, templates |

Third-party code lives in [third_party/](third_party/): [Mongoose](https://github.com/cesanta/mongoose) (HTTP and WebSockets), [cJSON](https://github.com/DaveGamble/cJSON), [SQLite](https://sqlite.org) and [linenoise](https://github.com/antirez/linenoise). Each keeps its own license.

## Status

Esu is an early, experimental language and is under active development. Syntax and runtime behavior may change between versions.
