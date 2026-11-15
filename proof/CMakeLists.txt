cmake_minimum_required(VERSION 3.20)

project(trytr_proof VERSION 0.1.0 LANGUAGES C)

add_library(trytr_proof STATIC
    src/kernel.c
    src/limit.c
    src/algebra.c
    src/algebra_poly.c
)
target_include_directories(trytr_proof PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(trytr_proof PUBLIC c_std_23)
target_compile_options(
    trytr_proof
    PRIVATE
        "$<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>"
)

add_executable(trytr-proof src/main.c)
target_link_libraries(trytr-proof PRIVATE trytr_proof)
target_compile_features(trytr-proof PRIVATE c_std_23)

enable_testing()
add_executable(test_proof tests/test_proof.c)
target_link_libraries(test_proof PRIVATE trytr_proof)
target_compile_features(test_proof PRIVATE c_std_23)
add_test(NAME proof_kernel COMMAND test_proof)

add_executable(test_limit tests/test_limit.c)
target_link_libraries(test_limit PRIVATE trytr_proof)
target_compile_features(test_limit PRIVATE c_std_23)
add_test(NAME limit_certificate COMMAND test_limit)

add_executable(test_algebra tests/test_algebra.c)
target_link_libraries(test_algebra PRIVATE trytr_proof)
target_compile_features(test_algebra PRIVATE c_std_23)
add_test(NAME algebra_identity COMMAND test_algebra)
