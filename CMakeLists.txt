cmake_minimum_required(VERSION 3.16)
project(Calculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

# ---- Engine: calc::core ------------------------------------------------
# The calculation engine. No I/O, no user-facing text, no dependency on the
# frontend. Everything reachable through CalculatorCore.h lives here.

add_library(calc_core STATIC
    src/core/Ast.h
    src/core/Builtins.h          src/core/Builtins.cpp
    src/core/CalculatorCore.h    src/core/CalculatorCore.cpp
    src/core/DiagCode.h
    src/core/Parser.h            src/core/Parser.cpp
    src/core/Printer.h           src/core/Printer.cpp
    src/core/Simplifier.h        src/core/Simplifier.cpp
    src/core/Tokenizer.h         src/core/Tokenizer.cpp
    src/core/Types.h
    src/core/VMbytecode.h        src/core/VMbytecode.cpp
)
# Consumers of the engine get src/core on their include path. The engine
# itself needs nothing from the frontend.
target_include_directories(calc_core PUBLIC ${CMAKE_SOURCE_DIR}/src/core)

# ---- Frontend: calc ----------------------------------------------------
# The console frontend. Depends on the engine through its single public
# header; the dependency is one-way (frontend -> core, never the reverse).
add_library(calc_frontend STATIC
    src/frontend/Grapher.h       src/frontend/Grapher.cpp
    src/frontend/Repl.h          src/frontend/Repl.cpp
    src/frontend/StringTable.h   src/frontend/StringTable.cpp
)
target_include_directories(calc_frontend PUBLIC ${CMAKE_SOURCE_DIR}/src/frontend)
target_link_libraries(calc_frontend PUBLIC calc_core)

# ---- CLI binary --------------------------------------------------------
add_executable(calculator src/frontend/main.cpp)
target_link_libraries(calculator PRIVATE calc_frontend)

# Presentation data files (help text, error messages, UI strings, command
# names). They are resolved at runtime relative to the executable's own
# location, so they must sit next to the binary. Copy them there after each
# build. Using $<TARGET_FILE_DIR:...> (rather than CMAKE_BINARY_DIR) puts them
# in the right place for multi-config generators too, where the binary lands
# in a per-config subdirectory like Debug/.
set(CALCULATOR_DATA_FILES
    ${CMAKE_SOURCE_DIR}/data/Help.txt
    ${CMAKE_SOURCE_DIR}/data/Errors.txt
    ${CMAKE_SOURCE_DIR}/data/Ui.txt
    ${CMAKE_SOURCE_DIR}/data/Commands.txt
)
add_custom_command(TARGET calculator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CALCULATOR_DATA_FILES} $<TARGET_FILE_DIR:calculator>
    COMMENT "Staging data files next to the calculator binary"
    VERBATIM
)

# ---- Tests -------------------------------------------------------------
option(CALCULATOR_BUILD_TESTS "Build unit tests" ON)
if(CALCULATOR_BUILD_TESTS)
    enable_testing()
    add_executable(calc_tests
        tests/test_main.cpp
        tests/test_framework.h
    )
    # Tests exercise both layers, so they link the frontend (which pulls in
    # the engine transitively).
    target_link_libraries(calc_tests PRIVATE calc_frontend)
    # The tests construct a Repl, which loads the data files relative to the
    # test executable's location. Stage them next to the test binary too.
    add_custom_command(TARGET calc_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CALCULATOR_DATA_FILES} $<TARGET_FILE_DIR:calc_tests>
        COMMENT "Staging data files next to the test binary"
        VERBATIM
    )
    add_test(NAME calc_tests COMMAND calc_tests)
endif()

include(GNUInstallDirs)
install(TARGETS calculator
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# The runtime lookup is relative to the executable, so the data files must be
# installed into the same directory as the binary.
install(FILES ${CALCULATOR_DATA_FILES}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)