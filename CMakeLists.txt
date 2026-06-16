cmake_minimum_required(VERSION 3.16)
project(Calculator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Provides the standard install-location variables (CMAKE_INSTALL_LIBDIR,
# INCLUDEDIR, BINDIR). Needed by both the application install and the engine
# export below, so include it once, unconditionally.
include(GNUInstallDirs)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(CALC_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
elseif(MSVC)
    set(CALC_WARNING_FLAGS /W4 /permissive-)
endif()

# Whether this project is the top-level build (someone running it directly) or
# pulled into another project via add_subdirectory. We only do application-level
# things (build the binary, stage data files, run tests, install) when we are
# the top-level project, so a consumer who just wants the engine isn't forced to
# build or install the frontend.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    # PROJECT_IS_TOP_LEVEL is provided automatically on 3.21+.
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

# ---- Engine: calc::core ------------------------------------------------
# The calculation engine. No I/O, no user-facing text, no dependency on the
# frontend. Everything reachable through CalculatorCore.h lives here. This is
# the reusable library; the frontend below is simply its first consumer.

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

# Namespaced alias: consumers link `Calc::core`. The `::` makes CMake resolve
# it to this target or fail loudly, so it can never silently collide with a
# similarly named target in the consuming project.
add_library(Calc::core ALIAS calc_core)

# When installed and exported under the `Calc::` namespace, the target's
# EXPORT_NAME (not its raw target name) is what follows the namespace. Without
# this, the export would be `Calc::calc_core`, mismatching the alias above;
# setting it to `core` makes find_package consumers and add_subdirectory
# consumers see the identical name, `Calc::core`.
set_target_properties(calc_core PROPERTIES EXPORT_NAME core)

# Include path differs between building against this source tree (BUILD_INTERFACE)
# and linking an installed copy (INSTALL_INTERFACE). A consumer using
# add_subdirectory gets the former; one using find_package on an installed
# package gets the latter. Without this split, an installed package would point
# at a source directory that may not exist on the consumer's machine.
target_include_directories(calc_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Warning flags are PRIVATE so they apply when WE build the engine but are not
# forced onto consumers, who have their own warning policies.
target_compile_options(calc_core PRIVATE ${CALC_WARNING_FLAGS})

# The engine requires C++20, and this requirement propagates to consumers
# (PUBLIC) so anyone linking Calc::core is compiled as at least C++20.
target_compile_features(calc_core PUBLIC cxx_std_20)

# Everything below is the APPLICATION: the console frontend, the CLI binary,
# its data files, and the tests. A consumer who only wants the engine never
# reaches any of it.
if(PROJECT_IS_TOP_LEVEL)
# The console frontend. Depends on the engine through its single public
# header; the dependency is one-way (frontend -> core, never the reverse).
add_library(calc_frontend STATIC
    src/frontend/Grapher.h       src/frontend/Grapher.cpp
    src/frontend/Repl.h          src/frontend/Repl.cpp
    src/frontend/StringTable.h   src/frontend/StringTable.cpp
)
target_include_directories(calc_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/frontend>
)
target_compile_options(calc_frontend PRIVATE ${CALC_WARNING_FLAGS})
target_link_libraries(calc_frontend PUBLIC calc_core)

# ---- CLI binary --------------------------------------------------------
add_executable(calculator src/frontend/main.cpp)
target_compile_options(calculator PRIVATE ${CALC_WARNING_FLAGS})
target_link_libraries(calculator PRIVATE calc_frontend)

# Presentation data files (help text, error messages, UI strings, command
# names). They are resolved at runtime relative to the executable's own
# location, so they must sit next to the binary. Copy them there after each
# build. Using $<TARGET_FILE_DIR:...> (rather than CMAKE_BINARY_DIR) puts them
# in the right place for multi-config generators too, where the binary lands
# in a per-config subdirectory like Debug/.
set(CALCULATOR_DATA_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/Help.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/data/Errors.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/data/Ui.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/data/Commands.txt
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
    target_compile_options(calc_tests PRIVATE ${CALC_WARNING_FLAGS})
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
    # Bound the whole suite's runtime. The simplifier performance regression
    # tests complete in well under a second when healthy; if a change ever
    # reintroduces the exponential-simplification bug they would otherwise hang
    # indefinitely. A timeout turns that hang into a clean "Timeout" failure
    # instead of stalling CI until the runner-level limit kills it.
    set_tests_properties(calc_tests PROPERTIES TIMEOUT 60)
endif()  # CALCULATOR_BUILD_TESTS

# ---- Install the application -------------------------------------------
install(TARGETS calculator
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# The runtime lookup is relative to the executable, so the data files must be
# installed into the same directory as the binary.
install(FILES ${CALCULATOR_DATA_FILES}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

endif()  # PROJECT_IS_TOP_LEVEL

# ---- Install & export the engine ---------------------------------------
# This is what makes `find_package(Calc)` work from an unrelated project. It is
# done unconditionally (not gated on top-level) so that even when this project
# is consumed via add_subdirectory, a parent that installs its own targets can
# also install the engine if it links against it.

# Install the compiled library, and record it in an export set named
# "CalcTargets" that we turn into a config file below.
install(TARGETS calc_core
    EXPORT CalcTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install ONLY the public headers. The pimpl in CalculatorCore.h keeps the AST
# and every other internal type out of the public surface. The public closure
# is three headers: CalculatorCore.h (the engine API), Types.h (which it
# includes, for Result/Diagnostic), and DiagCode.h (which Types.h includes, for
# the diagnostic codes carried in Result). The remaining internal headers
# (Ast.h, Simplifier.h, Parser.h, ...) are deliberately NOT installed.
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/CalculatorCore.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/DiagCode.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Write the file that, when installed, defines the imported `Calc::core` target
# for find_package consumers. The NAMESPACE here is what produces the `Calc::`
# prefix, matching the add_subdirectory alias so consumer code is identical
# either way.
install(EXPORT CalcTargets
    FILE CalcTargets.cmake
    NAMESPACE Calc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Calc
)

# Generate a tiny CalcConfig.cmake that find_package(Calc) loads; it simply
# pulls in the targets file above. Generated from the template Config.cmake.in.
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CalcConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Calc
)
# A version file so consumers can request find_package(Calc 1.0).
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CalcConfigVersion.cmake
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CalcConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CalcConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Calc
)