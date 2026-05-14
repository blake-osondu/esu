cmake_minimum_required(VERSION 3.16)
project(Esu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Third-party: mongoose (HTTP server/client)
add_library(mongoose STATIC third_party/mongoose/mongoose.c)
target_include_directories(mongoose PUBLIC ${CMAKE_SOURCE_DIR}/third_party/mongoose)
target_compile_definitions(mongoose PUBLIC MG_ENABLE_LOG=0)
set_source_files_properties(third_party/mongoose/mongoose.c
    PROPERTIES COMPILE_FLAGS "-w"
)

# Third-party: cJSON (JSON parsing)
add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_SOURCE_DIR}/third_party/cjson)
set_source_files_properties(third_party/cjson/cJSON.c
    PROPERTIES COMPILE_FLAGS "-w"
)

# Third-party: SQLite (single-file amalgamation)
add_library(sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(sqlite PUBLIC
    SQLITE_THREADSAFE=0           # single-threaded; matches our runtime
    SQLITE_OMIT_LOAD_EXTENSION=1   # no dlopen — smaller binary, safer
    SQLITE_DQS=0                   # reject double-quoted string literals (correct SQL)
)
set_source_files_properties(third_party/sqlite/sqlite3.c
    PROPERTIES COMPILE_FLAGS "-w"
)

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
    runtime/esu_json.c
    runtime/esu_http.c
    runtime/esu_db.c
)
target_link_libraries(esu_runtime PRIVATE m mongoose cjson sqlite)
target_include_directories(esu_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/runtime
    ${CMAKE_SOURCE_DIR}/third_party/mongoose
    ${CMAKE_SOURCE_DIR}/third_party/cjson
    ${CMAKE_SOURCE_DIR}/third_party/sqlite
)

# Esu compiler executable
add_executable(esu
    src/main.c
    src/lexer.c
    src/normalizer.c
    src/ast.c
    src/parser.c
    src/resolver.c
    src/module.c
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
