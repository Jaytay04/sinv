# Source-only contract test for the combined superbuild.
# This intentionally does not configure either component; it is safe to run
# before a Windows/WDK/OVMF environment is available.

if(NOT DEFINED ROOT_SOURCE_DIR OR NOT EXISTS "${ROOT_SOURCE_DIR}")
    message(FATAL_ERROR "ROOT_SOURCE_DIR must point at the combined repository")
endif()

set(_root_cmake "${ROOT_SOURCE_DIR}/CMakeLists.txt")
set(_readme "${ROOT_SOURCE_DIR}/README.md")
set(_build_script "${ROOT_SOURCE_DIR}/tools/Build-Combined.ps1")
foreach(_path IN ITEMS _root_cmake _readme _build_script)
    if(NOT EXISTS "${${_path}}")
        message(FATAL_ERROR "Missing combined scaffold file: ${${_path}}")
    endif()
endforeach()

file(READ "${_root_cmake}" _root_text)
file(READ "${_readme}" _readme_text)
file(READ "${_build_script}" _script_text)

function(assert_contains text needle description)
    string(FIND "${text}" "${needle}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Contract failed: ${description} (missing '${needle}')")
    endif()
endfunction()

assert_contains("${_root_text}" "include(ExternalProject)" "root uses an isolated superbuild")
assert_contains("${_root_text}" "BINARY_DIR" "components use isolated build directories")
assert_contains("${_root_text}" "uefi-loader" "UEFILoader has an isolated build directory")
assert_contains("${_root_text}" "hypervisor" "Hypervisor has an isolated build directory")
assert_contains("${_root_text}" "HYPERSINV_BUILD_DRIVER:BOOL=OFF" "driver is disabled by default")
assert_contains("${_root_text}" "HYPERSINV_BUILD_UEFI:BOOL=ON" "EFI payload is enabled by the component option")
assert_contains("${_root_text}" "HYPERSINV_UEFI_SUPPORT_DIR:PATH" "UEFI support path is explicit")
assert_contains("${_root_text}" "HYPERSINV_KERNEL_IMAGE:FILEPATH" "kernel image pin is forwarded")
assert_contains("${_root_text}" "HYPERSINV_KERNEL_PDB:FILEPATH" "kernel PDB pin is forwarded")
assert_contains("${_root_text}" "HYPERSINV_REQUIRE_KERNEL_PROFILE:BOOL" "kernel profile can be required")
assert_contains("${_root_text}" "IB_PAYLOAD_REPLAY_INDEPENDENT:BOOL=ON" "combined loader permits the EFI payload on stock measured firmware")
assert_contains("${_root_text}" "--target uefi_hypervisor" "EFI target is an explicit build contract")
assert_contains("${_root_text}" "COMBINED_BUILD_COMPONENT_TESTS" "component test orchestration is configurable")

assert_contains("${_readme_text}" "UEFILoader" "README describes the loader")
assert_contains("${_readme_text}" "payload.efi" "README describes the payload handoff")
assert_contains("${_readme_text}" "ExitBootServices" "README states the boot-services boundary")
assert_contains("${_readme_text}" "CPUID" "README documents the smoke interface")
assert_contains("${_readme_text}" "VM-only" "README states VM-only safety")
assert_contains("${_readme_text}" "live-load" "README rejects driver live-load")
assert_contains("${_readme_text}" "Build-pinned Windows process enumeration" "README documents process introspection")

assert_contains("${_script_text}" "COMBINED_BUILD_HYPERVISOR_EFI" "build script enables the EFI target")
assert_contains("${_script_text}" "combined_test" "build script exposes component tests")

message(STATUS "PASS: combined superbuild source contract")
