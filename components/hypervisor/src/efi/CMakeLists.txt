# Freestanding EFI payloads and portable preflight contracts.

set(HYPERSINV_EFI_CRT "${HYPERSINV_UEFI_SUPPORT_DIR}/efi_crt.c")
set(HYPERSINV_EFI_HEADER "${HYPERSINV_UEFI_SUPPORT_DIR}/Uefi.h")

if(NOT EXISTS "${HYPERSINV_EFI_CRT}" OR NOT EXISTS "${HYPERSINV_EFI_HEADER}")
    message(WARNING
        "UEFI support files were not found under HYPERSINV_UEFI_SUPPORT_DIR="
        "${HYPERSINV_UEFI_SUPPORT_DIR}; EFI images will be skipped")
else()
    if(MSVC)
        enable_language(ASM_MASM)

        add_executable(uefi_hypervisor
            ept_protection.cpp
            efi_hypervisor.c
            efi_hypervisor_math.c
            iommu_isolation.cpp
            iommu_runtime.c
            interrupt_policy.cpp
            runtime_control.cpp
            runtime_dispatch.cpp
            runtime_lifecycle.cpp
            runtime_teardown.cpp
            runtime_transport.cpp
            scanner.cpp
            efi_hypervisor.asm
            "${HYPERSINV_EFI_CRT}")
        target_include_directories(uefi_hypervisor PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
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
    efi_hypervisor_math.c)
target_include_directories(uefi_hypervisor_static_contracts PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uefi_hypervisor_static_contracts PRIVATE c_std_11)
if(MSVC)
    target_compile_options(uefi_hypervisor_static_contracts PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(uefi_hypervisor_static_contracts PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME uefi_hypervisor_static_contracts
    COMMAND uefi_hypervisor_static_contracts)

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
    add_test(NAME uefi_hypervisor_runtime_detach_asm_contracts
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-EfiRuntimeDetachContracts.ps1"
            -EfiSourceRoot "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
