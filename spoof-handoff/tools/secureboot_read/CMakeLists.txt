cmake_minimum_required(VERSION 3.24)
project(secureboot_read_probe LANGUAGES C)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows x64 MSVC is required.")
endif()
if(NOT REFERENCE_REPO)
    message(FATAL_ERROR "REFERENCE_REPO must name the read-only proven source checkout.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

set(VENDOR "${REFERENCE_REPO}/components/uefi-loader/src/vendor")
if(EXISTS "${CMAKE_BINARY_DIR}/build-id.txt")
    file(READ "${CMAKE_BINARY_DIR}/build-id.txt" HSB_BUILD_ID)
    string(STRIP "${HSB_BUILD_ID}" HSB_BUILD_ID)
else()
    string(RANDOM LENGTH 32 ALPHABET "0123456789ABCDEF" HSB_BUILD_ID)
    file(WRITE "${CMAKE_BINARY_DIR}/build-id.txt" "${HSB_BUILD_ID}\n")
endif()
string(LENGTH "${HSB_BUILD_ID}" HSB_BUILD_ID_LENGTH)
if(NOT HSB_BUILD_ID MATCHES "^[0-9A-F]+$" OR NOT HSB_BUILD_ID_LENGTH EQUAL 32)
    message(FATAL_ERROR "Malformed build identity.")
endif()
configure_file(build_identity.h.in "${CMAKE_BINARY_DIR}/build_identity.h" @ONLY)

add_executable(secureboot_read_collector
    boot_read.c
    probe.c
    "${REFERENCE_REPO}/tools/efi_inventory/efi/sbat.c"
    "${VENDOR}/efi_crt.c")
target_include_directories(secureboot_read_collector PRIVATE
    "${VENDOR}"
    "${CMAKE_BINARY_DIR}")
target_compile_options(secureboot_read_collector PRIVATE /W4 /WX /GS- /Oi- /utf-8)
set_target_properties(secureboot_read_collector PROPERTIES
    SUFFIX ".efi"
    RUNTIME_OUTPUT_NAME "inventory"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/efi"
    MSVC_RUNTIME_LIBRARY "")
target_link_options(secureboot_read_collector PRIVATE
    /SUBSYSTEM:EFI_APPLICATION
    /ENTRY:EfiMain
    /MANIFEST:NO
    /NODEFAULTLIB
    /FIXED:NO
    /INCREMENTAL:NO
    /INCLUDE:g_hypersinv_inventory_sbat
    /INCLUDE:g_inventory_image_relocation_anchor)

include(CTest)
if(BUILD_TESTING)
    add_executable(secureboot_read_tests
        tests/probe_tests.c
        probe.c)
    target_compile_options(secureboot_read_tests PRIVATE /W4 /WX /utf-8)
    add_test(NAME secureboot_variable_read_probe COMMAND secureboot_read_tests)
endif()
