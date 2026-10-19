# Freestanding EFI payloads and portable preflight contracts.

option(HYPERSINV_GPU_VM_SELFTEST
    "Run GPU models and a root WC check in a disposable nested VMware VM only" OFF)
option(HYPERSINV_GPU_DIAGNOSTIC_NATIVE
    "Reserve a dedicated Intel diagnostic adapter until reset (explicit opt-in)" OFF)
option(HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED
    "Operator has physically verified Intel connector and attached diagnostic display" OFF)
if(HYPERSINV_GPU_VM_SELFTEST AND HYPERSINV_GPU_DIAGNOSTIC_NATIVE)
    message(FATAL_ERROR
        "GPU VM self-test owns the CPU WC window; disable native activation in that image")
endif()

set(HYPERSINV_EFI_CRT "${HYPERSINV_UEFI_SUPPORT_DIR}/efi_crt.c")
set(HYPERSINV_EFI_HEADER "${HYPERSINV_UEFI_SUPPORT_DIR}/Uefi.h")

set(HYPERSINV_KERNEL_PROFILE_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(HYPERSINV_KERNEL_PROFILE_AVAILABLE)
    set(HYPERSINV_KERNEL_PROFILE_HEADER
        "${HYPERSINV_KERNEL_PROFILE_GENERATED_DIR}/kernel_profile_gen.h")
    set(HYPERSINV_KERNEL_PROFILE_MANIFEST
        "${HYPERSINV_KERNEL_PROFILE_GENERATED_DIR}/kernel_profile_manifest.json")
    add_custom_command(
        OUTPUT
            "${HYPERSINV_KERNEL_PROFILE_HEADER}"
            "${HYPERSINV_KERNEL_PROFILE_MANIFEST}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${HYPERSINV_KERNEL_PROFILE_GENERATED_DIR}"
        COMMAND $<TARGET_FILE:hypersinv_kernel_profile_generator>
            --image "${HYPERSINV_KERNEL_IMAGE}"
            --pdb "${HYPERSINV_KERNEL_PDB}"
            --build "${HYPERSINV_KERNEL_BUILD}"
            --ubr "${HYPERSINV_KERNEL_UBR}"
            --out-dir "${HYPERSINV_KERNEL_PROFILE_GENERATED_DIR}"
        DEPENDS
            hypersinv_kernel_profile_generator
            "${HYPERSINV_KERNEL_IMAGE}"
            "${HYPERSINV_KERNEL_PDB}"
        VERBATIM
        COMMENT "Generating validated Windows kernel profile")
    add_custom_target(hypersinv_kernel_profile
        DEPENDS
            "${HYPERSINV_KERNEL_PROFILE_HEADER}"
            "${HYPERSINV_KERNEL_PROFILE_MANIFEST}")
endif()

if(NOT EXISTS "${HYPERSINV_EFI_CRT}" OR NOT EXISTS "${HYPERSINV_EFI_HEADER}")
    message(WARNING
        "UEFI support files were not found under HYPERSINV_UEFI_SUPPORT_DIR="
        "${HYPERSINV_UEFI_SUPPORT_DIR}; EFI images will be skipped")
else()
    if(MSVC)
        enable_language(ASM_MASM)

        add_executable(uefi_hypervisor
            ept_protection.cpp
            shadow_ept.cpp
            efi_hypervisor.c
            efi_hypervisor_math.c
            iommu_isolation.cpp
            iommu_runtime.c
            interrupt_policy.cpp
            guest_process.cpp
            guest_pt_walker.cpp
            runtime_analysis.cpp
            runtime_control.cpp
            runtime_dispatch.cpp
            runtime_lifecycle.cpp
            runtime_scanner.cpp
            runtime_shadow.cpp
            runtime_teardown.cpp
            user_vmcall_dispatch.c
            runtime_transport.cpp
            scanner.cpp
            overlay_render.c
            gpu_hal.c
            gpu_intel.c
            gpu_native.c
            gpu_boot.c
            gpu_guest_filter.c
            gpu_wc.c
            efi_hypervisor.asm
            "${HYPERSINV_EFI_CRT}")
        if(HYPERSINV_GPU_VM_SELFTEST)
            target_sources(uefi_hypervisor PRIVATE gpu_vm_selftest.c
                gpu_enforcement_model.c gpu_vm_wc.c)
            target_compile_definitions(uefi_hypervisor PRIVATE
                HYPERSINV_GPU_VM_SELFTEST=1)
        endif()
        if(HYPERSINV_GPU_DIAGNOSTIC_NATIVE)
            target_compile_definitions(uefi_hypervisor PRIVATE
                HYPERSINV_GPU_DIAGNOSTIC_NATIVE=1)
        endif()
        if(HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED)
            target_compile_definitions(uefi_hypervisor PRIVATE
                HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=1)
        endif()
        if(HYPERSINV_KERNEL_PROFILE_AVAILABLE)
            target_sources(uefi_hypervisor PRIVATE
                "${HYPERSINV_KERNEL_PROFILE_HEADER}")
            add_dependencies(uefi_hypervisor hypersinv_kernel_profile)
            target_compile_definitions(uefi_hypervisor PRIVATE
                HYPERSINV_KERNEL_PROFILE_AVAILABLE=1)
        else()
            target_compile_definitions(uefi_hypervisor PRIVATE
                HYPERSINV_KERNEL_PROFILE_AVAILABLE=0)
        endif()
        target_include_directories(uefi_hypervisor PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${HYPERSINV_KERNEL_PROFILE_GENERATED_DIR}"
            "${HYPERSINV_UEFI_SUPPORT_DIR}")
        target_compile_options(uefi_hypervisor PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:/GS->"
            "$<$<COMPILE_LANGUAGE:C>:/Oi->"
            "$<$<COMPILE_LANGUAGE:CXX>:/GS->"
            "$<$<COMPILE_LANGUAGE:CXX>:/Oi->"
            "$<$<COMPILE_LANGUAGE:CXX>:/GR->"
            "$<$<COMPILE_LANGUAGE:CXX>:/EHs-c->")
        set_target_properties(uefi_hypervisor PROPERTIES
            OUTPUT_NAME "uefi_hypervisor"
            SUFFIX ".efi"
            MSVC_RUNTIME_LIBRARY "")
        target_link_options(uefi_hypervisor PRIVATE
            /SUBSYSTEM:EFI_APPLICATION
            /ENTRY:EfiMain
            /FIXED:NO
            /MANIFEST:NO
            /NODEFAULTLIB)
        # Phase 6: Strip debug info and remove identifiable PE metadata
        target_link_options(uefi_hypervisor PRIVATE
            /DEBUG:NONE
            /OPT:REF
            /OPT:ICF
            /DYNAMICBASE:NO)
        # Retain the diagnostic entry point so the existing resident-image
        # audit covers its code and ROM even before a caller is integrated.
        target_link_options(uefi_hypervisor PRIVATE /INCLUDE:HvOverlayRender)
        # Keep the HAL and cleanup entries in the resident-image audit even
        # when native activation is disabled in the default build.
        target_link_options(uefi_hypervisor PRIVATE
            /INCLUDE:HvGpuInitialize /INCLUDE:HvGpuRender
            /INCLUDE:HvGpuBeforeGuestResume /INCLUDE:HvGpuShutdown
            /INCLUDE:HvGpuIntelParsePci)
        target_link_options(uefi_hypervisor PRIVATE /INCLUDE:HvGpuNativeResident)
        find_program(HYPERSINV_DUMPBIN_EXECUTABLE NAMES dumpbin.exe)
        find_program(HYPERSINV_POWERSHELL_EXECUTABLE
            NAMES powershell.exe pwsh.exe)
        if(HYPERSINV_DUMPBIN_EXECUTABLE AND
           HYPERSINV_POWERSHELL_EXECUTABLE)
            add_custom_command(TARGET uefi_hypervisor POST_BUILD
                COMMAND "${HYPERSINV_POWERSHELL_EXECUTABLE}"
                    -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-EfiRuntimeScannerImage.ps1"
                    -ImagePath "$<TARGET_FILE:uefi_hypervisor>"
                    -DumpbinPath "${HYPERSINV_DUMPBIN_EXECUTABLE}"
                    -MaximumPages 16
                VERBATIM)
        endif()

        add_executable(uefi_hypervisor_probe
            uefi_hypervisor_probe.c
            uefi_hypervisor_probe.asm
            "${HYPERSINV_EFI_CRT}")
        target_include_directories(uefi_hypervisor_probe PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${HYPERSINV_UEFI_SUPPORT_DIR}")
        target_compile_options(uefi_hypervisor_probe PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:/GS->"
            "$<$<COMPILE_LANGUAGE:C>:/Oi->")
        set_target_properties(uefi_hypervisor_probe PROPERTIES
            OUTPUT_NAME "uefi_hypervisor_probe"
            SUFFIX ".efi"
            MSVC_RUNTIME_LIBRARY "")
        target_link_options(uefi_hypervisor_probe PRIVATE
            /SUBSYSTEM:EFI_APPLICATION
            /ENTRY:EfiMain
            /FIXED:NO
            /MANIFEST:NO
            /NODEFAULTLIB)

        add_executable(uefi_hypervisor_post_ebs_probe
            uefi_hypervisor_post_ebs_probe.c
            uefi_hypervisor_post_ebs_probe.asm
            "${HYPERSINV_EFI_CRT}")
        target_include_directories(uefi_hypervisor_post_ebs_probe PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${HYPERSINV_UEFI_SUPPORT_DIR}")
        target_compile_options(uefi_hypervisor_post_ebs_probe PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:/GS->"
            "$<$<COMPILE_LANGUAGE:C>:/Oi->")
        set_target_properties(uefi_hypervisor_post_ebs_probe PROPERTIES
            OUTPUT_NAME "uefi_hypervisor_post_ebs_probe"
            SUFFIX ".efi"
            MSVC_RUNTIME_LIBRARY "")
        target_link_options(uefi_hypervisor_post_ebs_probe PRIVATE
            /SUBSYSTEM:EFI_APPLICATION
            /ENTRY:EfiMain
            /FIXED:NO
            /MANIFEST:NO
            /NODEFAULTLIB)

        # Keep the internal target name short.  The ExternalProject superbuild
        # has a deep binary directory, and MSVC still rejects object paths that
        # cross MAX_PATH even when the source/output artifact names are valid.
        add_executable(uefi_hv_post_ebs_interrupt_probe
            uefi_hypervisor_post_ebs_interrupt_probe.c
            uefi_hypervisor_post_ebs_interrupt_probe.asm
            "${HYPERSINV_EFI_CRT}")
        target_include_directories(uefi_hv_post_ebs_interrupt_probe PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${HYPERSINV_UEFI_SUPPORT_DIR}")
        target_compile_options(uefi_hv_post_ebs_interrupt_probe PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:/GS->"
            "$<$<COMPILE_LANGUAGE:C>:/Oi->")
        set_target_properties(uefi_hv_post_ebs_interrupt_probe PROPERTIES
            OUTPUT_NAME "uefi_hypervisor_post_ebs_interrupt_probe"
            SUFFIX ".efi"
            MSVC_RUNTIME_LIBRARY "")
        target_link_options(uefi_hv_post_ebs_interrupt_probe PRIVATE
            /SUBSYSTEM:EFI_APPLICATION
            /ENTRY:EfiMain
            /FIXED:NO
            /MANIFEST:NO
            /NODEFAULTLIB)
        add_custom_target(uefi_hypervisor_post_ebs_interrupt_probe
            DEPENDS uefi_hv_post_ebs_interrupt_probe)
    else()
        message(STATUS
            "MSVC/MASM is required for uefi_hypervisor.efi; EFI images skipped")
    endif()
endif()

# Host-side tests exercise the same control math and explicit C/MASM layout
# contract without executing privileged VMX instructions.
add_executable(uefi_hypervisor_static_contracts
    tests/test_efi_hypervisor_math.c
    efi_hypervisor_math.c
    tests/gpu_hal_test.c
    gpu_hal.c
    gpu_intel.c
    gpu_vm_selftest.c
    gpu_enforcement_model.c
    iommu_isolation.cpp
    iommu_runtime.c
    gpu_guest_filter.c
    gpu_wc.c
    tests/gpu_guest_filter_test.c
    tests/gpu_wc_test.c
    overlay_render.c)
target_include_directories(uefi_hypervisor_static_contracts PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_static_contracts PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_static_contracts PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(uefi_hypervisor_static_contracts PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_static_contracts
    COMMAND uefi_hypervisor_static_contracts)
# Reuse the existing host executable; no new CMake targets or EFI images.
add_test(NAME uefi_hypervisor_gpu_hal_tests
    COMMAND uefi_hypervisor_static_contracts --gpu-hal)
add_test(NAME uefi_hypervisor_gpu_vm_model_tests
    COMMAND uefi_hypervisor_static_contracts --gpu-vm-model)
add_test(NAME uefi_hypervisor_gpu_guest_filter_tests
    COMMAND uefi_hypervisor_static_contracts --gpu-filter)
add_test(NAME uefi_hypervisor_gpu_wc_tests
    COMMAND uefi_hypervisor_static_contracts --gpu-wc)
add_test(NAME uefi_hypervisor_gpu_enforcement_tests
    COMMAND uefi_hypervisor_static_contracts --gpu-enforcement)

add_executable(uefi_hypervisor_scanner_tests
    tests/test_scanner.cpp
    scanner.cpp)
target_include_directories(uefi_hypervisor_scanner_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_scanner_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_scanner_tests PRIVATE /W4 /WX)
else()
    target_compile_options(uefi_hypervisor_scanner_tests PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_scanner_tests
    COMMAND uefi_hypervisor_scanner_tests)

# Diagnostic renderer tests use only caller-owned RAM and execute no VMX code.
add_executable(uefi_hypervisor_overlay_render_tests
    tests/overlay_render_test.c
    overlay_render.c)
target_include_directories(uefi_hypervisor_overlay_render_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_overlay_render_tests PRIVATE c_std_11)
if(MSVC)
    target_compile_options(uefi_hypervisor_overlay_render_tests PRIVATE
        /W4 /WX /GS- /Oi-)
else()
    target_compile_options(uefi_hypervisor_overlay_render_tests PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_overlay_render_tests
    COMMAND uefi_hypervisor_overlay_render_tests)

add_executable(uefi_hypervisor_runtime_scanner_tests
    tests/test_runtime_scanner.cpp
    runtime_scanner.cpp
    scanner.cpp)
target_include_directories(uefi_hypervisor_runtime_scanner_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_runtime_scanner_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_runtime_scanner_tests PRIVATE
        /W4 /WX /GS- /GR- /EHs-c-)
else()
    target_compile_options(uefi_hypervisor_runtime_scanner_tests PRIVATE
        -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME uefi_hypervisor_runtime_scanner_tests
    COMMAND uefi_hypervisor_runtime_scanner_tests)

add_executable(uefi_hypervisor_guest_introspection_tests
    tests/test_guest_introspection.cpp
    guest_process.cpp
    guest_pt_walker.cpp
    scanner.cpp)
target_include_directories(uefi_hypervisor_guest_introspection_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_guest_introspection_tests PRIVATE
    cxx_std_17)
target_compile_definitions(uefi_hypervisor_guest_introspection_tests PRIVATE
    HYPERSINV_KERNEL_PROFILE_AVAILABLE=0)
if(MSVC)
    target_compile_options(uefi_hypervisor_guest_introspection_tests PRIVATE
        /W4 /WX /GS- /GR- /EHs-c-)
else()
    target_compile_options(uefi_hypervisor_guest_introspection_tests PRIVATE
        -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME uefi_hypervisor_guest_introspection_tests
    COMMAND uefi_hypervisor_guest_introspection_tests)

add_executable(uefi_hypervisor_runtime_analysis_tests
    tests/test_runtime_analysis.cpp
    runtime_analysis.cpp
    guest_pt_walker.cpp
    scanner.cpp)
target_include_directories(uefi_hypervisor_runtime_analysis_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_runtime_analysis_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_runtime_analysis_tests PRIVATE
        /W4 /WX /GS- /GR- /EHs-c-)
else()
    target_compile_options(uefi_hypervisor_runtime_analysis_tests PRIVATE
        -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME uefi_hypervisor_runtime_analysis_tests
    COMMAND uefi_hypervisor_runtime_analysis_tests)

add_executable(uefi_hypervisor_runtime_control_tests
    tests/test_runtime_control.cpp
    runtime_control.cpp)
target_include_directories(uefi_hypervisor_runtime_control_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_runtime_control_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_runtime_control_tests PRIVATE
        /W4 /WX)
else()
    target_compile_options(uefi_hypervisor_runtime_control_tests PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_runtime_control_tests
    COMMAND uefi_hypervisor_runtime_control_tests)

add_executable(uefi_hypervisor_runtime_lifecycle_tests
    tests/test_runtime_lifecycle.cpp
    runtime_lifecycle.cpp)
target_include_directories(uefi_hypervisor_runtime_lifecycle_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_runtime_lifecycle_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_runtime_lifecycle_tests PRIVATE
        /W4 /WX)
else()
    target_compile_options(uefi_hypervisor_runtime_lifecycle_tests PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_runtime_lifecycle_tests
    COMMAND uefi_hypervisor_runtime_lifecycle_tests)

add_executable(uefi_hypervisor_ept_protection_tests
    tests/test_ept_protection.cpp
    ept_protection.cpp)
target_include_directories(uefi_hypervisor_ept_protection_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_ept_protection_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_ept_protection_tests PRIVATE
        /W4 /WX)
else()
    target_compile_options(uefi_hypervisor_ept_protection_tests PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_ept_protection_tests
    COMMAND uefi_hypervisor_ept_protection_tests)

add_executable(uefi_hypervisor_shadow_ept_tests
    tests/test_shadow_ept.cpp
    shadow_ept.cpp)
target_include_directories(uefi_hypervisor_shadow_ept_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_shadow_ept_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_shadow_ept_tests PRIVATE
        /W4 /WX /GS- /GR- /EHs-c-)
else()
    target_compile_options(uefi_hypervisor_shadow_ept_tests PRIVATE
        -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME uefi_hypervisor_shadow_ept_tests
    COMMAND uefi_hypervisor_shadow_ept_tests)

add_executable(uefi_hypervisor_interrupt_policy_tests
    tests/test_interrupt_policy.cpp
    interrupt_policy.cpp)
target_include_directories(uefi_hypervisor_interrupt_policy_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_interrupt_policy_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_interrupt_policy_tests PRIVATE
        /W4 /WX)
else()
    target_compile_options(uefi_hypervisor_interrupt_policy_tests PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_interrupt_policy_tests
    COMMAND uefi_hypervisor_interrupt_policy_tests)

add_executable(uefi_hypervisor_runtime_dispatch_tests
    tests/test_runtime_dispatch.cpp
    runtime_dispatch.cpp
    runtime_control.cpp
    runtime_lifecycle.cpp)
target_include_directories(uefi_hypervisor_runtime_dispatch_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_runtime_dispatch_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_runtime_dispatch_tests PRIVATE
        /W4 /WX)
else()
    target_compile_options(uefi_hypervisor_runtime_dispatch_tests PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_runtime_dispatch_tests
    COMMAND uefi_hypervisor_runtime_dispatch_tests)

add_executable(uefi_hypervisor_runtime_teardown_tests
    tests/test_runtime_teardown.cpp
    runtime_teardown.cpp)
target_include_directories(uefi_hypervisor_runtime_teardown_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_runtime_teardown_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_runtime_teardown_tests PRIVATE
        /W4 /WX /GS- /GR- /EHs-c-)
else()
    target_compile_options(uefi_hypervisor_runtime_teardown_tests PRIVATE
        -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME uefi_hypervisor_runtime_teardown_tests
    COMMAND uefi_hypervisor_runtime_teardown_tests)

add_executable(uefi_hypervisor_iommu_isolation_tests
    tests/test_iommu_isolation.cpp
    iommu_isolation.cpp)
target_include_directories(uefi_hypervisor_iommu_isolation_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_iommu_isolation_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_iommu_isolation_tests PRIVATE
        /W4 /WX /GS- /GR- /EHs-c-)
else()
    target_compile_options(uefi_hypervisor_iommu_isolation_tests PRIVATE
        -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME uefi_hypervisor_iommu_isolation_tests
    COMMAND uefi_hypervisor_iommu_isolation_tests)

add_executable(uefi_hypervisor_iommu_runtime_tests
    tests/test_iommu_runtime.c
    iommu_runtime.c)
target_include_directories(uefi_hypervisor_iommu_runtime_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_iommu_runtime_tests PRIVATE c_std_11)
if(MSVC)
    target_compile_options(uefi_hypervisor_iommu_runtime_tests PRIVATE
        /W4 /WX /GS- /Oi-)
else()
    target_compile_options(uefi_hypervisor_iommu_runtime_tests PRIVATE
        -Wall -Wextra -Werror -ffreestanding)
endif()
add_test(NAME uefi_hypervisor_iommu_runtime_tests
    COMMAND uefi_hypervisor_iommu_runtime_tests)

add_executable(uefi_hypervisor_runtime_transport_tests
    tests/test_runtime_transport.cpp
    runtime_transport.cpp
    runtime_dispatch.cpp
    runtime_control.cpp
    runtime_lifecycle.cpp)
target_include_directories(uefi_hypervisor_runtime_transport_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_runtime_transport_tests PRIVATE
    cxx_std_17)
if(MSVC)
    target_compile_options(uefi_hypervisor_runtime_transport_tests PRIVATE
        /W4 /WX /GS- /GR- /EHs-c-)
else()
    target_compile_options(uefi_hypervisor_runtime_transport_tests PRIVATE
        -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME uefi_hypervisor_runtime_transport_tests
    COMMAND uefi_hypervisor_runtime_transport_tests)

if(WIN32)
    add_test(NAME uefi_hypervisor_resident_rva_table_tests
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-EfiResidentRvaTables.ps1")
    add_test(NAME uefi_hypervisor_gpu_preflight_tests
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-GpuDiagnosticPreflight.ps1"
            -ToolPath "${CMAKE_CURRENT_SOURCE_DIR}/../../setup/Get-GpuDiagnosticPreflight.ps1")
    add_test(NAME uefi_hypervisor_vmexit_asm_contracts
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-EfiVmExitContracts.ps1"
            -EfiSourceRoot "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME uefi_hypervisor_runtime_detach_asm_contracts
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-EfiRuntimeDetachContracts.ps1"
            -EfiSourceRoot "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
