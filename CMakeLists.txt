cmake_minimum_required(VERSION 3.24)

# uefi2hsinv is intentionally a superbuild.  The two source trees remain
# independently buildable projects; this file only composes them and keeps
# their build trees, caches, and test registries separate.
project(uefi2hsinv
    VERSION 0.1.0
    DESCRIPTION "UEFI loader plus EFI hypervisor MVP superbuild"
    LANGUAGES NONE
)

include(ExternalProject)

enable_testing()

set(COMBINED_CONFIG "Release" CACHE STRING
    "Configuration passed to component builds (Debug, Release, RelWithDebInfo, or MinSizeRel)")
set_property(CACHE COMBINED_CONFIG PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)

option(COMBINED_BUILD_UEFI_LOADER
    "Configure and build the UEFILoader component" ON)
option(COMBINED_BUILD_HYPERVISOR_EFI
    "Configure and build the Hypervisor EFI target" ON)
option(COMBINED_BUILD_COMPONENT_TESTS
    "Run each component's CTest suite through the combined test target" ON)
option(COMBINED_BUILD_EFI_RUNTIME_COMPANION
    "Build the opt-in Windows WDM companion (never installed or loaded)" OFF)
option(COMBINED_BUILD_EFI_RUNTIME_SERVICE
    "Build the opt-in Windows LocalSystem companion service/CLI" OFF)

set(COMBINED_UEFI_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/components/uefi-loader"
    CACHE PATH "UEFILoader component source directory")
set(COMBINED_HYPERVISOR_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/components/hypervisor"
    CACHE PATH "Hypervisor component source directory")
set(COMBINED_UEFI_SUPPORT_DIR
    "${COMBINED_UEFI_SOURCE_DIR}/src/vendor"
    CACHE PATH "Freestanding UEFI support sources consumed by Hypervisor EFI")
set(COMBINED_BUILD_ROOT
    "${CMAKE_BINARY_DIR}/components"
    CACHE PATH "Parent directory for isolated component build trees")

foreach(_required_path IN ITEMS
        COMBINED_UEFI_SOURCE_DIR
        COMBINED_HYPERVISOR_SOURCE_DIR
        COMBINED_UEFI_SUPPORT_DIR)
    if(NOT EXISTS "${${_required_path}}")
        message(FATAL_ERROR
            "${_required_path} does not exist: ${${_required_path}}")
    endif()
endforeach()

if(NOT EXISTS "${COMBINED_UEFI_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "UEFILoader source has no CMakeLists.txt: ${COMBINED_UEFI_SOURCE_DIR}")
endif()
if(NOT EXISTS "${COMBINED_HYPERVISOR_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Hypervisor source has no CMakeLists.txt: ${COMBINED_HYPERVISOR_SOURCE_DIR}")
endif()

file(MAKE_DIRECTORY "${COMBINED_BUILD_ROOT}")

# Keep the component caches independent.  In particular, do not add either
# source tree with add_subdirectory(): both projects set compiler/runtime
# policy globally and the Hypervisor project has optional WDK targets.
set(_combined_common_cmake_args
    -DCMAKE_BUILD_TYPE:STRING=${COMBINED_CONFIG}
    -DCMAKE_INSTALL_PREFIX:PATH=<BINARY_DIR>/install
)

if(COMBINED_BUILD_UEFI_LOADER)
    ExternalProject_Add(uefi_loader_component
        SOURCE_DIR "${COMBINED_UEFI_SOURCE_DIR}"
        BINARY_DIR "${COMBINED_BUILD_ROOT}/uefi-loader"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS
            ${_combined_common_cmake_args}
            -DIB_STATIC_RUNTIME:BOOL=ON
            -DIB_PAYLOAD_REPLAY_INDEPENDENT:BOOL=ON
            -DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreaded
        BUILD_COMMAND
            ${CMAKE_COMMAND} --build <BINARY_DIR>
                --config ${COMBINED_CONFIG} --parallel
        TEST_COMMAND
            ""
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
        STEP_TARGETS build
        EXCLUDE_FROM_ALL FALSE
    )
endif()

if(COMBINED_BUILD_HYPERVISOR_EFI)
    # The EFI target is deliberately independent of the KMDF driver and the
    # historical user-mode SCM loader.  The UEFI paths are passed explicitly so
    # that the target cannot silently pick up a different CRT/UEFI shim.
    set(_hypervisor_dependencies)
    if(TARGET uefi_loader_component)
        list(APPEND _hypervisor_dependencies uefi_loader_component)
    endif()

    ExternalProject_Add(hypervisor_component
        DEPENDS ${_hypervisor_dependencies}
        SOURCE_DIR "${COMBINED_HYPERVISOR_SOURCE_DIR}"
        BINARY_DIR "${COMBINED_BUILD_ROOT}/hypervisor"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS
            ${_combined_common_cmake_args}
            -DHYPERSINV_BUILD_DRIVER:BOOL=OFF
            -DHYPERSINV_BUILD_LOADER:BOOL=OFF
            -DHYPERSINV_BUILD_CORE:BOOL=OFF
            -DHYPERSINV_BUILD_UEFI:BOOL=ON
            -DHYPERSINV_BUILD_EFI_RUNTIME_COMPANION:BOOL=${COMBINED_BUILD_EFI_RUNTIME_COMPANION}
            -DHYPERSINV_BUILD_EFI_RUNTIME_SERVICE:BOOL=${COMBINED_BUILD_EFI_RUNTIME_SERVICE}
            -DHYPERSINV_TEST_EFI_RUNTIME_COMPANION:BOOL=ON
            -DHYPERSINV_UEFI_SOURCE_DIR:PATH=${COMBINED_UEFI_SOURCE_DIR}
            -DHYPERSINV_UEFI_SUPPORT_DIR:PATH=${COMBINED_UEFI_SUPPORT_DIR}
            -DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreaded
        BUILD_COMMAND
            ${CMAKE_COMMAND} --build <BINARY_DIR>
                --config ${COMBINED_CONFIG} --parallel
        TEST_COMMAND
            ""
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
        STEP_TARGETS build
        EXCLUDE_FROM_ALL FALSE
    )

    # Make the intended artifact an explicit root target even when a component
    # later adds additional default targets.  The target name is part of the
    # combined-build contract implemented by components/hypervisor.
    add_custom_target(combined_hypervisor_efi
        COMMAND ${CMAKE_COMMAND} --build "${COMBINED_BUILD_ROOT}/hypervisor"
            --config ${COMBINED_CONFIG} --target uefi_hypervisor --parallel
        DEPENDS hypervisor_component
        USES_TERMINAL
        COMMENT "Build the Hypervisor EFI payload with UEFI support sources"
    )
endif()

set(_combined_build_dependencies)
if(TARGET uefi_loader_component)
    list(APPEND _combined_build_dependencies uefi_loader_component)
endif()
if(TARGET hypervisor_component)
    list(APPEND _combined_build_dependencies hypervisor_component)
endif()

if(_combined_build_dependencies)
    add_custom_target(combined_build
        DEPENDS ${_combined_build_dependencies}
        COMMENT "Build all enabled components in isolated directories"
    )
endif()

if(COMBINED_BUILD_COMPONENT_TESTS)
    set(_combined_test_commands)
    if(COMBINED_BUILD_UEFI_LOADER)
        list(APPEND _combined_test_commands
            COMMAND ${CMAKE_CTEST_COMMAND}
                --test-dir "${COMBINED_BUILD_ROOT}/uefi-loader"
                --build-config ${COMBINED_CONFIG}
                --output-on-failure)
    endif()
    if(COMBINED_BUILD_HYPERVISOR_EFI)
        list(APPEND _combined_test_commands
            COMMAND ${CMAKE_CTEST_COMMAND}
                --test-dir "${COMBINED_BUILD_ROOT}/hypervisor"
                --build-config ${COMBINED_CONFIG}
                --output-on-failure)
    endif()

    add_custom_target(combined_test
        ${_combined_test_commands}
        USES_TERMINAL
        COMMENT "Run component tests from their isolated CTest registries"
    )
    if(TARGET combined_build)
        add_dependencies(combined_test combined_build)
    endif()
endif()

# This contract test is intentionally source-only.  It can run on a developer
# machine before a Windows/WDK/OVMF environment is available.
add_test(
    NAME combined_scaffold_contract
    COMMAND ${CMAKE_COMMAND}
        -DROOT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/Verify-CombinedScaffold.cmake"
)

message(STATUS "uefi2hsinv combined build")
message(STATUS "  UEFILoader: ${COMBINED_UEFI_SOURCE_DIR}")
message(STATUS "  Hypervisor: ${COMBINED_HYPERVISOR_SOURCE_DIR}")
message(STATUS "  UEFI support: ${COMBINED_UEFI_SUPPORT_DIR}")
message(STATUS "  Isolated build root: ${COMBINED_BUILD_ROOT}")
message(STATUS "  Configuration: ${COMBINED_CONFIG}")
message(STATUS "  EFI runtime companion build: ${COMBINED_BUILD_EFI_RUNTIME_COMPANION}")
message(STATUS "  EFI runtime service build: ${COMBINED_BUILD_EFI_RUNTIME_SERVICE}")
message(STATUS "  Driver installation/live-load: never performed by this build")
