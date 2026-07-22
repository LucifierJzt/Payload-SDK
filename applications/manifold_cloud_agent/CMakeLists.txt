cmake_minimum_required(VERSION 3.16)

project(manifold_cloud_agent LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PSDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(PSDK_LIB_DIR "${PSDK_ROOT}/psdk_lib/lib/aarch64-linux-gnu-gcc")
set(PSDK_INCLUDE_DIR "${PSDK_ROOT}/psdk_lib/include")
set(PSDK_LINUX_COMMON_DIR "${PSDK_ROOT}/samples/sample_c/platform/linux/common")
set(PSDK_MANIFOLD3_HAL_DIR "${PSDK_ROOT}/samples/sample_c/platform/linux/manifold3/hal")
set(MANIFOLD_AGENT_USER_APP_ID "" CACHE STRING "DJI user_app_id used only for the M0 DPK manifest")
set(MANIFOLD_AGENT_DPK_NAME "PSDK" CACHE STRING
    "Registered DJI PSDK app name used only for the M0 probe package")
set(MANIFOLD_AGENT_CREDENTIALS_PATH "/home/dji/workspace/credentials.env" CACHE STRING
    "Absolute Manifold-only credentials.env path; never add the file to the DPK or Git")
set(MANIFOLD_AGENT_EMBED_CREDENTIALS_FILE "" CACHE FILEPATH
    "M0-only credentials.env to embed in the probe binary; never enable for production")

if(NOT EXISTS "${PSDK_LIB_DIR}/libpayloadsdk.a")
    message(FATAL_ERROR "Missing aarch64 PSDK library: ${PSDK_LIB_DIR}/libpayloadsdk.a")
endif()

if(MANIFOLD_AGENT_EMBED_CREDENTIALS_FILE)
    if(NOT EXISTS "${MANIFOLD_AGENT_EMBED_CREDENTIALS_FILE}")
        message(FATAL_ERROR "M0 credentials file does not exist: ${MANIFOLD_AGENT_EMBED_CREDENTIALS_FILE}")
    endif()

    function(read_m0_credential variable_name output_name)
        file(STRINGS "${MANIFOLD_AGENT_EMBED_CREDENTIALS_FILE}" matching_lines
            REGEX "^${variable_name}=.+$")
        list(LENGTH matching_lines matching_count)
        if(NOT matching_count EQUAL 1)
            message(FATAL_ERROR "Expected exactly one ${variable_name} entry in the M0 credentials file")
        endif()
        list(GET matching_lines 0 matching_line)
        string(REGEX REPLACE "^${variable_name}=" "" credential_value "${matching_line}")
        string(REPLACE "\\" "\\\\" credential_value "${credential_value}")
        string(REPLACE "\"" "\\\"" credential_value "${credential_value}")
        set(${output_name} "${credential_value}" PARENT_SCOPE)
    endfunction()

    read_m0_credential(MANIFOLD_AGENT_APP_NAME M0_PROBE_APP_NAME)
    read_m0_credential(MANIFOLD_AGENT_APP_ID M0_PROBE_APP_ID)
    read_m0_credential(MANIFOLD_AGENT_APP_KEY M0_PROBE_APP_KEY)
    read_m0_credential(MANIFOLD_AGENT_APP_LICENSE M0_PROBE_APP_LICENSE)
    read_m0_credential(MANIFOLD_AGENT_DEVELOPER_ACCOUNT M0_PROBE_DEVELOPER_ACCOUNT)
    read_m0_credential(MANIFOLD_AGENT_BAUD_RATE M0_PROBE_BAUD_RATE)

    set(M0_PROBE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${M0_PROBE_GENERATED_DIR}")
    configure_file(src/probe/m0_probe_credentials.h.in
        "${M0_PROBE_GENERATED_DIR}/m0_probe_credentials.h" @ONLY)
endif()

add_executable(m0_aircraft_info_probe
    src/probe/m0_aircraft_info_probe.c
    ${PSDK_LINUX_COMMON_DIR}/osal/osal.c
    ${PSDK_LINUX_COMMON_DIR}/osal/osal_fs.c
    ${PSDK_LINUX_COMMON_DIR}/osal/osal_socket.c
    ${PSDK_MANIFOLD3_HAL_DIR}/hal_usb_bulk.c
)

target_compile_definitions(m0_aircraft_info_probe PRIVATE
    _GNU_SOURCE
    SYSTEM_ARCH_LINUX
    PLATFORM_ARCH_aarch64=1
    MANIFOLD_AGENT_CREDENTIALS_PATH="${MANIFOLD_AGENT_CREDENTIALS_PATH}"
)

if(MANIFOLD_AGENT_EMBED_CREDENTIALS_FILE)
    target_compile_definitions(m0_aircraft_info_probe PRIVATE M0_PROBE_EMBED_CREDENTIALS=1)
    target_include_directories(m0_aircraft_info_probe PRIVATE "${M0_PROBE_GENERATED_DIR}")
endif()

target_include_directories(m0_aircraft_info_probe PRIVATE
    ${PSDK_INCLUDE_DIR}
    ${PSDK_LINUX_COMMON_DIR}/osal
    ${PSDK_MANIFOLD3_HAL_DIR}
)

target_link_libraries(m0_aircraft_info_probe PRIVATE
    ${PSDK_LIB_DIR}/libpayloadsdk.a
    pthread
    m
    dl
)

# Match the executable name used by the registered Manifold 3 PSDK application.
set_target_properties(m0_aircraft_info_probe PROPERTIES OUTPUT_NAME dji_sdk_demo_on_manifold3)

if(MANIFOLD_AGENT_USER_APP_ID AND MANIFOLD_AGENT_DPK_NAME)
    set(M0_DPK_MANIFEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/packaging")
    file(MAKE_DIRECTORY "${M0_DPK_MANIFEST_DIR}")
    configure_file(packaging/m0-aircraft-info-probe.app.json.in
        "${M0_DPK_MANIFEST_DIR}/app.json" @ONLY)

    add_custom_target(m0_aircraft_info_probe_dpk
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/dpk"
        COMMAND bash "${PSDK_ROOT}/tools/build_dpk/build_dpk.sh"
            -i "${M0_DPK_MANIFEST_DIR}/app.json"
            -o "${CMAKE_CURRENT_BINARY_DIR}/dpk"
        DEPENDS m0_aircraft_info_probe "${M0_DPK_MANIFEST_DIR}/app.json"
        COMMENT "Packaging the M0 read-only AircraftInfo probe as a Manifold 3 DPK"
        VERBATIM
    )
else()
    message(STATUS "Set MANIFOLD_AGENT_USER_APP_ID and MANIFOLD_AGENT_DPK_NAME to enable the M0 DPK target")
endif()
