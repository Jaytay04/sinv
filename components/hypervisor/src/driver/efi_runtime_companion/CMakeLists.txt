cmake_minimum_required(VERSION 3.20)

# This is intentionally a standalone WDM target.  The parent project decides
# whether to add this directory; the legacy driver is never linked here.
project(hvr1_efi_runtime_companion LANGUAGES C CXX)

set(HVR1_OVERLAY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../tools/overlay")

if(MSVC)
    # Kernel targets must not inherit user-mode Debug runtime/check flags.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG
                   "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_RELEASE
                   "${CMAKE_C_FLAGS_RELEASE}")
endif()

option(HVR1_BUILD_WDM_DRIVER
       "Build the optional WDM companion .sys (requires the WDK)" ON)
option(HVR1_BUILD_CONTRACT_TESTS
       "Register the WDK-free HVR1 source contract" ON)
option(HVR1_BUILD_SERVICE
       "Build the optional LocalSystem service/CLI" ON)

if(HVR1_BUILD_CONTRACT_TESTS AND WIN32)
    enable_testing()
    find_program(HVR1_POWERSHELL_EXECUTABLE NAMES powershell.exe pwsh.exe)
    if(HVR1_POWERSHELL_EXECUTABLE)
        add_test(
            NAME hvr1_companion_source_contract
            COMMAND "${HVR1_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/test_hvr1_contract.ps1"
        )
        add_test(
            NAME hvr1_service_source_contract
            COMMAND "${HVR1_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/test_hvr1_service_contract.ps1"
        )
    endif()
    add_executable(hvr1_auth_tests
        test_hvr1_auth.cpp
        hvr1_auth.c)
    target_compile_features(hvr1_auth_tests PRIVATE cxx_std_17)
    target_compile_definitions(hvr1_auth_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NTDDI_VERSION=0x0A00000C)
    if(MSVC)
        target_compile_options(hvr1_auth_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(hvr1_auth_tests PRIVATE
            -Wall -Wextra -Werror)
    endif()
    target_link_libraries(hvr1_auth_tests PRIVATE Bcrypt.lib)
    add_test(NAME hvr1_auth_tests COMMAND hvr1_auth_tests)

    add_executable(hvr1_overlay_adapter_tests
        test_hvr1_overlay_adapter.cpp
        hvr1_overlay_adapter.cpp
        "${HVR1_OVERLAY_ROOT}/src/data_transform.cpp"
        "${HVR1_OVERLAY_ROOT}/src/shared_memory_publisher.cpp"
        "${HVR1_OVERLAY_ROOT}/src/shared_memory_reader.cpp")
    target_compile_features(hvr1_overlay_adapter_tests PRIVATE cxx_std_17)
    target_compile_definitions(hvr1_overlay_adapter_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_include_directories(hvr1_overlay_adapter_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../efi
        "${HVR1_OVERLAY_ROOT}/include")
    if(MSVC)
        target_compile_options(hvr1_overlay_adapter_tests PRIVATE
            /W4 /WX /EHsc /permissive- /utf-8)
    endif()
    target_link_libraries(hvr1_overlay_adapter_tests PRIVATE Advapi32.lib)
    add_test(NAME hvr1_overlay_adapter_tests COMMAND hvr1_overlay_adapter_tests)
endif()

if(HVR1_BUILD_SERVICE AND WIN32)
    add_executable(hvr1_runtime_service
        hvr1_service.cpp
        hvr1_auth.c
        hvr1_overlay_adapter.cpp
        "${HVR1_OVERLAY_ROOT}/src/data_transform.cpp"
        "${HVR1_OVERLAY_ROOT}/src/shared_memory_publisher.cpp")
    target_compile_features(hvr1_runtime_service PRIVATE cxx_std_17)
    target_compile_definitions(hvr1_runtime_service PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        NTDDI_VERSION=0x0A00000C
    )
    target_include_directories(hvr1_runtime_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../efi
        "${HVR1_OVERLAY_ROOT}/include"
    )
    if(MSVC)
        target_compile_options(hvr1_runtime_service PRIVATE
            /W4 /WX /EHsc /permissive-)
    else()
        target_compile_options(hvr1_runtime_service PRIVATE
            -Wall -Wextra -Werror)
    endif()
    target_link_libraries(hvr1_runtime_service PRIVATE
        Advapi32.lib
        Bcrypt.lib
        PowrProf.lib
    )
endif()

if(NOT HVR1_BUILD_WDM_DRIVER)
    message(STATUS "HVR1 WDM companion disabled")
    return()
endif()

# Reuse only the repository's WDK discovery module.  The driver target and
# linker entry point below are defined locally because the parent helper is
# KMDF-oriented and uses FxDriverEntry.
list(APPEND CMAKE_MODULE_PATH
     "${CMAKE_CURRENT_LIST_DIR}/../../../cmake")
find_package(WDK QUIET)
if(NOT WDK_FOUND)
    message(STATUS "WDK not found; skipping hvr1_efi_runtime_companion.sys")
    return()
endif()

if(NOT MSVC)
    message(FATAL_ERROR "The HVR1 WDM companion requires the MSVC WDK toolchain")
endif()

enable_language(ASM_MASM)

add_library(hvr1_efi_runtime_companion SHARED
    hvr1_driver.c
    efi_runtime_companion.c
    hvr1_auth.c
    hvr1_vmcall.asm
)

target_compile_definitions(hvr1_efi_runtime_companion PRIVATE
    UNICODE
    _UNICODE
    _AMD64_
    _WIN64
    NTDDI_VERSION=0x0A00000C
    HVR1_KERNEL_MODE=1
)
target_include_directories(hvr1_efi_runtime_companion PRIVATE
    ${WDK_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../efi
)
target_link_directories(hvr1_efi_runtime_companion PRIVATE
    ${WDK_LIBRARY_DIRS}
)
target_compile_options(hvr1_efi_runtime_companion PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:/kernel>"
    "$<$<COMPILE_LANGUAGE:C>:/GS->"
    "$<$<COMPILE_LANGUAGE:C>:/Gz>"
    "$<$<COMPILE_LANGUAGE:C>:/Zp8>"
    "$<$<COMPILE_LANGUAGE:C>:/W4>"
    "$<$<COMPILE_LANGUAGE:C>:/WX>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4100>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4324>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4083>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4005>"
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # clang-cl does not implement several WDK pragma intrinsics and reports
    # the WDK's nested pack pragmas as diagnostics.  MSVC WDK builds do not
    # need these compatibility suppressions.
    target_compile_options(hvr1_efi_runtime_companion PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:/clang:-Wno-ignored-pragma-intrinsic>"
        "$<$<COMPILE_LANGUAGE:C>:/clang:-Wno-pragma-pack>"
    )
endif()
target_link_libraries(hvr1_efi_runtime_companion PRIVATE
    ntoskrnl.lib
    hal.lib
    wmilib.lib
    wdmsec.lib
    cng.lib
    Aux_Klib.lib
    "${WDK_LIBRARY_DIRS}/BufferOverflowK.lib"
)
set_target_properties(hvr1_efi_runtime_companion PROPERTIES
    SUFFIX ".sys"
    PREFIX ""
    IMPORT_PREFIX ""
    LINKER_LANGUAGE C
    LINK_OPTIONS "/DRIVER;/SUBSYSTEM:NATIVE;/NODEFAULTLIB;/MANIFEST:NO;/INCREMENTAL:NO;/ENTRY:DriverEntry"
)
