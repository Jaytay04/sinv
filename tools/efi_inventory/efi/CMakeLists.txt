cmake_minimum_required(VERSION 3.24)

project(hypersinv_efi_inventory_efi
    VERSION 1.0.0
    DESCRIPTION "Read-only UEFI variable inventory application"
    LANGUAGES C
)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR
        "The EFI inventory application requires the Windows MSVC toolchain")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The EFI inventory application requires an x64 target")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(UEFI_SUPPORT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/uefi-loader/src/vendor")

add_executable(efi_inventory_collector
    collector.c
    sbat.c
    "${UEFI_SUPPORT_DIR}/efi_crt.c"
)
target_include_directories(efi_inventory_collector PRIVATE
    "${UEFI_SUPPORT_DIR}")
target_compile_options(efi_inventory_collector PRIVATE /W4 /WX /GS- /Oi-)
set_target_properties(efi_inventory_collector PROPERTIES
    SUFFIX ".efi"
    RUNTIME_OUTPUT_NAME "inventory"
    MSVC_RUNTIME_LIBRARY ""
)
target_link_options(efi_inventory_collector PRIVATE
    /SUBSYSTEM:EFI_APPLICATION
    /ENTRY:EfiMain
    /MANIFEST:NO
    /NODEFAULTLIB
    /FIXED:NO
    /INCREMENTAL:NO
    /INCLUDE:g_hypersinv_inventory_sbat
    /INCLUDE:g_inventory_image_relocation_anchor
)
