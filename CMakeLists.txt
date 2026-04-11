cmake_minimum_required(VERSION 3.16)
project(Babel C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Runtime static library
add_library(babel_runtime STATIC
    runtime/babel_runtime.c
    runtime/babel_value.c
    runtime/babel_mem.c
    runtime/babel_io.c
    runtime/babel_ops.c
)
target_link_libraries(babel_runtime PRIVATE m)
target_include_directories(babel_runtime PUBLIC ${CMAKE_SOURCE_DIR}/runtime)

# Babel compiler executable
add_executable(babel
    src/main.c
    src/lexer.c
    src/normalizer.c
    src/ast.c
    src/parser.c
    src/codegen.c
    src/error.c
    src/util.c
)
target_include_directories(babel PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Install the runtime headers/lib so compiled programs can link against it
install(TARGETS babel_runtime ARCHIVE DESTINATION lib)
install(DIRECTORY runtime/ DESTINATION include/babel FILES_MATCHING PATTERN "*.h")
install(TARGETS babel RUNTIME DESTINATION bin)
