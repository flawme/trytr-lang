cmake_minimum_required(VERSION 3.20)

project(
    trytr
    VERSION 0.1.0
    DESCRIPTION "The trytr visual document language"
    LANGUAGES C
)

option(TRYTR_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(trytr_build_options INTERFACE)
target_compile_features(trytr_build_options INTERFACE c_std_23)
target_compile_options(
    trytr_build_options
    INTERFACE
        "$<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>"
        "$<$<C_COMPILER_ID:MSVC>:/W4>"
)

if(TRYTR_ENABLE_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "TRYTR_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    target_compile_options(
        trytr_build_options
        INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(trytr_build_options INTERFACE -fsanitize=address,undefined)
endif()

function(trytr_target_defaults target)
    target_link_libraries(${target} PRIVATE trytr_build_options)
    set_target_properties(
        ${target}
        PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    )
endfunction()

add_library(
    trytr_core
    STATIC
        src/trytr.c
        src/arena.c
        src/diagnostic.c
        src/ast.c
        src/lexer.c
        src/path.c
        src/parser.c
        src/validator.c
        src/ir.c
        src/vm.c
        src/svg_render.c
        src/document.c
)
target_include_directories(trytr_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
trytr_target_defaults(trytr_core)

add_executable(trytr src/main.c)
target_link_libraries(trytr PRIVATE trytr_core)
trytr_target_defaults(trytr)

include(CTest)

if(BUILD_TESTING)
    function(trytr_add_test target test_name source)
        add_executable(${target} ${source})
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${target} PRIVATE trytr_core)
        trytr_target_defaults(${target})
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    trytr_add_test(trytr_version_tests trytr.version tests/test_version.c)
    trytr_add_test(trytr_lexer_tests trytr.lexer tests/test_lexer.c)
    trytr_add_test(trytr_parser_tests trytr.parser tests/test_parser.c)
    trytr_add_test(trytr_expression_tests trytr.expressions tests/test_expressions.c)
    trytr_add_test(trytr_path_tests trytr.path tests/test_path.c)
    trytr_add_test(trytr_group_tests trytr.groups tests/test_groups.c)
    trytr_add_test(trytr_document_tests trytr.document tests/test_document.c)
    trytr_add_test(trytr_vm_tests trytr.vm tests/test_vm.c)
    trytr_add_test(trytr_svg_tests trytr.svg tests/test_svg.c)

    add_test(NAME trytr.cli.version COMMAND trytr --version)
    set_tests_properties(
        trytr.cli.version
        PROPERTIES PASS_REGULAR_EXPRESSION "trytr 0\\.1\\.0"
    )

    add_test(
        NAME trytr.cli.valid
        COMMAND trytr "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/valid.trytr"
    )
    add_test(
        NAME trytr.cli.render_groups
        COMMAND trytr --render-svg "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/groups.trytr"
    )
    set_tests_properties(
        trytr.cli.render_groups
        PROPERTIES PASS_REGULAR_EXPRESSION "<g id="
    )
    add_test(
        NAME trytr.cli.dump_group_events
        COMMAND trytr --dump-ir "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/groups.trytr"
    )
    set_tests_properties(
        trytr.cli.dump_group_events
        PROPERTIES PASS_REGULAR_EXPRESSION "group-begin"
    )
    add_test(
        NAME trytr.cli.invalid
        COMMAND
            ${CMAKE_COMMAND}
            "-DTRYTR_EXECUTABLE=$<TARGET_FILE:trytr>"
            "-DTRYTR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid.trytr"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_invalid_cli.cmake"
    )
endif()
