# UEFILoader — Insecure Boot Toolkit
#
# Milestone M1 (P2): shared portable tcglog library + host test harness +
# dual-target compile proof. Targets landing in later milestones:
#
#   src/tcgdump/ib-tcg-dump Windows TBS capture + probe CLI
#   src/loader/loader.efi   UEFI hybrid PCR-replay loader
#   src/install/ib-install  Windows staging/signing installer
#   src/audit/ibaudit       Windows defensive audit CLI
#   src/fixtures            synthetic logs + host test harness (gen in M5)
#
# Windows tools are MSVC /W4, zero runtime deps beyond CRT + TBS + firmware
# environment APIs. loader.efi uses /SUBSYSTEM:EFI_APPLICATION with vendored
# UEFI/TCG2 headers.

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

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM_MASM)
    add_library(ib_shim_verify_abi OBJECT src/loader/shim_verify_x64.asm)
endif()

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

# --- Portable hybrid replay core (P5) --------------------------------------
# The host library and test exercise the policy without UEFI or TPM APIs.  The
# loader target compiles the same source freestanding below, alongside its
# UEFI allocator-backed tcglog implementation.
add_library(replay STATIC
    src/loader/replay.c
)
target_include_directories(replay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(replay PUBLIC tcglog)

# --- Portable raw-extend divergence manifest (P5) --------------------------
add_library(divergence STATIC
    src/loader/divergence.c
)
target_include_directories(divergence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(divergence PUBLIC replay)

# --- Strict tcglog.ib reference-log loader ---------------------------------
add_library(reference_log STATIC
    src/loader/reference_log.c
)
target_include_directories(reference_log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(reference_log PUBLIC tcglog)

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

add_executable(replay_test
    src/fixtures/tests/replay_test.c
)
target_link_libraries(replay_test PRIVATE replay)
add_test(NAME replay_test COMMAND replay_test)

add_executable(divergence_test
    src/fixtures/tests/divergence_test.c
)
target_link_libraries(divergence_test PRIVATE divergence)
add_test(NAME divergence_test COMMAND divergence_test)

add_executable(sha256_test
    src/fixtures/tests/sha256_test.c
)
target_link_libraries(sha256_test PRIVATE sha256)
add_test(NAME sha256_test COMMAND sha256_test)

add_executable(reference_log_test
    src/fixtures/tests/reference_log_test.c
)
target_link_libraries(reference_log_test PRIVATE reference_log)
add_test(NAME reference_log_test COMMAND reference_log_test)

add_executable(boot_restore_test
    src/fixtures/tests/boot_restore_test.c
    src/loader/boot_restore.c
)
target_include_directories(boot_restore_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(boot_restore_test PRIVATE sha256)
add_test(NAME boot_restore_test COMMAND boot_restore_test)

add_executable(boot_chain_test
    src/fixtures/tests/boot_chain_test.c
    src/loader/boot_chain.c
    src/loader/efi_file.c
)
target_include_directories(boot_chain_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(boot_chain_test PRIVATE sha256)
add_test(NAME boot_chain_test COMMAND boot_chain_test)

add_executable(efi_file_test
    src/fixtures/tests/efi_file_test.c
    src/loader/efi_file.c
)
target_include_directories(efi_file_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(efi_file_test PRIVATE sha256)
add_test(NAME efi_file_test COMMAND efi_file_test)

add_executable(efi_storage_probe_test
    src/fixtures/tests/efi_storage_probe_test.c
    src/loader/efi_storage_probe.c)
target_include_directories(efi_storage_probe_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader)
target_link_libraries(efi_storage_probe_test PRIVATE sha256)
add_test(NAME efi_storage_probe_test COMMAND efi_storage_probe_test)

add_executable(efi_storage_fault_report_test
    src/fixtures/tests/efi_storage_fault_report_test.c
    src/loader/efi_storage_fault_report.c)
target_include_directories(efi_storage_fault_report_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader)
add_test(NAME efi_storage_fault_report_test COMMAND efi_storage_fault_report_test)

add_executable(handoff_diagnostic_test
    src/fixtures/tests/handoff_diagnostic_test.c
    src/loader/handoff_diagnostic.c)
target_include_directories(handoff_diagnostic_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader)
add_test(NAME handoff_diagnostic_test COMMAND handoff_diagnostic_test)

add_executable(image_launch_test
    src/fixtures/tests/image_launch_test.c
    src/loader/image_launch.c
)
target_include_directories(image_launch_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
add_test(NAME image_launch_test COMMAND image_launch_test)

add_executable(payload_diagnostic_test
    src/fixtures/tests/payload_diagnostic_test.c
    src/loader/payload_diagnostic.c
)
target_include_directories(payload_diagnostic_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
add_test(NAME payload_diagnostic_test COMMAND payload_diagnostic_test)

add_executable(inventory_entry_test
    src/fixtures/tests/inventory_entry_test.c
    src/loader/inventory_entry.c
)
target_include_directories(inventory_entry_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
add_test(NAME inventory_entry_test COMMAND inventory_entry_test)

add_executable(tcg2_test
    src/fixtures/tests/tcg2_test.c
    src/loader/tcg2.c
)
target_include_directories(tcg2_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
add_test(NAME tcg2_test COMMAND tcg2_test)

add_executable(replay_uefi_path_test
    src/fixtures/tests/replay_uefi_path_test.c
    src/loader/replay_uefi.c
    src/loader/tcg2.c
)
target_include_directories(replay_uefi_path_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(replay_uefi_path_test PRIVATE replay sha256 tcglog)
add_test(NAME replay_uefi_path_test COMMAND replay_uefi_path_test)

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
add_executable(audit_dump_test
    src/fixtures/tests/audit_dump_test.c
    src/audit/dump.c
)
target_include_directories(audit_dump_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(audit_dump_test PRIVATE reference_log sha256)
add_test(NAME audit_dump_test COMMAND audit_dump_test)

add_executable(audit_pcr_test
    src/fixtures/tests/audit_pcr_test.c
    src/audit/pcr.c
)
target_include_directories(audit_pcr_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(audit_pcr_test PRIVATE reference_log divergence replay)
add_test(NAME audit_pcr_test COMMAND audit_pcr_test)

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

add_executable(audit_bootmgr_platform_test
    src/fixtures/tests/audit_bootmgr_platform_test.c
    src/audit/pcr.c
    src/audit/platform_win.c
    src/audit/vars.c
    src/tcgdump/tbs_tcgdump.c
)
target_include_directories(audit_bootmgr_platform_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcgdump
)
target_link_libraries(audit_bootmgr_platform_test PRIVATE
    reference_log divergence replay sha256 Tbs Bcrypt Crypt32 Wintrust Advapi32)

add_executable(audit_firmware_privilege_test
    src/fixtures/tests/audit_firmware_privilege_test.c
    src/audit/pcr.c
    src/audit/platform_win.c
    src/audit/vars.c
    src/tcgdump/tbs_tcgdump.c
)
target_include_directories(audit_firmware_privilege_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcgdump
)
target_link_libraries(audit_firmware_privilege_test PRIVATE
    reference_log divergence replay sha256 Tbs Bcrypt Crypt32 Wintrust Advapi32)
add_test(NAME audit_firmware_privilege_test COMMAND audit_firmware_privilege_test)

add_executable(audit_baseline_test
    src/fixtures/tests/audit_baseline_test.c
    src/audit/baseline.c
)
target_include_directories(audit_baseline_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
add_test(NAME audit_baseline_test COMMAND audit_baseline_test)

add_executable(handoff_fixture_gen
    src/fixtures/handoff_fixture_gen.c
)
target_link_libraries(handoff_fixture_gen PRIVATE reference_log)

add_executable(fixture_gen
    src/fixtures/gen.c
    src/audit/baseline.c
)
target_include_directories(fixture_gen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(fixture_gen PRIVATE reference_log sha256)

add_executable(fixture_suite_test
    src/fixtures/tests/fixture_suite_test.c
    src/audit/baseline.c
    src/audit/dump.c
    src/audit/tcg_version.c
)
target_include_directories(fixture_suite_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_link_libraries(fixture_suite_test PRIVATE reference_log sha256)

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

# --- ibaudit (P8): read-only defensive audit detectors ---------------------
add_executable(ibaudit
    src/audit/audit_io.c
    src/audit/baseline.c
    src/audit/baseline_capture.c
    src/audit/bootmgr.c
    src/audit/dump.c
    src/audit/main.c
    src/audit/pcr.c
    src/audit/platform_win.c
    src/audit/tcg_version.c
    src/audit/vars.c
    src/tcgdump/tbs_tcgdump.c
)
target_include_directories(ibaudit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audit
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcgdump
)
target_link_libraries(ibaudit PRIVATE
    reference_log divergence replay sha256 Tbs Bcrypt Crypt32 Wintrust Advapi32)

# --- UEFI smoke app (dual-target compile/link proof) ------------------------
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

# --- P5 replay EFI dry-run --------------------------------------------------
# Executes replay_run() over a synthetic H/R/H event sequence under real OVMF
# while callbacks only record their inputs. This proves the freestanding ABI
# and active-bank selection without mutating the TPM during the dry-run.
add_executable(replay_efi_smoke
    src/fixtures/replay_efi_smoke.c
    src/loader/replay.c
    src/vendor/efi_alloc.c
    src/vendor/efi_crt.c
    src/common/tcglog.c
)
target_include_directories(replay_efi_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_compile_definitions(replay_efi_smoke PRIVATE TCGLOG_UEFI)
target_compile_options(replay_efi_smoke PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
if(MSVC)
    target_compile_options(replay_efi_smoke PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
endif()
set_target_properties(replay_efi_smoke PROPERTIES
    SUFFIX ".efi"
    MSVC_RUNTIME_LIBRARY ""
)
if(MSVC)
    target_link_options(replay_efi_smoke PRIVATE
        /SUBSYSTEM:EFI_APPLICATION
        /ENTRY:EfiMain
        /MANIFEST:NO
        /NODEFAULTLIB
    )
endif()

# --- P5 PE/COFF pre-relocation rehash gate ---------------------------------
add_executable(pe_rehash_efi_smoke
    src/fixtures/pe_rehash_efi_smoke.c
    src/loader/efi_file.c
    src/loader/pe_rehash.c
    src/common/sha256.c
    src/vendor/efi_crt.c
)
target_include_directories(pe_rehash_efi_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_compile_options(pe_rehash_efi_smoke PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
if(MSVC)
    target_compile_options(pe_rehash_efi_smoke PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
endif()
set_target_properties(pe_rehash_efi_smoke PROPERTIES
    SUFFIX ".efi"
    MSVC_RUNTIME_LIBRARY ""
)
if(MSVC)
    target_link_options(pe_rehash_efi_smoke PRIVATE
        /SUBSYSTEM:EFI_APPLICATION
        /ENTRY:EfiMain
        /MANIFEST:NO
        /NODEFAULTLIB
    )
endif()

# --- P5 raw-extend divergence manifest gate --------------------------------
add_executable(divergence_efi_smoke
    src/fixtures/divergence_efi_smoke.c
    src/loader/divergence.c
    src/loader/efi_file.c
    src/loader/replay.c
    src/loader/tcg2.c
    src/vendor/efi_alloc.c
    src/vendor/efi_crt.c
    src/common/tcglog.c
    src/common/sha256.c
)
target_include_directories(divergence_efi_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_compile_definitions(divergence_efi_smoke PRIVATE TCGLOG_UEFI)
target_compile_options(divergence_efi_smoke PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
if(MSVC)
    target_compile_options(divergence_efi_smoke PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
endif()

# --- P5 synthetic restore-and-handoff mechanics gate -----------------------
add_executable(handoff_efi_smoke
    src/fixtures/handoff_efi_smoke.c
    src/loader/boot_restore.c
    src/loader/divergence.c
    src/loader/efi_file.c
    src/loader/image_launch.c
    src/loader/reference_log.c
    src/loader/replay.c
    src/loader/tcg2.c
    src/common/sha256.c
    src/common/tcglog.c
    src/common/tcglog_ib.c
    src/vendor/efi_alloc.c
    src/vendor/efi_crt.c
)
target_include_directories(handoff_efi_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_compile_definitions(handoff_efi_smoke PRIVATE TCGLOG_UEFI)
target_compile_options(handoff_efi_smoke PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
if(MSVC)
    target_compile_options(handoff_efi_smoke PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
endif()
set_target_properties(handoff_efi_smoke PROPERTIES
    SUFFIX ".efi"
    MSVC_RUNTIME_LIBRARY ""
)
if(MSVC)
    target_link_options(handoff_efi_smoke PRIVATE
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
set_target_properties(divergence_efi_smoke PROPERTIES
    SUFFIX ".efi"
    MSVC_RUNTIME_LIBRARY ""
)
if(MSVC)
    target_link_options(divergence_efi_smoke PRIVATE
        /SUBSYSTEM:EFI_APPLICATION
        /ENTRY:EfiMain
        /MANIFEST:NO
        /NODEFAULTLIB
    )
endif()

# --- loader.efi (P5): TCG2 layer + empty-bank gate + replay core ------------
# Freestanding EFI application: same pattern as the smoke app, plus the TCG2
# protocol wrapper and the portable gate. tcglog/tcglog_ib join in P5 when the
# replay loop reads the recorded container. /Oi- disables MSVC intrinsic
# generation so the efi_crt.c memcpy/memset definitions do not collide under
# /O2 (the Release issue recorded against tcglog_efi_smoke).
add_executable(loader
    src/loader/boot_restore.c
    src/loader/divergence.c
    src/loader/efi_entry.c
    src/loader/efi_file.c
    src/loader/image_launch.c
    src/loader/payload_diagnostic.c
    src/loader/handoff_diagnostic.c
    src/loader/pe_rehash.c
    src/loader/reference_log.c
    src/loader/tcg2.c
    src/loader/replay.c
    src/loader/replay_uefi.c
    src/common/sha256.c
    src/vendor/efi_alloc.c
    src/vendor/efi_crt.c
    src/common/tcglog.c
    src/common/tcglog_ib.c
    src/common/pcr_gate.c
)
target_include_directories(loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_compile_options(loader PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
target_compile_definitions(loader PRIVATE TCGLOG_UEFI)
if(IB_PAYLOAD_REPLAY_INDEPENDENT)
    target_compile_definitions(loader PRIVATE
        IB_PAYLOAD_REPLAY_INDEPENDENT=1)
endif()
if(IB_FIRMWARE_STORAGE_PROBE)
    target_sources(loader PRIVATE src/loader/efi_storage_probe.c
        src/loader/efi_storage_fault_report.c)
    target_compile_definitions(loader PRIVATE IB_FIRMWARE_STORAGE_PROBE=1)
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

# --- inventory_loader.efi: fixed collector + Windows handoff only -----------
# Keep this entry independent from the replay loader. Its only child paths are
# the fixed inventory collector and Windows Boot Manager paths in
# src/loader/inventory_entry.c; no replay, payload, or NVRAM code is linked.
add_executable(inventory_loader
    src/loader/efi_file.c
    src/loader/image_launch.c
    src/loader/inventory_entry.c
    src/common/sha256.c
    src/vendor/efi_crt.c
)
target_include_directories(inventory_loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
)
target_compile_options(inventory_loader PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
if(MSVC)
    target_compile_options(inventory_loader PRIVATE "$<$<COMPILE_LANGUAGE:C>:/Oi->")
endif()

# --- boot_chain_loader.efi: restore-first firmware Windows handoff ----------
# This separate entry is the BOOT-ONLY route for the installer-owned one-shot
# replacement profile. It restores the hash-pinned canonical Windows Boot
# Manager before calling firmware LoadImage on its full file device path.
# Keep replay, payload, TPM, shim policy hooks, and inventory code out of this
# target so the normal firmware signature policy remains in force.
if(IB_BUILD_BOOT_CHAIN_LOADER)
    add_executable(boot_chain_loader
        src/loader/boot_chain.c
        src/loader/boot_chain_entry.c
        src/loader/boot_restore.c
        src/loader/efi_file.c
        src/loader/sbat.c
        src/common/sha256.c
        src/vendor/efi_crt.c
    )
    target_include_directories(boot_chain_loader PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/loader
    )
    target_compile_options(boot_chain_loader PRIVATE
        "$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/GS->")
    if(MSVC)
        target_compile_options(boot_chain_loader PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:/Oi->")
    endif()
    set_target_properties(boot_chain_loader PROPERTIES
        SUFFIX ".efi"
        MSVC_RUNTIME_LIBRARY ""
    )
    if(MSVC)
        target_link_options(boot_chain_loader PRIVATE
            /SUBSYSTEM:EFI_APPLICATION
            /ENTRY:EfiMain
            /FIXED:NO
            /INCREMENTAL:NO
            /INCLUDE:g_hypersinv_loader_sbat
            /MANIFEST:NO
            /NODEFAULTLIB
        )
    endif()
endif()
set_target_properties(inventory_loader PROPERTIES
    SUFFIX ".efi"
    MSVC_RUNTIME_LIBRARY ""
)
if(MSVC)
    target_link_options(inventory_loader PRIVATE
        /SUBSYSTEM:EFI_APPLICATION
        /ENTRY:EfiMain
        /FIXED:NO
        /INCREMENTAL:NO
        /MANIFEST:NO
        /NODEFAULTLIB
    )
endif()

# Shared shim verification is used by the production and diagnostic paths.
foreach(_shim_target IN ITEMS loader handoff_efi_smoke inventory_loader image_launch_test)
    target_sources(${_shim_target} PRIVATE src/loader/shim_chain.c)
    if(TARGET ib_shim_verify_abi)
        target_sources(${_shim_target} PRIVATE $<TARGET_OBJECTS:ib_shim_verify_abi>)
    endif()
endforeach()
target_sources(loader PRIVATE src/loader/shim_inventory.c src/loader/sbat.c)
if(MSVC)
    target_link_options(loader PRIVATE /INCLUDE:g_hypersinv_loader_sbat /INCREMENTAL:NO)
endif()
if(IB_QEMU_DEBUG_PORTS)
    foreach(_debug_target IN ITEMS loader handoff_efi_smoke)
        target_compile_definitions(${_debug_target} PRIVATE IB_QEMU_DEBUG_PORTS=1)
    endforeach()
endif()

add_executable(shim_inventory_test
    src/fixtures/tests/shim_inventory_test.c
    src/loader/shim_inventory.c)
target_include_directories(shim_inventory_test PRIVATE src/vendor src/loader)
add_test(NAME shim_inventory_test COMMAND shim_inventory_test)

add_executable(shim_chain_test
    src/fixtures/tests/shim_chain_test.c
    src/loader/shim_chain.c)
target_include_directories(shim_chain_test PRIVATE src/vendor src/loader)
target_compile_definitions(shim_chain_test PRIVATE IB_SHIM_CHAIN_HOST_TEST)
if(TARGET ib_shim_verify_abi)
    target_sources(shim_chain_test PRIVATE
        $<TARGET_OBJECTS:ib_shim_verify_abi>
        src/fixtures/tests/shim_verify_abi_test_x64.asm)
endif()
add_test(NAME shim_chain_test COMMAND shim_chain_test)

add_executable(uefi_runtime_abi_test src/fixtures/tests/uefi_runtime_abi_test.c)
target_include_directories(uefi_runtime_abi_test PRIVATE src/vendor)
add_test(NAME uefi_runtime_abi_test COMMAND uefi_runtime_abi_test)

# --- Static CRT wiring --------------------------------------------------------
# Applied to every Windows host target (tcglog + pcr_gate libs, host tests,
# ib-tcg-dump). tcglog_efi_smoke and loader are deliberately excluded: they are
# freestanding EFI apps linked with /NODEFAULTLIB and their own minimal CRT, and
# must carry no runtime flag (their MSVC_RUNTIME_LIBRARY is "" above).
if(IB_STATIC_RUNTIME AND MSVC)
    set(_ib_msvc_runtime
        "$<$<CONFIG:Debug>:MultiThreadedDebug>$<$<NOT:$<CONFIG:Debug>>:MultiThreaded>")
    foreach(_target IN ITEMS tcglog pcr_gate sha256 replay divergence
                               reference_log tcglog_test
                               tcglog_ib_test pcr_gate_test replay_test
                               divergence_test sha256_test reference_log_test
                               boot_restore_test efi_file_test efi_storage_probe_test efi_storage_fault_report_test image_launch_test payload_diagnostic_test handoff_diagnostic_test tcg2_test
                               boot_chain_test inventory_entry_test shim_inventory_test shim_chain_test uefi_runtime_abi_test
                               replay_uefi_path_test
                               install_sign_test install_authenticode_test
                               install_stage_test install_oneshot_test
                               install_bootnext_test
                               audit_dump_test audit_pcr_test audit_vars_test
                               audit_tcg_version_test audit_bootmgr_test
                               audit_bootmgr_platform_test audit_firmware_privilege_test
                               audit_baseline_test
                               handoff_fixture_gen fixture_gen fixture_suite_test
                               ib-tcg-dump hypervisor_health_test ib-install
                               ib-nested-e2e-service
                               ibaudit)
        set_target_properties(${_target} PROPERTIES
            MSVC_RUNTIME_LIBRARY "${_ib_msvc_runtime}")
    endforeach()
    unset(_ib_msvc_runtime)
endif()
