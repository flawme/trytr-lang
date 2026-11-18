cmake_minimum_required(VERSION 3.20)

project(
    trytr
    VERSION 0.1.0
    DESCRIPTION "The trytr visual document language"
    LANGUAGES C
)

option(TRYTR_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
include(GNUInstallDirs)

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
    target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:trytr_build_options>")
    set_target_properties(
        ${target}
        PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    )
endfunction()

add_library(
    trytr_math
    STATIC
        src/math/real.c
        src/math/expression.c
        src/math/expression_eval.c
        src/math/complex.c
        src/math/vector2.c
        src/math/matrix2.c
        src/math/point2.c
        src/math/affine2.c
        src/math/curve_interpolate.c
        src/math/curve_raster.c
)
target_include_directories(trytr_math PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
if(UNIX)
    target_link_libraries(trytr_math PUBLIC m)
endif()
trytr_target_defaults(trytr_math)

add_library(
    trytr_core
    STATIC
        src/trytr.c
        src/arena.c
        src/diagnostic.c
        src/ast.c
        src/lexer.c
        src/path.c
        src/polygon.c
        src/plot.c
        src/plot_formula.c
        src/math/notation.c
        src/math/inline.c
        src/mathml_render.c
        src/html_render.c
        src/parser.c
        src/validator.c
        src/ir.c
        src/vm.c
        src/layout/text.c
        src/layout/geometry.c
        src/layout/flow.c
        src/svg_render.c
        src/text_raster.c
        src/text_render.c
        src/document.c
        src/stream/stream.c
        src/stream/publish.c
        src/stream/snapshot.c
        src/stream/parameters.c
)
target_include_directories(trytr_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(trytr_core PUBLIC trytr_math)
trytr_target_defaults(trytr_core)

add_executable(trytr src/main.c)
target_link_libraries(trytr PRIVATE trytr_core)
trytr_target_defaults(trytr)

add_executable(trytr_stream_demo examples/stream_demo.c)
target_link_libraries(trytr_stream_demo PRIVATE trytr_core)
trytr_target_defaults(trytr_stream_demo)

# PDF export reuses Trytr's SVG output and a locally installed headless
# Chromium. Keep this optional so the language core remains dependency-free.
target_sources(trytr PRIVATE src/pdf_render.c src/pdf_process.c)
if(UNIX)
    find_program(
        TRYTR_CHROMIUM_EXECUTABLE
        NAMES chromium chromium-browser google-chrome-stable google-chrome
    )
    if(TRYTR_CHROMIUM_EXECUTABLE)
        message(STATUS "Trytr PDF browser: ${TRYTR_CHROMIUM_EXECUTABLE}")
        target_compile_definitions(
            trytr PRIVATE TRYTR_CHROMIUM_EXECUTABLE="${TRYTR_CHROMIUM_EXECUTABLE}"
        )
    endif()
endif()

include(CTest)

# Keep the reusable proof checker in its own module while building it alongside
# the language so editor integrations can offer verified equation review.
add_subdirectory(proof)
include(cmake/Install.cmake)

if(BUILD_TESTING)
    function(trytr_add_test target test_name source)
        add_executable(${target} ${source})
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${target} PRIVATE trytr_core)
        trytr_target_defaults(${target})
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    function(trytr_add_math_test target test_name source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE trytr_math)
        trytr_target_defaults(${target})
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    trytr_add_test(trytr_version_tests trytr.version tests/test_version.c)
    trytr_add_test(trytr_lexer_tests trytr.lexer tests/test_lexer.c)
    trytr_add_test(trytr_parser_tests trytr.parser tests/test_parser.c)
    trytr_add_test(trytr_expression_tests trytr.expressions tests/test_expressions.c)
    trytr_add_test(trytr_parameter_tests trytr.parameters tests/test_parameters.c)
    trytr_add_test(trytr_path_tests trytr.path tests/test_path.c)
    trytr_add_math_test(trytr_math_real_tests trytr.math.real tests/test_math_real.c)
    trytr_add_math_test(trytr_math_expression_tests trytr.math.expression tests/test_math_expression.c)
    trytr_add_math_test(trytr_math_complex_tests trytr.math.complex tests/test_math_complex.c)
    trytr_add_math_test(trytr_math_vector2_tests trytr.math.vector2 tests/test_math_vector2.c)
    trytr_add_math_test(trytr_math_matrix2_tests trytr.math.matrix2 tests/test_math_matrix2.c)
    trytr_add_math_test(trytr_math_point2_tests trytr.math.point2 tests/test_math_point2.c)
    trytr_add_math_test(trytr_math_affine2_tests trytr.math.affine2 tests/test_math_affine2.c)
    trytr_add_math_test(trytr_math_curve_interpolate_tests trytr.math.curve.interpolate tests/test_math_curve_interpolate.c)
    trytr_add_math_test(trytr_math_curve_raster_tests trytr.math.curve.raster tests/test_math_curve_raster.c)
    trytr_add_test(trytr_polygon_tests trytr.polygon tests/test_polygon.c)
    trytr_add_test(trytr_plot_tests trytr.plot tests/test_plot.c)
    trytr_add_test(trytr_roundrect_tests trytr.roundrect tests/test_roundrect.c)
    trytr_add_test(trytr_style_tests trytr.style tests/test_style.c)
    trytr_add_test(trytr_group_tests trytr.groups tests/test_groups.c)
    trytr_add_test(trytr_document_tests trytr.document tests/test_document.c)
    trytr_add_test(trytr_math_notation_tests trytr.math.notation tests/test_math_notation.c)
    trytr_add_test(trytr_mathml_tests trytr.mathml tests/test_mathml.c)
    trytr_add_test(trytr_vm_tests trytr.vm tests/test_vm.c)
    trytr_add_test(trytr_svg_tests trytr.svg tests/test_svg.c)
    trytr_add_test(trytr_text_render_tests trytr.text_render tests/test_text_render.c)
    trytr_add_test(trytr_layout_tests trytr.layout tests/test_layout.c)
    trytr_add_test(trytr_html_tests trytr.html tests/test_html.c)
    trytr_add_test(trytr_inline_math_tests trytr.inline_math tests/test_inline_math.c)
    trytr_add_test(trytr_inline_render_tests trytr.inline_render tests/test_inline_render.c)
    trytr_add_test(trytr_stream_tests trytr.stream tests/test_stream.c)
    trytr_add_test(trytr_stream_limit_tests trytr.stream.limits tests/test_stream_limits.c)
    trytr_add_test(trytr_stream_render_tests trytr.stream.render tests/test_stream_render.c)
    trytr_add_test(trytr_stream_parameter_tests trytr.stream.parameters tests/test_stream_parameters.c)
    add_test(NAME trytr.stream.demo COMMAND trytr_stream_demo "${CMAKE_CURRENT_BINARY_DIR}/stream-demo")
    set_tests_properties(trytr.stream.demo PROPERTIES PASS_REGULAR_EXPRESSION "revision 4:")

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
        NAME trytr.cli.render_text
        COMMAND trytr --render-text "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/text-graph.trytr"
    )
    set_tests_properties(
        trytr.cli.render_text
        PROPERTIES PASS_REGULAR_EXPRESSION "Revenue"
    )
    add_test(
        NAME trytr.cli.render_mathml
        COMMAND trytr --render-mathml "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/native-math.trytr"
    )
    set_tests_properties(
        trytr.cli.render_mathml
        PROPERTIES PASS_REGULAR_EXPRESSION "<mfrac>"
    )
    add_test(
        NAME trytr.cli.render_html
        COMMAND trytr --render-html "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/flow-math.trytr"
    )
    set_tests_properties(trytr.cli.render_html PROPERTIES PASS_REGULAR_EXPRESSION "<mfrac>")
    add_test(
        NAME trytr.cli.render_inline_math
        COMMAND trytr --render-html "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/inline-math.trytr"
    )
    set_tests_properties(trytr.cli.render_inline_math PROPERTIES PASS_REGULAR_EXPRESSION "display=\"inline\"")
    add_test(
        NAME trytr.cli.dump_group_events
        COMMAND trytr --dump-ir "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/groups.trytr"
    )
    set_tests_properties(
        trytr.cli.dump_group_events
        PROPERTIES PASS_REGULAR_EXPRESSION "group-begin"
    )
    add_test(
        NAME trytr.cli.parameter_override
        COMMAND trytr --set offset=8 --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/parameters.trytr"
    )
    set_tests_properties(
        trytr.cli.parameter_override
        PROPERTIES PASS_REGULAR_EXPRESSION "x=8 y=0 width=10 height=10"
    )
    add_test(
        NAME trytr.cli.dump_parameters
        COMMAND trytr --dump-parameters
                "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/interactive-peak.trytr"
    )
    set_tests_properties(
        trytr.cli.dump_parameters
        PROPERTIES PASS_REGULAR_EXPRESSION "\"id\":\"peak\".*\"value\":24"
    )
    add_test(
        NAME trytr.cli.proof
        COMMAND trytr --render-svg "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/proof-sqrt2.trytr"
    )
    set_tests_properties(
        trytr.cli.proof
        PROPERTIES PASS_REGULAR_EXPRESSION "Both p and q are even, contradicting that p and q are coprime"
    )
    add_test(
        NAME trytr.cli.invalid
        COMMAND
            ${CMAKE_COMMAND}
            "-DTRYTR_EXECUTABLE=$<TARGET_FILE:trytr>"
            "-DTRYTR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid.trytr"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_invalid_cli.cmake"
    )

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        include(cmake/PdfProcessTests.cmake)
        if(NOT TRYTR_ENABLE_SANITIZERS)
            add_test(NAME trytr.install COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_install.py" "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
        add_test(NAME trytr.math_review COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_math_review.py" "$<TARGET_FILE:trytr-proof>")
        add_test(
            NAME trytr.mcp
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mcp.py"
                "$<TARGET_FILE:trytr>"
                "$<TARGET_FILE:trytr-proof>"
        )
        if(TRYTR_CHROMIUM_EXECUTABLE)
            add_test(
                NAME trytr.cli.render_pdf
                COMMAND
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_export.py"
                    "$<TARGET_FILE:trytr>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/proof-sqrt2.trytr"
                    "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/interactive-peak.trytr"
            )
        endif()
    endif()
endif()
