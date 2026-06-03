cmake_minimum_required(VERSION 3.16)
project(Calculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

add_library(calc_core STATIC
    Ast.h
    Builtins.h
    Builtins.cpp
    CalculatorCore.cpp CalculatorCore.h
    Grapher.cpp   Grapher.h
    Parser.cpp    Parser.h
    Printer.cpp   Printer.h
    Repl.cpp      Repl.h
    StringTable.cpp StringTable.h
    Simplifier.cpp Simplifier.h
    Tokenizer.cpp Tokenizer.h
    Types.h
    VMbytecode.h  VMbytecode.cpp
)

add_executable(calculator main.cpp)
target_link_libraries(calculator PRIVATE calc_core)

# Presentation data files (help text, error messages), copied next to the
# binaries so the default cwd-relative lookup finds them. Plain text, no
# build dependency.
configure_file(${CMAKE_SOURCE_DIR}/help.txt     ${CMAKE_BINARY_DIR}/help.txt     COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/errors.txt   ${CMAKE_BINARY_DIR}/errors.txt   COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/ui.txt       ${CMAKE_BINARY_DIR}/ui.txt       COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/commands.txt ${CMAKE_BINARY_DIR}/commands.txt COPYONLY)

enable_testing()
add_executable(calc_tests test_main.cpp test_framework.h)
target_link_libraries(calc_tests PRIVATE calc_core)
add_test(NAME calc_tests COMMAND calc_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
