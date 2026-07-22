#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dji_aircraft_info.h"
#include "dji_core.h"
#include "dji_error.h"
#include "dji_logger.h"
#include "dji_platform.h"
#include "hal_usb_bulk.h"
#include "osal.h"
#include "osal_fs.h"
#include "osal_socket.h"

#ifdef M0_PROBE_EMBED_CREDENTIALS
#include "m0_probe_credentials.h"
#endif

#ifndef MANIFOLD_AGENT_CREDENTIALS_PATH
#define MANIFOLD_AGENT_CREDENTIALS_PATH "/home/dji/workspace/credentials.env"
#endif

#define M0_PROBE_TRACE_PATH "/home/dji/m0_probe_install_trace.log"

static volatile sig_atomic_t s_stop_requested;

static void Probe_StopHandler(int signal_number)
{
    (void) signal_number;
    s_stop_requested = 1;
}

static void Probe_Trace(const char *stage, T_DjiReturnCode result)
{
    FILE *trace_file = fopen(M0_PROBE_TRACE_PATH, "a");
    if (trace_file == NULL) {
        return;
    }
    fprintf(trace_file, "M0_PROBE stage=%s result=0x%08lX\n", stage, (unsigned long) result);
    fclose(trace_file);
}

static void Probe_TraceAircraftInfo(const char *phase, const T_DjiAircraftInfoBaseInfo *base_info,
                                    const T_DjiAircraftVersion *version, T_DjiReturnCode result)
{
    FILE *trace_file = fopen(M0_PROBE_TRACE_PATH, "a");
    if (trace_file == NULL) {
        return;
    }
    if (version != NULL) {
        fprintf(trace_file, "AIRCRAFT_VERSION phase=%s version=%u.%u.%u.%u\n", phase,
                version->majorVersion, version->minorVersion, version->modifyVersion, version->debugVersion);
    } else if (base_info != NULL) {
        fprintf(trace_file,
                "AIRCRAFT_INFO phase=%s series=%d type=%d adapter=%d mount_type=%d mount_position=%d\n",
                phase, base_info->aircraftSeries, base_info->aircraftType, base_info->djiAdapterType,
                base_info->mountPositionType, base_info->mountPosition);
    } else {
        fprintf(trace_file, "AIRCRAFT_INFO phase=%s result=0x%08lX\n", phase, (unsigned long) result);
    }
    fclose(trace_file);
}

static T_DjiReturnCode Probe_ConsoleWrite(const uint8_t *data, uint16_t data_len)
{
    (void) data_len;
    fputs((const char *) data, stdout);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode Probe_RegisterPlatformHandlers(void)
{
    const T_DjiOsalHandler osal_handler = {
        .TaskCreate = Osal_TaskCreate,
        .TaskDestroy = Osal_TaskDestroy,
        .TaskSleepMs = Osal_TaskSleepMs,
        .MutexCreate = Osal_MutexCreate,
        .MutexDestroy = Osal_MutexDestroy,
        .MutexLock = Osal_MutexLock,
        .MutexUnlock = Osal_MutexUnlock,
        .SemaphoreCreate = Osal_SemaphoreCreate,
        .SemaphoreDestroy = Osal_SemaphoreDestroy,
        .SemaphoreWait = Osal_SemaphoreWait,
        .SemaphoreTimedWait = Osal_SemaphoreTimedWait,
        .SemaphorePost = Osal_SemaphorePost,
        .Malloc = Osal_Malloc,
        .Free = Osal_Free,
        .GetRandomNum = Osal_GetRandomNum,
        .GetTimeMs = Osal_GetTimeMs,
        .GetTimeUs = Osal_GetTimeUs,
    };
    const T_DjiHalUsbBulkHandler usb_bulk_handler = {
        .UsbBulkInit = HalUsbBulk_Init,
        .UsbBulkDeInit = HalUsbBulk_DeInit,
        .UsbBulkWriteData = HalUsbBulk_WriteData,
        .UsbBulkReadData = HalUsbBulk_ReadData,
        .UsbBulkGetDeviceInfo = HalUsbBulk_GetDeviceInfo,
    };
    const T_DjiFileSystemHandler file_system_handler = {
        .FileOpen = Osal_FileOpen,
        .FileClose = Osal_FileClose,
        .FileWrite = Osal_FileWrite,
        .FileRead = Osal_FileRead,
        .FileSync = Osal_FileSync,
        .FileSeek = Osal_FileSeek,
        .DirOpen = Osal_DirOpen,
        .DirClose = Osal_DirClose,
        .DirRead = Osal_DirRead,
        .Mkdir = Osal_Mkdir,
        .Unlink = Osal_Unlink,
        .Rename = Osal_Rename,
        .Stat = Osal_Stat,
    };
    const T_DjiSocketHandler socket_handler = {
        .Socket = Osal_Socket,
        .Bind = Osal_Bind,
        .Close = Osal_Close,
        .UdpSendData = Osal_UdpSendData,
        .UdpRecvData = Osal_UdpRecvData,
        .TcpListen = Osal_TcpListen,
        .TcpAccept = Osal_TcpAccept,
        .TcpConnect = Osal_TcpConnect,
        .TcpSendData = Osal_TcpSendData,
        .TcpRecvData = Osal_TcpRecvData,
    };
    T_DjiLoggerConsole console = {
        .func = Probe_ConsoleWrite,
        .consoleLevel = DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO,
        .isSupportColor = false,
    };
    T_DjiReturnCode result;

    result = DjiPlatform_RegOsalHandler(&osal_handler);
    if (result == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        result = DjiPlatform_RegHalUsbBulkHandler(&usb_bulk_handler);
    }
    if (result == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        result = DjiPlatform_RegSocketHandler(&socket_handler);
    }
    if (result == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        result = DjiPlatform_RegFileSystemHandler(&file_system_handler);
    }
    if (result == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        result = DjiLogger_AddConsole(&console);
    }
    return result;
}

static bool Probe_CopyRequiredEnv(char *destination, size_t destination_size, const char *environment_name)
{
    const char *value = getenv(environment_name);
    size_t value_length;

    if (value == NULL || value[0] == '\0') {
        fprintf(stderr, "Missing required environment variable: %s\n", environment_name);
        return false;
    }
    value_length = strlen(value);
    if (value_length >= destination_size) {
        fprintf(stderr, "Value is too long for %s\n", environment_name);
        return false;
    }
    memcpy(destination, value, value_length + 1U);
    return true;
}

static bool Probe_IsCredentialName(const char *name)
{
    return strcmp(name, "MANIFOLD_AGENT_APP_NAME") == 0 ||
           strcmp(name, "MANIFOLD_AGENT_APP_ID") == 0 ||
           strcmp(name, "MANIFOLD_AGENT_APP_KEY") == 0 ||
           strcmp(name, "MANIFOLD_AGENT_APP_LICENSE") == 0 ||
           strcmp(name, "MANIFOLD_AGENT_DEVELOPER_ACCOUNT") == 0 ||
           strcmp(name, "MANIFOLD_AGENT_BAUD_RATE") == 0;
}

static bool Probe_LoadCredentialFile(void)
{
    FILE *file = fopen(MANIFOLD_AGENT_CREDENTIALS_PATH, "r");
    char line[2048];

    if (file == NULL) {
        fprintf(stderr, "Cannot read credentials file: %s\n", MANIFOLD_AGENT_CREDENTIALS_PATH);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *name = line;
        char *value = strchr(line, '=');
        size_t value_length;

        if (value == NULL || name[0] == '#' || name[0] == '\n') {
            continue;
        }
        *value++ = '\0';
        value[strcspn(value, "\r\n")] = '\0';
        if (!Probe_IsCredentialName(name)) {
            continue;
        }
        value_length = strlen(value);
        if (value_length >= 2U &&
            ((value[0] == '\"' && value[value_length - 1U] == '\"') ||
             (value[0] == '\'' && value[value_length - 1U] == '\''))) {
            value[value_length - 1U] = '\0';
            ++value;
        }
        if (setenv(name, value, 0) != 0) {
            fclose(file);
            fprintf(stderr, "Cannot load required credential variable: %s\n", name);
            return false;
        }
    }
    fclose(file);
    return true;
}

static bool Probe_LoadUserInfo(T_DjiUserInfo *user_info)
{
#ifdef M0_PROBE_EMBED_CREDENTIALS
    memset(user_info, 0, sizeof(*user_info));
    if (strlen(kM0ProbeAppName) >= sizeof(user_info->appName) ||
        strlen(kM0ProbeAppId) >= sizeof(user_info->appId) ||
        strlen(kM0ProbeAppKey) >= sizeof(user_info->appKey) ||
        strlen(kM0ProbeAppLicense) >= sizeof(user_info->appLicense) ||
        strlen(kM0ProbeDeveloperAccount) >= sizeof(user_info->developerAccount) ||
        strlen(kM0ProbeBaudRate) >= sizeof(user_info->baudRate)) {
        fprintf(stderr, "Embedded M0 credentials exceed a PSDK field limit\n");
        return false;
    }
    strcpy(user_info->appName, kM0ProbeAppName);
    strcpy(user_info->appId, kM0ProbeAppId);
    strcpy(user_info->appKey, kM0ProbeAppKey);
    strcpy(user_info->appLicense, kM0ProbeAppLicense);
    strcpy(user_info->developerAccount, kM0ProbeDeveloperAccount);
    strcpy(user_info->baudRate, kM0ProbeBaudRate);
    return true;
#else
    if (!Probe_LoadCredentialFile()) {
        return false;
    }
    memset(user_info, 0, sizeof(*user_info));
    return Probe_CopyRequiredEnv(user_info->appName, sizeof(user_info->appName), "MANIFOLD_AGENT_APP_NAME") &&
           Probe_CopyRequiredEnv(user_info->appId, sizeof(user_info->appId), "MANIFOLD_AGENT_APP_ID") &&
           Probe_CopyRequiredEnv(user_info->appKey, sizeof(user_info->appKey), "MANIFOLD_AGENT_APP_KEY") &&
           Probe_CopyRequiredEnv(user_info->appLicense, sizeof(user_info->appLicense), "MANIFOLD_AGENT_APP_LICENSE") &&
           Probe_CopyRequiredEnv(user_info->developerAccount, sizeof(user_info->developerAccount),
                                 "MANIFOLD_AGENT_DEVELOPER_ACCOUNT") &&
           Probe_CopyRequiredEnv(user_info->baudRate, sizeof(user_info->baudRate), "MANIFOLD_AGENT_BAUD_RATE");
#endif
}

static T_DjiReturnCode Probe_ReportAircraftInfo(const char *phase)
{
    T_DjiAircraftInfoBaseInfo base_info = {0};
    T_DjiAircraftVersion version = {0};
    T_DjiReturnCode result = DjiAircraftInfo_GetBaseInfo(&base_info);

    if (result != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        printf("AIRCRAFT_INFO phase=%s base_info_result=0x%08lX\n", phase, (unsigned long) result);
        Probe_TraceAircraftInfo(phase, NULL, NULL, result);
        return result;
    }
    printf("AIRCRAFT_INFO phase=%s series=%d type=%d adapter=%d mount_type=%d mount_position=%d\n",
           phase, base_info.aircraftSeries, base_info.aircraftType, base_info.djiAdapterType,
           base_info.mountPositionType, base_info.mountPosition);
    Probe_TraceAircraftInfo(phase, &base_info, NULL, result);

    result = DjiAircraftInfo_GetAircraftVersion(&version);
    if (result == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        printf("AIRCRAFT_VERSION phase=%s version=%u.%u.%u.%u\n", phase, version.majorVersion,
               version.minorVersion, version.modifyVersion, version.debugVersion);
        Probe_TraceAircraftInfo(phase, NULL, &version, result);
    } else {
        printf("AIRCRAFT_VERSION phase=%s result=0x%08lX\n", phase, (unsigned long) result);
        Probe_TraceAircraftInfo(phase, NULL, NULL, result);
    }
    return result;
}

int main(void)
{
    T_DjiUserInfo user_info;
    const T_DjiFirmwareVersion firmware_version = {
        .majorVersion = 1,
        .minorVersion = 0,
        .modifyVersion = 0,
        .debugVersion = 7,
    };
    T_DjiReturnCode result;
    int exit_code = EXIT_FAILURE;

    signal(SIGINT, Probe_StopHandler);
    signal(SIGTERM, Probe_StopHandler);
    puts("M0_PROBE stage=begin operation=read_only_aircraft_info");
    Probe_Trace("begin", DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS);
    result = Probe_RegisterPlatformHandlers();
    Probe_Trace("platform_registration", result);
    if (result != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        fprintf(stderr, "M0_PROBE stage=platform_registration result=0x%08lX\n", (unsigned long) result);
        return exit_code;
    }
    if (!Probe_LoadUserInfo(&user_info)) {
        Probe_Trace("load_user_info", DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR);
        return exit_code;
    }
    Probe_Trace("load_user_info", DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS);

    result = DjiCore_Init(&user_info);
    Probe_Trace("core_init", result);
    printf("M0_PROBE stage=core_init result=0x%08lX\n", (unsigned long) result);
    if (result != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return exit_code;
    }

    result = DjiCore_SetAlias("M0_AIRCRAFT_INFO_PROBE");
    Probe_Trace("set_alias", result);
    printf("M0_PROBE stage=set_alias result=0x%08lX\n", (unsigned long) result);
    if (result != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        (void) DjiCore_DeInit();
        return exit_code;
    }
    result = DjiCore_SetFirmwareVersion(firmware_version);
    Probe_Trace("set_firmware_version", result);
    printf("M0_PROBE stage=set_firmware_version result=0x%08lX\n", (unsigned long) result);
    if (result != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        (void) DjiCore_DeInit();
        return exit_code;
    }
    result = DjiCore_SetSerialNumber("M0PROBE00000001");
    Probe_Trace("set_serial_number", result);
    printf("M0_PROBE stage=set_serial_number result=0x%08lX\n", (unsigned long) result);
    if (result != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        (void) DjiCore_DeInit();
        return exit_code;
    }
    (void) Probe_ReportAircraftInfo("before_application_start");
    result = DjiCore_ApplicationStart();
    Probe_Trace("application_start", result);
    printf("M0_PROBE stage=application_start result=0x%08lX\n", (unsigned long) result);
    if (result == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        (void) Probe_ReportAircraftInfo("after_application_start");
        puts("M0_PROBE stage=running operation=read_only_aircraft_info");
        Probe_Trace("running", DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS);
        while (!s_stop_requested) {
            sleep(1);
        }
        Probe_Trace("stop_requested", DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS);
        exit_code = EXIT_SUCCESS;
    }

    result = DjiCore_DeInit();
    Probe_Trace("core_deinit", result);
    printf("M0_PROBE stage=core_deinit result=0x%08lX\n", (unsigned long) result);
    return result == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? exit_code : EXIT_FAILURE;
}
