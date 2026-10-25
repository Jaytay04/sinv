# UEFILoader — Insecure Boot Toolkit
#
# Milestone M1 (P2): shared portable tcglog library + host test harness +
# dual-target compile proof. Targets landing in later milestones:
#
#   src/tcgdump/ib-tcg-dump Windows TBS capture + probe CLI
#   src/insecure_boot/  insecure-boot EFI loader (loader.efi)
#   src/install/ib-install  Windows staging/signing installer
#   src/audit/          defensive audit detectors (host sources)
#   src/fixtures            synthetic logs + host test harness (gen in M5)
#
# Windows tools are MSVC /W4, zero runtime deps beyond CRT + TBS + firmware
# environment APIs. loader.efi uses /SUBSYSTEM:EFI_APPLICATION with the port's
# own ib_uefi.h typedef surface rather than the vendored UEFI/TCG2 headers.

cmake_minimum_required(VERSION 3.20)

project(UEFILoader
    VERSION 0.1.0
    DESCRIPTION "Insecure Boot Toolkit — PCR-replay loader and defensive audit"
    LANGUAGES C)

# --- Compiler warnings -------------------------------------------------------
# Match the /W4 contract from the design; Windows tools and the EFI app alike.
if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:/W4>")
endif()

# --- C standard --------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Static CRT option --------------------------------------------------------
# OFF (default): dynamic CRT (/MD, /MDd) — requires the VC++ redistributable.
# ON:            static CRT (/MT, /MTd)  — zero redist dependency; the binaries
#                carry the runtime and run on a clean Windows guest.
option(IB_STATIC_RUNTIME
    "Link Windows host targets against the static CRT (/MT, /MTd)"
    OFF)
option(IB_PAYLOAD_REPLAY_INDEPENDENT
    "Launch payload.efi even when PCR replay cannot run"
    OFF)
option(IB_FIRMWARE_STORAGE_PROBE
    "Trace bounded read-only boot-volume probes around payload activation"
    OFF)
option(IB_BUILD_BOOT_CHAIN_LOADER
    "Build the isolated restore-first Windows boot-chain EFI image"
    OFF)

option(IB_QEMU_DEBUG_PORTS
    "Emit loader diagnostics to QEMU debug/serial I/O ports (VM-only)"
    OFF)
option(IB_BUILD_BOOT_CHAIN_VM_FIXTURES
    "Build VM-only restore and firmware-handoff fixtures without TPM or payload code"
    OFF)

enable_testing()

# --- Shared tcglog library (host build) -------------------------------------
# Freestanding; compiles unmodified for host tools and the UEFI target.
# tcglog_ib.c (the tcglog.ib pre-boot container) shares the same portability
# contract so loader.efi can read the container ib-tcg-dump writes.
add_library(tcglog STATIC
    src/common/tcglog.c
    src/common/tcglog_ib.c
)
target_include_directories(tcglog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# --- Portable empty-bank gate (host + UEFI) ---------------------------------
# Pure verdict logic shared by the host test harness and loader.efi. Kept as a
# separate library from tcglog: it has no allocation hooks and no dependency on
# the parser, so it compiles identically for both targets.
add_library(pcr_gate STATIC
    src/common/pcr_gate.c
)

# --- Portable SHA-256 -------------------------------------------------------
add_library(sha256 STATIC
    src/common/sha256.c
)
target_include_directories(sha256 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
target_include_directories(pcr_gate PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# --- Host test harness -------------------------------------------------------
add_executable(tcglog_test
    src/fixtures/tests/tcglog_test.c
)
target_link_libraries(tcglog_test PRIVATE tcglog)
add_test(NAME tcglog_test COMMAND tcglog_test)

add_executable(tcglog_ib_test
    src/fixtures/tests/tcglog_ib_test.c
)
target_link_libraries(tcglog_ib_test PRIVATE tcglog)
add_test(NAME tcglog_ib_test COMMAND tcglog_ib_test)

add_executable(pcr_gate_test
    src/fixtures/tests/pcr_gate_test.c
)
target_link_libraries(pcr_gate_test PRIVATE pcr_gate)
add_test(NAME pcr_gate_test COMMAND pcr_gate_test)

add_executable(sha256_test
    src/fixtures/tests/sha256_test.c
)
target_link_libraries(sha256_test PRIVATE sha256)
add_test(NAME sha256_test COMMAND sha256_test)

add_executable(install_sign_test
    src/fixtures/tests/install_sign_test.c
    src/install/authenticode.c
    src/install/sign.c
)
target_include_directories(install_sign_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/install
)
target_link_libraries(install_sign_test PRIVATE Crypt32 Wintrust)
add_test(NAME install_sign_test COMMAND install_sign_test)

add_executable(install_authenticode_test
    src/fixtures/tests/install_authenticode_test.c
    src/install/authenticode.c
)
target_include_directories(install_authenticode_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/install
)
target_link_libraries(install_authenticode_test PRIVATE Crypt32 Wintrust)

add_executable(install_stage_test
    src/fixtures/tests/install_stage_test.c
    src/install/stage.c
)
target_include_directories(install_stage_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/install
)
target_link_libraries(install_stage_test PRIVATE sha256 Advapi32)
add_test(NAME install_stage_test COMMAND install_stage_test)

add_executable(install_oneshot_test
    src/fixtures/tests/install_oneshot_test.c
    src/install/oneshot.c
)
target_include_directories(install_oneshot_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/install
)
target_link_libraries(install_oneshot_test PRIVATE sha256)
add_test(NAME install_oneshot_test COMMAND install_oneshot_test)

add_executable(install_bootnext_test
    src/fixtures/tests/install_bootnext_test.c
    src/install/bootnext.c
)
target_include_directories(install_bootnext_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/install
)
target_link_libraries(install_bootnext_test PRIVATE Advapi32)
add_test(NAME install_bootnext_test COMMAND install_bootnext_test)

# --- P8 defensive-audit detector tests -------------------------------------
add_executable(audit_vars_test
    src/fixtures/tests/audit_vars_test.c
    src/audit/vars.c
)
target_include_directories(audit_vars_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
target_link_libraries(audit_vars_test PRIVATE sha256)
add_test(NAME audit_vars_test COMMAND audit_vars_test)

add_executable(audit_tcg_version_test
    src/fixtures/tests/audit_tcg_version_test.c
    src/audit/tcg_version.c
)
target_include_directories(audit_tcg_version_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
)
add_test(NAME audit_tcg_version_test COMMAND audit_tcg_version_test)

add_executable(audit_bootmgr_test
    src/fixtures/tests/audit_bootmgr_test.c
    src/audit/bootmgr.c
    src/audit/vars.c
)
target_include_directories(audit_bootmgr_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
target_link_libraries(audit_bootmgr_test PRIVATE sha256)
add_test(NAME audit_bootmgr_test COMMAND audit_bootmgr_test)

add_executable(audit_baseline_test
    src/fixtures/tests/audit_baseline_test.c
    src/audit/baseline.c
)
target_include_directories(audit_baseline_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
add_test(NAME audit_baseline_test COMMAND audit_baseline_test)

# --- ib-install (P6): Authenticode signing + fail-closed ESP staging -------
add_executable(ib-install
    src/install/authenticode.c
    src/install/bootnext.c
    src/install/main.c
    src/install/oneshot.c
    src/install/sign.c
    src/install/stage.c
)
target_include_directories(ib-install PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/install
)
target_link_libraries(ib-install PRIVATE sha256 Advapi32 Crypt32 Wintrust)

# --- ib-tcg-dump (P3): capture + probe CLI ----------------------------------
# Windows user-mode tool; links Tbs.lib (tbs.dll) for TPM Base Services.
add_executable(ib-tcg-dump
    src/tcgdump/main.c
    src/tcgdump/capture.c
    src/tcgdump/hypervisor_health.c
    src/tcgdump/probe.c
    src/tcgdump/tcgdump_util.c
    src/tcgdump/tbs_tcgdump.c
)
target_include_directories(ib-tcg-dump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcgdump
)
target_link_libraries(ib-tcg-dump PRIVATE tcglog Tbs)

# --- Disposable nested-VM capture service ---------------------------------
# A real SCM service host launches the bounded PowerShell evidence workflow.
# Pointing an auto-start service directly at powershell.exe is not a valid
# service contract because it never connects to the service dispatcher.
add_executable(ib-nested-e2e-service
    src/runner_service/main.c
)
target_link_libraries(ib-nested-e2e-service PRIVATE Advapi32)
add_test(NAME nested_e2e_service_test
    COMMAND ib-nested-e2e-service --self-test)

add_executable(hypervisor_health_test
    src/fixtures/tests/hypervisor_health_test.c
    src/tcgdump/hypervisor_health.c
)
target_include_directories(hypervisor_health_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcgdump
)
add_test(NAME hypervisor_health_test COMMAND hypervisor_health_test)

# Same src/common/tcglog.c (+ tcglog_ib.c) compiled with TCGLOG_UEFI and
# linked as a freestanding EFI application. Proves task 1.1 (mini EFI app
# builds with /SUBSYSTEM:EFI_APPLICATION /ENTRY:EfiMain), task 1.4 (dual-target
# compile), and task 1.7 (container is portable to the loader target).
add_executable(tcglog_efi_smoke
    src/vendor/efi_entry.c
    src/vendor/efi_alloc.c
    src/vendor/efi_crt.c
    src/common/tcglog.c
    src/common/tcglog_ib.c
)
target_include_directories(tcglog_efi_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
target_compile_definitions(tcglog_efi_smoke PRIVATE TCGLOG_UEFI)
target_compile_options(tcglog_efi_smoke PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
if(MSVC)
    target_compile_options(tcglog_efi_smoke PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
endif()
set_target_properties(tcglog_efi_smoke PROPERTIES
    SUFFIX ".efi"
    MSVC_RUNTIME_LIBRARY ""
)
if(MSVC)
    target_link_options(tcglog_efi_smoke PRIVATE
        /SUBSYSTEM:EFI_APPLICATION
        /ENTRY:EfiMain
        /MANIFEST:NO
        /NODEFAULTLIB
    )
endif()

foreach(_target IN ITEMS handoff_payload_stub handoff_bootmgr_stub)
    add_executable(${_target}
        src/fixtures/handoff_child_stub.c
        src/vendor/efi_crt.c
    )
    target_include_directories(${_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    )
    target_compile_options(${_target} PRIVATE
        "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
    if(MSVC)
        target_compile_options(${_target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
    endif()
    set_target_properties(${_target} PROPERTIES
        SUFFIX ".efi"
        MSVC_RUNTIME_LIBRARY ""
    )
    if(MSVC)
        target_link_options(${_target} PRIVATE
            /SUBSYSTEM:EFI_APPLICATION
            /ENTRY:EfiMain
            /MANIFEST:NO
            /NODEFAULTLIB
        )
    endif()
endforeach()
target_compile_definitions(handoff_payload_stub PRIVATE HANDOFF_PAYLOAD_STUB)
target_compile_definitions(handoff_bootmgr_stub PRIVATE HANDOFF_BOOTMGR_STUB)

# --- loader.efi: insecure-boot flow (C port of the ib-loader Rust project) ----
# Freestanding EFI application built only from src/insecure_boot. The port ships
# its own UEFI typedef surface (src/insecure_boot/include/ib_uefi.h) and its own
# freestanding helpers, so no src/common or src/vendor source joins this target.
add_executable(loader
    src/insecure_boot/host-free/ib_config.c
    src/insecure_boot/host-free/ib_debug.c
    src/insecure_boot/host-free/ib_format.c
    src/insecure_boot/host-free/ib_hash.c
    src/insecure_boot/host-free/ib_tcg2_error.c
    src/insecure_boot/host-free/ib_tcg2_final_events.c
    src/insecure_boot/host-free/ib_tcg2_hash.c
    src/insecure_boot/host-free/ib_tcg2_install.c
    src/insecure_boot/host-free/ib_tcg2_instance.c
    src/insecure_boot/host-free/ib_tcg2_log.c
    src/insecure_boot/host-free/ib_tcg2_log_efi.c
    src/insecure_boot/host-free/ib_tcg2_pecoff.c
    src/insecure_boot/host-free/ib_tcglog.c
    src/insecure_boot/host-free/ib_tpm2.c
    src/insecure_boot/host-free/ib_tpm_crb.c
    src/insecure_boot/host-free/ib_uacpi_stub.c
    src/insecure_boot/host-free/ib_util.c
    src/insecure_boot/loader/ib_boot.c
    src/insecure_boot/loader/ib_bootmgfw.c
    src/insecure_boot/loader/ib_console.c
    src/insecure_boot/loader/ib_efi_glue.c
    src/insecure_boot/loader/ib_fs.c
    src/insecure_boot/loader/ib_loader_error.c
    src/insecure_boot/loader/ib_main.c
    src/insecure_boot/loader/ib_nvram.c
    src/insecure_boot/loader/ib_payload.c
    src/insecure_boot/loader/ib_replay.c
    src/insecure_boot/loader/ib_sbat.c
    src/insecure_boot/loader/ib_tcg2_loader.c
)
# MSVC's loop-idiom recognition rewrites ib_util.c's own byte loops into calls
# to memcpy/memset. /NODEFAULTLIB cannot resolve them because the port ships its
# own ib_memcpy/ib_memset instead of a CRT, and /Oi- no longer suppresses that
# recognition in this toolset. Compile this one file unoptimised so the port's
# loops stay intact; every other port source keeps the Release /O2.
set_source_files_properties(
    src/insecure_boot/host-free/ib_util.c
    COMPILE_FLAGS "/Od")
target_include_directories(loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/insecure_boot/include
)
target_compile_options(loader PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
if(IB_PAYLOAD_REPLAY_INDEPENDENT)
    target_compile_definitions(loader PRIVATE
        IB_PAYLOAD_REPLAY_INDEPENDENT=1)
endif()
if(MSVC)
    target_compile_options(loader PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
endif()
set_target_properties(loader PROPERTIES
    SUFFIX ".efi"
    MSVC_RUNTIME_LIBRARY ""
)
if(MSVC)
    target_link_options(loader PRIVATE
        /SUBSYSTEM:EFI_APPLICATION
        /ENTRY:EfiMain
        /FIXED:NO
        /MANIFEST:NO
        /NODEFAULTLIB
    )
endif()
# MSVC still emits a stack-probe call for ib_fs_wipe's 4 KiB chunk buffer (ib_fs.c
# IB_FS_WIPE_CHUNK). /NODEFAULTLIB drops the CRT that owns __chkstk, so link the
# toolset's standalone probe object (shipped for freestanding builds) by path.
if(MSVC)
    get_filename_component(_ib_cc "${CMAKE_C_COMPILER}" REALPATH)
    get_filename_component(_ib_d1 "${_ib_cc}" DIRECTORY)
    get_filename_component(_ib_d2 "${_ib_d1}" DIRECTORY)
    get_filename_component(_ib_d3 "${_ib_d2}" DIRECTORY)
    get_filename_component(_ib_libdir "${_ib_d3}" DIRECTORY)
    get_filename_component(_ib_arch "${_ib_d2}" NAME)
    string(REPLACE "Host" "" _ib_arch "${_ib_arch}")
    set(_ib_libdir "${_ib_libdir}/lib/${_ib_arch}")
    if(NOT EXISTS "${_ib_libdir}/chkstk.obj")
        message(FATAL_ERROR "MSVC stack-probe object not found in ${_ib_libdir}")
    endif()
    target_link_directories(loader PRIVATE "${_ib_libdir}")
    target_link_options(loader PRIVATE "chkstk.obj")
    unset(_ib_cc)
    unset(_ib_d1)
    unset(_ib_d2)
    unset(_ib_d3)
    unset(_ib_libdir)
    unset(_ib_arch)
endif()

# The .sbat section shim 15.3 requires is emitted by insecure_boot/loader/ib_sbat.c
# and is never read at run time, so /INCLUDE keeps the section data in the image.
if(MSVC)
    target_link_options(loader PRIVATE /INCLUDE:ib_sbat_section /INCREMENTAL:NO)
endif()

add_executable(uefi_runtime_abi_test src/fixtures/tests/uefi_runtime_abi_test.c)
target_include_directories(uefi_runtime_abi_test PRIVATE src/vendor)
add_test(NAME uefi_runtime_abi_test COMMAND uefi_runtime_abi_test)

# Exercise the actual C port's ordinary ownership/recovery helpers on the host.
add_executable(ib_bootmgfw_port_test
    src/fixtures/tests/ib_bootmgfw_port_test.c
    src/insecure_boot/loader/ib_bootmgfw.c
    src/insecure_boot/loader/ib_boot.c
    src/insecure_boot/loader/ib_efi_glue.c
    src/insecure_boot/host-free/ib_util.c)
target_include_directories(ib_bootmgfw_port_test PRIVATE src/insecure_boot/include)
add_test(NAME ib_bootmgfw_port_test COMMAND ib_bootmgfw_port_test)

add_executable(ib_port_safety_test
    src/fixtures/tests/ib_port_safety_test.c
    src/insecure_boot/host-free/ib_debug.c
    src/insecure_boot/host-free/ib_format.c
    src/insecure_boot/host-free/ib_util.c)
target_include_directories(ib_port_safety_test PRIVATE src/insecure_boot/include)
add_test(NAME ib_port_safety_test COMMAND ib_port_safety_test)

if(IB_BUILD_BOOT_CHAIN_VM_FIXTURES)
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Boot-chain VM fixtures require the MSVC x64 toolchain")
    endif()
    # This source list deliberately excludes the main loader, TPM, replay,
    # NVRAM mutation, custom TCG protocols, and manual payload mapper.
    add_executable(boot_chain_vm_fixture
        src/fixtures/boot_chain_vm_fixture.c
        src/insecure_boot/loader/ib_bootmgfw.c
        src/insecure_boot/loader/ib_boot.c
        src/insecure_boot/loader/ib_fs.c
        src/insecure_boot/loader/ib_console.c
        src/insecure_boot/loader/ib_efi_glue.c
        src/insecure_boot/host-free/ib_format.c
        src/insecure_boot/host-free/ib_util.c)
    add_executable(boot_chain_vm_child src/fixtures/boot_chain_vm_fixture.c)
    target_compile_definitions(boot_chain_vm_child PRIVATE IB_VM_CHILD=1)
    get_filename_component(_ib_fixture_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_ib_fixture_host "${_ib_fixture_bin}" DIRECTORY)
    get_filename_component(_ib_fixture_binroot "${_ib_fixture_host}" DIRECTORY)
    get_filename_component(_ib_fixture_vcroot "${_ib_fixture_binroot}" DIRECTORY)
    foreach(_fixture IN ITEMS boot_chain_vm_fixture boot_chain_vm_child)
        target_include_directories(${_fixture} PRIVATE src/insecure_boot/include)
        target_compile_options(${_fixture} PRIVATE /GS- /Oi-)
        set_target_properties(${_fixture} PROPERTIES SUFFIX ".efi" MSVC_RUNTIME_LIBRARY "")
        target_link_options(${_fixture} PRIVATE /SUBSYSTEM:EFI_APPLICATION /ENTRY:EfiMain
            /FIXED:NO /INCREMENTAL:NO /MANIFEST:NO /NODEFAULTLIB
            "${_ib_fixture_vcroot}/lib/x64/chkstk.obj")
    endforeach()
endif()

# --- Static CRT wiring --------------------------------------------------------
# Applied to every Windows host target (tcglog + pcr_gate libs, host tests,
# ib-tcg-dump). tcglog_efi_smoke and loader are deliberately excluded: they are
# freestanding EFI apps linked with /NODEFAULTLIB and their own minimal CRT, and
# must carry no runtime flag (their MSVC_RUNTIME_LIBRARY is "" above).
if(IB_STATIC_RUNTIME AND MSVC)
    set(_ib_msvc_runtime
        "$<$<CONFIG:Debug>:MultiThreadedDebug>$<$<NOT:$<CONFIG:Debug>>:MultiThreaded>")
    foreach(_target IN ITEMS tcglog pcr_gate sha256
                               tcglog_test tcglog_ib_test pcr_gate_test sha256_test
                               install_sign_test install_authenticode_test
                               install_stage_test install_oneshot_test
                               install_bootnext_test
                               audit_vars_test audit_tcg_version_test
                               audit_bootmgr_test audit_baseline_test
                               uefi_runtime_abi_test ib_bootmgfw_port_test ib_port_safety_test
                               ib-tcg-dump hypervisor_health_test ib-install
                               ib-nested-e2e-service)
        set_target_properties(${_target} PROPERTIES
            MSVC_RUNTIME_LIBRARY "${_ib_msvc_runtime}")
    endforeach()
    unset(_ib_msvc_runtime)
endif()
