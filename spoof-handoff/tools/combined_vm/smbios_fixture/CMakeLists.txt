cmake_minimum_required(VERSION 3.24)

project(combined_vm_smbios2_fixture LANGUAGES C)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The VM fixture EFI prelude requires the x64 MSVC toolchain.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(UEFI_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../components/uefi-loader/src/vendor")
if(NOT EXISTS "${UEFI_SUPPORT_DIR}/Uefi.h" OR NOT EXISTS "${UEFI_SUPPORT_DIR}/efi_crt.c")
    message(FATAL_ERROR "The reference repository UEFI support headers are unavailable.")
endif()

add_library(smbios2_fixture_model STATIC smbios2_fixture.c)
target_include_directories(smbios2_fixture_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(smbios2_fixture_model PRIVATE /W4 /WX /GS- /Oi-)
set_target_properties(smbios2_fixture_model PROPERTIES MSVC_RUNTIME_LIBRARY "")

add_executable(smbios2_fixture_tests smbios2_fixture_tests.c)
target_link_libraries(smbios2_fixture_tests PRIVATE smbios2_fixture_model)
target_include_directories(smbios2_fixture_tests PRIVATE "${UEFI_SUPPORT_DIR}")
target_compile_options(smbios2_fixture_tests PRIVATE /W4 /WX)

add_executable(smbios2_ep_prelude
    efi_prelude.c
    smbios2_fixture.c
    "${UEFI_SUPPORT_DIR}/efi_crt.c")
target_include_directories(smbios2_ep_prelude PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${UEFI_SUPPORT_DIR}")
target_compile_options(smbios2_ep_prelude PRIVATE /W4 /WX /GS- /Oi-)
target_link_options(smbios2_ep_prelude PRIVATE
    /SUBSYSTEM:EFI_APPLICATION
    /ENTRY:EfiMain
    /MANIFEST:NO
    /NODEFAULTLIB
    /FIXED:NO
    /INCREMENTAL:NO)
set_target_properties(smbios2_ep_prelude PROPERTIES
    SUFFIX ".efi"
    RUNTIME_OUTPUT_NAME "smbios2-ep-prelude"
    MSVC_RUNTIME_LIBRARY "")

enable_testing()
add_test(NAME smbios2_fixture_model COMMAND smbios2_fixture_tests)
