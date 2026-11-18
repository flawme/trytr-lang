include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set_target_properties(trytr_core PROPERTIES EXPORT_NAME core)
set_target_properties(trytr_math PROPERTIES EXPORT_NAME math)
set_target_properties(trytr_proof PROPERTIES EXPORT_NAME proof)

install(TARGETS trytr trytr-proof trytr_core trytr_math trytr_proof
    EXPORT TrytrTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/trytr"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/proof/include/trytr"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/README.md" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/" DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs")
install(FILES "${PROJECT_SOURCE_DIR}/proof/README.md" DESTINATION "${CMAKE_INSTALL_DOCDIR}/proof")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/proof/examples/" DESTINATION "${CMAKE_INSTALL_DOCDIR}/proof/examples")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/examples/" DESTINATION "${CMAKE_INSTALL_DOCDIR}/examples")

set(trytr_package_directory "${CMAKE_INSTALL_LIBDIR}/cmake/Trytr")
configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/TrytrConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/TrytrConfig.cmake" INSTALL_DESTINATION "${trytr_package_directory}")
write_basic_package_version_file("${PROJECT_BINARY_DIR}/TrytrConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMinorVersion)
install(EXPORT TrytrTargets NAMESPACE trytr:: DESTINATION "${trytr_package_directory}")
install(FILES "${PROJECT_BINARY_DIR}/TrytrConfig.cmake" "${PROJECT_BINARY_DIR}/TrytrConfigVersion.cmake"
    DESTINATION "${trytr_package_directory}")

set(CPACK_PACKAGE_NAME trytr)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_FILE_NAME "trytr-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)
