# hypervisor_loader — User-mode Win32 loader (.exe)
# Loads the signed driver via SCM. No WDK dependency.

add_executable(hypervisor_loader
    loader_main.c
    control_session_protocol_client.c
    loader_cpuid_probe.c
    loader_msr_read_probe.c
    loader_tsc_probe.c
    loader_invlpg_probe.c
    loader_cet_vmcs_probe.c
    launch_probe_diagnostics.c
    loader_service.c
    "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_compatibility.c"
)

target_include_directories(hypervisor_loader
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_compile_features(hypervisor_loader PRIVATE c_std_17)

if(MSVC)
    target_compile_options(hypervisor_loader PRIVATE
        /W4
        /WX
        /GS
    )
else()
    target_compile_options(hypervisor_loader PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wno-unused-parameter
    )
endif()

target_compile_definitions(hypervisor_loader PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

target_link_libraries(hypervisor_loader PRIVATE
    advapi32.lib   # SCM: OpenSCManager, CreateService, StartService
)

add_executable(control_session_owner_probe
    control_session_owner_probe.c
    control_session_protocol_client.c
)

target_include_directories(control_session_owner_probe
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
)

target_compile_features(control_session_owner_probe PRIVATE c_std_17)

if(MSVC)
    target_compile_options(control_session_owner_probe PRIVATE /W4 /WX /GS)
else()
    target_compile_options(control_session_owner_probe PRIVATE
        -Wall -Wextra -Werror)
endif()

target_compile_definitions(control_session_owner_probe PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

add_executable(control_session_owner_lifetime_probe
    control_session_owner_lifetime_probe.c
    control_session_protocol_client.c
)

target_include_directories(control_session_owner_lifetime_probe
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
)

target_compile_features(control_session_owner_lifetime_probe PRIVATE c_std_17)

if(MSVC)
    target_compile_options(control_session_owner_lifetime_probe PRIVATE
        /W4 /WX /GS)
else()
    target_compile_options(control_session_owner_lifetime_probe PRIVATE
        -Wall -Wextra -Werror)
endif()

target_compile_definitions(control_session_owner_lifetime_probe PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

add_executable(control_session_protocol_probe
    control_session_protocol_probe.c
    control_session_protocol_client.c
)

target_include_directories(control_session_protocol_probe
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
)

target_compile_features(control_session_protocol_probe PRIVATE c_std_17)

if(MSVC)
    target_compile_options(control_session_protocol_probe PRIVATE /W4 /WX /GS)
else()
    target_compile_options(control_session_protocol_probe PRIVATE
        -Wall -Wextra -Werror)
endif()

target_compile_definitions(control_session_protocol_probe PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

add_executable(control_session_request_frame_probe
    control_session_request_frame_probe.c
    control_session_protocol_client.c
)

target_include_directories(control_session_request_frame_probe
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
)

target_compile_features(control_session_request_frame_probe PRIVATE c_std_17)

if(MSVC)
    target_compile_options(control_session_request_frame_probe PRIVATE
        /W4 /WX /GS)
else()
    target_compile_options(control_session_request_frame_probe PRIVATE
        -Wall -Wextra -Werror)
endif()

target_compile_definitions(control_session_request_frame_probe PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

add_executable(control_session_protocol_replay_probe
    control_session_protocol_replay_probe.c
    control_session_protocol_client.c
)

target_include_directories(control_session_protocol_replay_probe
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
)

target_compile_features(control_session_protocol_replay_probe PRIVATE c_std_17)

if(MSVC)
    target_compile_options(control_session_protocol_replay_probe PRIVATE
        /W4 /WX /GS)
else()
    target_compile_options(control_session_protocol_replay_probe PRIVATE
        -Wall -Wextra -Werror)
endif()

target_compile_definitions(control_session_protocol_replay_probe PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

add_executable(control_session_response_frame_probe
    control_session_response_frame_probe.c
    control_session_protocol_client.c
)

target_include_directories(control_session_response_frame_probe
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
)

target_compile_features(control_session_response_frame_probe PRIVATE c_std_17)

if(MSVC)
    target_compile_options(control_session_response_frame_probe PRIVATE
        /W4 /WX /GS)
else()
    target_compile_options(control_session_response_frame_probe PRIVATE
        -Wall -Wextra -Werror)
endif()

target_compile_definitions(control_session_response_frame_probe PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)
