cmake_minimum_required(VERSION 3.16)
project(Esu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Runtime static library
add_library(esu_runtime STATIC
    runtime/esu_runtime.c
    runtime/esu_value.c
    runtime/esu_mem.c
    runtime/esu_io.c
    runtime/esu_ops.c
    runtime/esu_string.c
    runtime/esu_list.c
    runtime/esu_map.c
    runtime/esu_error.c
    runtime/esu_math.c
    runtime/esu_file.c
    runtime/esu_method.c
)
target_link_libraries(esu_runtime PRIVATE m)
target_include_directories(esu_runtime PUBLIC ${CMAKE_SOURCE_DIR}/runtime)

# Esu compiler executable
add_executable(esu
    src/main.c
    src/lexer.c
    src/normalizer.c
    src/ast.c
    src/parser.c
    src/resolver.c
    src/codegen.c
    src/error.c
    src/util.c
    third_party/linenoise/linenoise.c
)
target_include_directories(esu PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/linenoise
)

# Suppress pedantic warnings only for third-party linenoise
set_source_files_properties(third_party/linenoise/linenoise.c
    PROPERTIES COMPILE_FLAGS "-Wno-variadic-macro-arguments-omitted"
)

# Install the runtime headers/lib so compiled programs can link against it
install(TARGETS esu_runtime ARCHIVE DESTINATION lib)
install(DIRECTORY runtime/ DESTINATION include/esu FILES_MATCHING PATTERN "*.h")
install(TARGETS esu RUNTIME DESTINATION bin)
