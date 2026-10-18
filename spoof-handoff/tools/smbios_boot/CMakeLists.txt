cmake_minimum_required(VERSION 3.24)
project(smbios_boot_probe LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows x64 MSVC is required.")
endif()
if(NOT REFERENCE_REPO)
    message(FATAL_ERROR "REFERENCE_REPO must name the read-only proven source checkout.")
endif()
option(HYPERSINV_VM_ONLY_SMBIOS_PROFILE
    "Build the collector and readback tool with a disposable VM-specific SMBIOS fixture." OFF)
set(HYPERSINV_VM_SMBIOS_VALUES_HEADER "" CACHE FILEPATH
    "Generated VM-only smbios_values.h; required when HYPERSINV_VM_ONLY_SMBIOS_PROFILE is enabled.")
if(HYPERSINV_VM_ONLY_SMBIOS_PROFILE)
    if(NOT HYPERSINV_VM_SMBIOS_VALUES_HEADER OR
       NOT EXISTS "${HYPERSINV_VM_SMBIOS_VALUES_HEADER}")
        message(FATAL_ERROR
            "VM-only SMBIOS profile requires an existing HYPERSINV_VM_SMBIOS_VALUES_HEADER.")
    endif()
    file(REAL_PATH "${HYPERSINV_VM_SMBIOS_VALUES_HEADER}" HYPERSINV_VM_SMBIOS_VALUES_HEADER_REAL)
    file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/smbios_values.h" HYPERSINV_HOST_SMBIOS_VALUES_HEADER_REAL)
    if(HYPERSINV_VM_SMBIOS_VALUES_HEADER_REAL STREQUAL HYPERSINV_HOST_SMBIOS_VALUES_HEADER_REAL)
        message(FATAL_ERROR
            "The VM-only profile must use a generated guest-capture header, not the checked-in host values.")
    endif()
    file(READ "${HYPERSINV_VM_SMBIOS_VALUES_HEADER_REAL}" HYPERSINV_VM_SMBIOS_VALUES_HEADER_CONTENTS)
    if(NOT HYPERSINV_VM_SMBIOS_VALUES_HEADER_CONTENTS MATCHES
       "#ifndef HYPERSINV_VM_ONLY_SMBIOS_PROFILE")
        message(FATAL_ERROR
            "The generated values header must refuse inclusion outside the VM-only profile.")
    endif()
    file(TO_CMAKE_PATH "${HYPERSINV_VM_SMBIOS_VALUES_HEADER_REAL}" HYPERSINV_VM_SMBIOS_VALUES_HEADER_INCLUDE)
elseif(HYPERSINV_VM_SMBIOS_VALUES_HEADER)
    message(FATAL_ERROR
        "HYPERSINV_VM_SMBIOS_VALUES_HEADER is only allowed with HYPERSINV_VM_ONLY_SMBIOS_PROFILE=ON.")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
set(VENDOR "${REFERENCE_REPO}/components/uefi-loader/src/vendor")
if(EXISTS "${CMAKE_BINARY_DIR}/build-id.txt")
    file(READ "${CMAKE_BINARY_DIR}/build-id.txt" SM_BUILD_ID)
    string(STRIP "${SM_BUILD_ID}" SM_BUILD_ID)
else()
    string(RANDOM LENGTH 32 ALPHABET "0123456789ABCDEF" SM_BUILD_ID)
    file(WRITE "${CMAKE_BINARY_DIR}/build-id.txt" "${SM_BUILD_ID}\n")
endif()
string(LENGTH "${SM_BUILD_ID}" SM_BUILD_ID_LENGTH)
if(NOT SM_BUILD_ID MATCHES "^[0-9A-F]+$" OR NOT SM_BUILD_ID_LENGTH EQUAL 32)
    message(FATAL_ERROR "Malformed build identity.")
endif()
configure_file(build_identity.h.in "${CMAKE_BINARY_DIR}/build_identity.h" @ONLY)
add_library(smbios_table STATIC smbios_table.c)
target_compile_options(smbios_table PRIVATE /W4 /WX /utf-8)
target_include_directories(smbios_table PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(smbios_windows_readback windows_readback.cpp)
target_link_libraries(smbios_windows_readback PRIVATE smbios_table wbemuuid ole32 oleaut32 advapi32)
target_compile_options(smbios_windows_readback PRIVATE /W4 /WX /utf-8)
target_compile_definitions(smbios_windows_readback PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(HYPERSINV_VM_ONLY_SMBIOS_PROFILE)
    target_include_directories(smbios_windows_readback PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(smbios_windows_readback PRIVATE
        HYPERSINV_VM_ONLY_SMBIOS_PROFILE=1
        "HYPERSINV_SMBIOS_VALUES_HEADER=\"${HYPERSINV_VM_SMBIOS_VALUES_HEADER_INCLUDE}\"")
endif()

add_executable(smbios_serial_collector boot_variant.c efi_smbios.c smbios_table.c
    "${REFERENCE_REPO}/tools/efi_inventory/efi/sbat.c" "${VENDOR}/efi_crt.c")
target_include_directories(smbios_serial_collector PRIVATE "${VENDOR}" "${CMAKE_BINARY_DIR}")
target_compile_options(smbios_serial_collector PRIVATE /W4 /WX /GS- /Oi- /utf-8)
if(HYPERSINV_VM_ONLY_SMBIOS_PROFILE)
    target_include_directories(smbios_serial_collector PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(smbios_serial_collector PRIVATE
        HYPERSINV_VM_ONLY_SMBIOS_PROFILE=1
        "HYPERSINV_SMBIOS_VALUES_HEADER=\"${HYPERSINV_VM_SMBIOS_VALUES_HEADER_INCLUDE}\"")
endif()
set_target_properties(smbios_serial_collector PROPERTIES SUFFIX ".efi"
    RUNTIME_OUTPUT_NAME "inventory" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/efi"
    MSVC_RUNTIME_LIBRARY "")
target_link_options(smbios_serial_collector PRIVATE /SUBSYSTEM:EFI_APPLICATION /ENTRY:EfiMain
    /MANIFEST:NO /NODEFAULTLIB /FIXED:NO /INCREMENTAL:NO
    /INCLUDE:g_hypersinv_inventory_sbat /INCLUDE:g_inventory_image_relocation_anchor)

include(CTest)
if(BUILD_TESTING)
    add_executable(smbios_table_tests tests/smbios_table_tests.c)
    target_link_libraries(smbios_table_tests PRIVATE smbios_table)
    target_compile_options(smbios_table_tests PRIVATE /W4 /WX /utf-8)
    add_test(NAME smbios_table_substitute_restore COMMAND smbios_table_tests)
    add_executable(efi_smbios_tests tests/efi_smbios_tests.c efi_smbios.c)
    target_include_directories(efi_smbios_tests PRIVATE "${VENDOR}")
    target_link_libraries(efi_smbios_tests PRIVATE smbios_table)
    target_compile_options(efi_smbios_tests PRIVATE /W4 /WX /utf-8)
    add_test(NAME efi_smbios_publication_transaction COMMAND efi_smbios_tests)
endif()
