cmake_minimum_required(VERSION 3.15)
project(hypersinv
    DESCRIPTION "Type 2 Hypervisor for Windows 11 x86_64 — advanced memory debugging engine"
    LANGUAGES C CXX
)

#---------------------------------------------------------------------
# Global compiler settings
#---------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Common MSVC runtime flag (only affects MSVC)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Strip /RTC1 from Debug builds — incompatible with kernel mode
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# Include directory for public headers
set(HYPERSINV_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Enable testing at project scope
enable_testing()

# Module path for custom Find*.cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#---------------------------------------------------------------------
# Subdirectories
#---------------------------------------------------------------------
option(HYPERSINV_BUILD_DRIVER "Build the KMDF kernel driver (requires WDK)" ON)
option(HYPERSINV_BUILD_LOADER "Build the user-mode driver loader" ON)
option(HYPERSINV_BUILD_CORE
    "Build the legacy host hypervisor core and its unit-test graph" ON)
option(HYPERSINV_BUILD_UEFI
    "Build the freestanding persistent UEFI hypervisor payload and probe" ON)
option(HYPERSINV_BUILD_EFI_RUNTIME_COMPANION
    "Build the opt-in WDM companion for the resident EFI hypervisor" OFF)
option(HYPERSINV_BUILD_EFI_RUNTIME_SERVICE
    "Build the opt-in LocalSystem service/CLI for the EFI companion" OFF)
option(HYPERSINV_TEST_EFI_RUNTIME_COMPANION
    "Register WDK-free source contracts for the EFI runtime companion" ON)
set(HYPERSINV_KERNEL_IMAGE "" CACHE FILEPATH
    "Pinned ntoskrnl.exe used to generate the VMX-root kernel profile")
set(HYPERSINV_KERNEL_PDB "" CACHE FILEPATH
    "PDB matching HYPERSINV_KERNEL_IMAGE")
set(HYPERSINV_KERNEL_BUILD "" CACHE STRING
    "Windows kernel build number recorded in the generated profile")
set(HYPERSINV_KERNEL_UBR "" CACHE STRING
    "Windows kernel update build revision recorded in the generated profile")
option(HYPERSINV_REQUIRE_KERNEL_PROFILE
    "Fail configuration unless all pinned kernel profile inputs are supplied" OFF)
set(HYPERSINV_UEFI_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../uefi-loader"
    CACHE PATH
    "UEFILoader component source used by the EFI integration")
set(HYPERSINV_UEFI_SUPPORT_DIR
    "${HYPERSINV_UEFI_SOURCE_DIR}/src/vendor"
    CACHE PATH
    "Directory containing Uefi.h and the freestanding EFI CRT support")

set(HYPERSINV_KERNEL_PROFILE_AVAILABLE OFF)
if(HYPERSINV_KERNEL_IMAGE OR HYPERSINV_KERNEL_PDB OR
        HYPERSINV_KERNEL_BUILD OR HYPERSINV_KERNEL_UBR)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR
            "Kernel profile generation requires a Windows MSVC host")
    endif()
    foreach(_profile_input IN ITEMS
            HYPERSINV_KERNEL_IMAGE HYPERSINV_KERNEL_PDB
            HYPERSINV_KERNEL_BUILD HYPERSINV_KERNEL_UBR)
        if("${${_profile_input}}" STREQUAL "")
            message(FATAL_ERROR
                "All kernel profile inputs are required; missing ${_profile_input}")
        endif()
    endforeach()
    if(NOT EXISTS "${HYPERSINV_KERNEL_IMAGE}" OR
       NOT EXISTS "${HYPERSINV_KERNEL_PDB}")
        message(FATAL_ERROR "Pinned kernel image or PDB does not exist")
    endif()
    add_subdirectory(tools/kernel_profile_generator)
    set(HYPERSINV_KERNEL_PROFILE_AVAILABLE ON)
elseif(HYPERSINV_REQUIRE_KERNEL_PROFILE)
    message(FATAL_ERROR
        "HYPERSINV_REQUIRE_KERNEL_PROFILE needs image, PDB, build, and UBR")
endif()

if(HYPERSINV_BUILD_CORE)
    add_subdirectory(src/hypervisor)
endif()

if(HYPERSINV_BUILD_UEFI)
    add_subdirectory(src/efi)
endif()

if(HYPERSINV_BUILD_EFI_RUNTIME_COMPANION OR
        HYPERSINV_BUILD_EFI_RUNTIME_SERVICE OR
        HYPERSINV_TEST_EFI_RUNTIME_COMPANION)
    set(HVR1_BUILD_WDM_DRIVER
        ${HYPERSINV_BUILD_EFI_RUNTIME_COMPANION})
    set(HVR1_BUILD_CONTRACT_TESTS
        ${HYPERSINV_TEST_EFI_RUNTIME_COMPANION})
    set(HVR1_BUILD_SERVICE
        ${HYPERSINV_BUILD_EFI_RUNTIME_SERVICE})
    add_subdirectory(src/driver/efi_runtime_companion)
endif()

# Driver and loader are conditional — require WDK / source files
if(HYPERSINV_BUILD_DRIVER)
    if(NOT HYPERSINV_BUILD_CORE)
        message(FATAL_ERROR "HYPERSINV_BUILD_DRIVER requires HYPERSINV_BUILD_CORE")
    endif()
    # Kernel driver must not link user-mode CRT libraries (kernel32, user32, etc.)
    set(_saved_c_std_libs "${CMAKE_C_STANDARD_LIBRARIES}")
    set(CMAKE_C_STANDARD_LIBRARIES "")
    add_subdirectory(src/driver)
    set(CMAKE_C_STANDARD_LIBRARIES "${_saved_c_std_libs}")
endif()

if(HYPERSINV_BUILD_LOADER)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/loader/loader_main.c")
        add_subdirectory(src/loader)
    else()
        message(STATUS "loader_main.c not found — skipping hypervisor_loader (will be added in later PR)")
    endif()
endif()

# Driver registration enables ASM_MASM before the test graph evaluates its
# ABI-mirror contract. Keep tests after the conditional production targets.
if(HYPERSINV_BUILD_CORE)
    add_subdirectory(src/tests)

    if(WIN32)
        find_program(HYPERSINV_POWERSHELL_EXECUTABLE NAMES powershell.exe)
        if(HYPERSINV_POWERSHELL_EXECUTABLE)
            add_test(
                NAME host_runner_contracts
                COMMAND "${HYPERSINV_POWERSHELL_EXECUTABLE}"
                    -NoProfile
                    -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/host-runner/tests/Test-HostRunnerContracts.ps1"
            )
        endif()
    endif()
endif()
