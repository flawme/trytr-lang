execute_process(
    COMMAND "${TRYTR_EXECUTABLE}" "${TRYTR_SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(result EQUAL 0)
    message(FATAL_ERROR "invalid fixture unexpectedly succeeded")
endif()
if(NOT standard_error MATCHES "TRYTR-E-DUPLICATE-ID")
    message(FATAL_ERROR "expected duplicate ID diagnostic; stderr was: ${standard_error}")
endif()
