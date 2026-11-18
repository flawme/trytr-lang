if(UNIX)
    add_executable(trytr_pdf_fake_browser tests/fixtures/pdf_browser_failure.c)
    trytr_target_defaults(trytr_pdf_fake_browser)
    add_executable(trytr_pdf_process_tests tests/test_pdf_process.c src/pdf_process.c)
    target_include_directories(trytr_pdf_process_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(trytr_pdf_process_tests PRIVATE trytr_core)
    target_compile_definitions(trytr_pdf_process_tests PRIVATE
        TRYTR_CHROMIUM_EXECUTABLE="$<TARGET_FILE:trytr_pdf_fake_browser>" TRYTR_PDF_TIMEOUT_MS=200)
    trytr_target_defaults(trytr_pdf_process_tests)
    add_dependencies(trytr_pdf_process_tests trytr_pdf_fake_browser)
    add_test(NAME trytr.pdf_diagnostics COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_diagnostics.py" "$<TARGET_FILE:trytr_pdf_process_tests>")
endif()
