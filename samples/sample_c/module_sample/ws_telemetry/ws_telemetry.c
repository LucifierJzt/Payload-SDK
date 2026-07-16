/**
 * @file ws_telemetry.c
 * @brief Minimal WebSocket client: Manifold FC data -> public hub.
 */

/* Includes ------------------------------------------------------------------*/
#include "ws_telemetry.h"
#include "ws_telemetry_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h>
#include <pthread.h>

#include "dji_logger.h"
#include "dji_platform.h"
#include "dji_fc_subscription.h"
#include "dji_aircraft_info.h"
#include "dji_typedef.h"
#include "utils/util_misc.h"
#include "flight_control/test_flight_control.h"
#include "dji_gimbal_manager.h"
#include "dji_gimbal.h"
#include "ws_liveview.h"

/* Private constants ---------------------------------------------------------*/
#define WS_TELEMETRY_TASK_STACK     (1024 * 8)
#define WS_FLIGHT_TASK_STACK        (1024 * 8)
#define WS_RECV_BUF                 (8192)
#define WS_SEND_BUF                 (3072)
#define WS_PATH_MAX                 (512)
#define WS_REQ_ID_MAX               (64)
#define WS_CMD_NAME_MAX             (48)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Private types -------------------------------------------------------------*/
typedef struct {
    int fd;
    int connected;
} T_WsConn;

typedef struct {
    E_DjiTestFlightCtrlSampleSelect sample;
    char reqId[WS_REQ_ID_MAX];
} T_WsFlightJob;

/* Private values ------------------------------------------------------------*/
static T_DjiTaskHandle s_wsTask;
static volatile int s_running = 0;
static uint32_t s_seq = 0;
static time_t s_startTime = 0;
static pthread_mutex_t s_sendMu = PTHREAD_MUTEX_INITIALIZER;
static T_WsConn *s_liveConn = NULL;
static volatile int s_flightBusy = 0;
static volatile int s_gimbalInited = 0;
static uint8_t s_rxBuf[WS_RECV_BUF];
static size_t s_rxLen = 0;

/* Private functions ---------------------------------------------------------*/
static int Ws_SetNonBlock(int fd, int nb)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (nb) {
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

static int Ws_TcpConnect(const char *host, int port)
{
    char portStr[16];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    int fd = -1;
    int rc;

    snprintf(portStr, sizeof(portStr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(host, portStr, &hints, &res);
    if (rc != 0) {
        USER_LOG_ERROR("ws getaddrinfo(%s): %s", host, gai_strerror(rc));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        USER_LOG_ERROR("ws tcp connect %s:%d failed: %s", host, port, strerror(errno));
    }
    return fd;
}

static int Ws_HttpUpgrade(int fd, const char *host, int port, const char *path)
{
    char req[1024];
    char resp[2048];
    int n;
    int total = 0;

    n = snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s:%d\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                 "Sec-WebSocket-Version: 13\r\n"
                 "\r\n",
                 path, host, port);
    if (n <= 0 || n >= (int) sizeof(req)) {
        return -1;
    }
    if (send(fd, req, (size_t) n, 0) != n) {
        USER_LOG_ERROR("ws send handshake failed");
        return -1;
    }

    memset(resp, 0, sizeof(resp));
    while (total < (int) sizeof(resp) - 1) {
        n = (int) recv(fd, resp + total, sizeof(resp) - 1 - (size_t) total, 0);
        if (n <= 0) {
            USER_LOG_ERROR("ws recv handshake failed");
            return -1;
        }
        total += n;
        if (strstr(resp, "\r\n\r\n") != NULL) {
            break;
        }
    }

    if (strstr(resp, "101") == NULL) {
        USER_LOG_ERROR("ws handshake rejected: %.120s", resp);
        return -1;
    }
    return 0;
}

static int Ws_SendTextUnlocked(int fd, const char *text)
{
    size_t len = strlen(text);
    uint8_t header[14];
    size_t headerLen = 0;
    uint8_t mask[4];
    size_t i;
    uint8_t *frame;
    size_t frameLen;
    ssize_t sent;
    size_t off = 0;

    /* client frames must be masked */
    mask[0] = (uint8_t) (rand() & 0xFF);
    mask[1] = (uint8_t) (rand() & 0xFF);
    mask[2] = (uint8_t) (rand() & 0xFF);
    mask[3] = (uint8_t) (rand() & 0xFF);

    header[0] = 0x81; /* FIN + text */
    if (len <= 125) {
        header[1] = (uint8_t) (0x80 | len);
        headerLen = 2;
    } else if (len <= 0xFFFF) {
        header[1] = (uint8_t) (0x80 | 126);
        header[2] = (uint8_t) ((len >> 8) & 0xFF);
        header[3] = (uint8_t) (len & 0xFF);
        headerLen = 4;
    } else {
        return -1;
    }
    memcpy(header + headerLen, mask, 4);
    headerLen += 4;

    frameLen = headerLen + len;
    frame = (uint8_t *) malloc(frameLen);
    if (frame == NULL) {
        return -1;
    }
    memcpy(frame, header, headerLen);
    for (i = 0; i < len; i++) {
        frame[headerLen + i] = (uint8_t) text[i] ^ mask[i % 4];
    }

    while (off < frameLen) {
        sent = send(fd, frame + off, frameLen - off, 0);
        if (sent <= 0) {
            free(frame);
            return -1;
        }
        off += (size_t) sent;
    }
    free(frame);
    return 0;
}

static int Ws_SendText(int fd, const char *text)
{
    int rc;
    pthread_mutex_lock(&s_sendMu);
    rc = Ws_SendTextUnlocked(fd, text);
    pthread_mutex_unlock(&s_sendMu);
    return rc;
}

static int Ws_SendPong(int fd, const uint8_t *payload, size_t len)
{
    uint8_t header[14];
    size_t headerLen;
    uint8_t mask[4];
    uint8_t *frame;
    size_t frameLen;
    size_t i;
    size_t off = 0;
    ssize_t sent;

    if (len > 125) {
        return -1;
    }
    mask[0] = (uint8_t) (rand() & 0xFF);
    mask[1] = (uint8_t) (rand() & 0xFF);
    mask[2] = (uint8_t) (rand() & 0xFF);
    mask[3] = (uint8_t) (rand() & 0xFF);
    header[0] = 0x8A; /* FIN + pong */
    header[1] = (uint8_t) (0x80 | len);
    headerLen = 2;
    memcpy(header + headerLen, mask, 4);
    headerLen += 4;
    frameLen = headerLen + len;
    frame = (uint8_t *) malloc(frameLen ? frameLen : 1);
    if (frame == NULL) {
        return -1;
    }
    memcpy(frame, header, headerLen);
    for (i = 0; i < len; i++) {
        frame[headerLen + i] = payload[i] ^ mask[i % 4];
    }
    pthread_mutex_lock(&s_sendMu);
    while (off < frameLen) {
        sent = send(fd, frame + off, frameLen - off, 0);
        if (sent <= 0) {
            pthread_mutex_unlock(&s_sendMu);
            free(frame);
            return -1;
        }
        off += (size_t) sent;
    }
    pthread_mutex_unlock(&s_sendMu);
    free(frame);
    return 0;
}

static int Ws_JsonGetString(const char *json, const char *key, char *out, size_t outLen)
{
    char pat[64];
    const char *p;
    const char *q;
    size_t n;

    if (json == NULL || key == NULL || out == NULL || outLen == 0) {
        return -1;
    }
    out[0] = '\0';
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (p == NULL) {
        return -1;
    }
    p = strchr(p + strlen(pat), ':');
    if (p == NULL) {
        return -1;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;
    q = p;
    while (*q && *q != '"') {
        if (*q == '\\' && q[1]) {
            q += 2;
            continue;
        }
        q++;
    }
    if (*q != '"') {
        return -1;
    }
    n = (size_t) (q - p);
    if (n >= outLen) {
        n = outLen - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int Ws_JsonHasTrue(const char *json, const char *key)
{
    char pat[80];
    const char *p;

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (p == NULL) {
        return 0;
    }
    p = strchr(p + strlen(pat), ':');
    if (p == NULL) {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (strncmp(p, "true", 4) == 0) ? 1 : 0;
}

static int Ws_JsonGetNumber(const char *json, const char *key, double *out)
{
    char pat[64];
    const char *p;
    char *end = NULL;
    double v;

    if (json == NULL || key == NULL || out == NULL) {
        return -1;
    }
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (p == NULL) {
        return -1;
    }
    p = strchr(p + strlen(pat), ':');
    if (p == NULL) {
        return -1;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    v = strtod(p, &end);
    if (end == p) {
        return -1;
    }
    *out = v;
    return 0;
}

static void Ws_SendCommandResult(int fd, const char *reqId, int ok, const char *message)
{
    char json[512];
    const char *rid = (reqId && reqId[0]) ? reqId : "";

    snprintf(json, sizeof(json),
             "{\"type\":\"command_result\",\"ts\":%lld,\"device_id\":\"%s\","
             "\"req_id\":\"%s\",\"payload\":{\"ok\":%s,\"message\":\"%s\"}}",
             (long long) (time(NULL) * 1000LL),
             WS_TELEMETRY_DEVICE_ID,
             rid,
             ok ? "true" : "false",
             message ? message : "");
    if (Ws_SendText(fd, json) != 0) {
        USER_LOG_WARN("ws command_result send failed");
    }
}

static T_DjiReturnCode Ws_EnsureGimbalManager(void)
{
    T_DjiReturnCode ret;

    if (s_gimbalInited) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    ret = DjiGimbalManager_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("ws gimbal manager init fail 0x%08X", (unsigned) ret);
        return ret;
    }
    ret = DjiGimbalManager_SetMode((E_DjiMountPosition) WS_TELEMETRY_GIMBAL_MOUNT_POS,
                                   DJI_GIMBAL_MODE_YAW_FOLLOW);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("ws gimbal set mode warn 0x%08X (continue)", (unsigned) ret);
    }
    s_gimbalInited = 1;
    USER_LOG_INFO("ws gimbal manager ready mount=%d", WS_TELEMETRY_GIMBAL_MOUNT_POS);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static void Ws_HandleGimbalCommand(int fd, const char *json, const char *name, const char *reqId)
{
#if !WS_TELEMETRY_ALLOW_GIMBAL_CMD
    Ws_SendCommandResult(fd, reqId, 0, "gimbal_cmd_disabled");
    return;
#else
    T_DjiReturnCode ret;
    T_DjiGimbalManagerRotation rotation = {0};
    double pitch = 0, roll = 0, yaw = 0, timeSec = 0.5;
    char modeStr[32];
    char msg[128];
    E_DjiMountPosition mount = (E_DjiMountPosition) WS_TELEMETRY_GIMBAL_MOUNT_POS;

    ret = Ws_EnsureGimbalManager();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        snprintf(msg, sizeof(msg), "gimbal_init_fail_0x%08X", (unsigned) ret);
        Ws_SendCommandResult(fd, reqId, 0, msg);
        return;
    }

    (void) Ws_JsonGetNumber(json, "pitch", &pitch);
    (void) Ws_JsonGetNumber(json, "roll", &roll);
    (void) Ws_JsonGetNumber(json, "yaw", &yaw);
    if (Ws_JsonGetNumber(json, "time", &timeSec) != 0) {
        timeSec = 0.5;
    }
    if (timeSec < 0.05) {
        timeSec = 0.05;
    }
    if (timeSec > 10.0) {
        timeSec = 10.0;
    }

    if (strcmp(name, "gimbal_reset") == 0) {
        ret = DjiGimbalManager_Reset(mount, DJI_GIMBAL_RESET_MODE_PITCH_AND_YAW);
        if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            Ws_SendCommandResult(fd, reqId, 1, "gimbal_reset_ok");
        } else {
            snprintf(msg, sizeof(msg), "gimbal_reset_fail_0x%08X", (unsigned) ret);
            Ws_SendCommandResult(fd, reqId, 0, msg);
        }
        return;
    }

    if (strcmp(name, "gimbal_mode") == 0) {
        E_DjiGimbalMode gmode = DJI_GIMBAL_MODE_YAW_FOLLOW;
        modeStr[0] = '\0';
        (void) Ws_JsonGetString(json, "mode", modeStr, sizeof(modeStr));
        if (strcmp(modeStr, "free") == 0) {
            gmode = DJI_GIMBAL_MODE_FREE;
        } else if (strcmp(modeStr, "fpv") == 0) {
            gmode = DJI_GIMBAL_MODE_FPV;
        } else if (strcmp(modeStr, "yaw_follow") == 0 || modeStr[0] == '\0') {
            gmode = DJI_GIMBAL_MODE_YAW_FOLLOW;
        }
        ret = DjiGimbalManager_SetMode(mount, gmode);
        if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            snprintf(msg, sizeof(msg), "gimbal_mode_ok %s", modeStr[0] ? modeStr : "yaw_follow");
            Ws_SendCommandResult(fd, reqId, 1, msg);
        } else {
            snprintf(msg, sizeof(msg), "gimbal_mode_fail_0x%08X", (unsigned) ret);
            Ws_SendCommandResult(fd, reqId, 0, msg);
        }
        return;
    }

    /* gimbal_rotate / gimbal_look */
    modeStr[0] = '\0';
    (void) Ws_JsonGetString(json, "mode", modeStr, sizeof(modeStr));
    if (strcmp(modeStr, "absolute") == 0 || strcmp(modeStr, "abs") == 0) {
        rotation.rotationMode = DJI_GIMBAL_ROTATION_MODE_ABSOLUTE_ANGLE;
        {
            T_DjiFcSubscriptionGimbalAngles ga = {0};
            T_DjiDataTimestamp ts = {0};
            if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
                                                       (uint8_t *) &ga, sizeof(ga), &ts)
                == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                if (Ws_JsonGetNumber(json, "yaw", &yaw) != 0) {
                    yaw = ga.z;
                }
            }
        }
    } else if (strcmp(modeStr, "speed") == 0) {
        rotation.rotationMode = DJI_GIMBAL_ROTATION_MODE_SPEED;
    } else {
        rotation.rotationMode = DJI_GIMBAL_ROTATION_MODE_RELATIVE_ANGLE;
    }

    rotation.pitch = (dji_f32_t) pitch;
    rotation.roll = (dji_f32_t) roll;
    rotation.yaw = (dji_f32_t) yaw;
    rotation.time = timeSec;

    USER_LOG_INFO("ws gimbal rotate mode=%d pry=(%.1f,%.1f,%.1f) t=%.2f",
                  (int) rotation.rotationMode, pitch, roll, yaw, timeSec);

    ret = DjiGimbalManager_Rotate(mount, rotation);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        snprintf(msg, sizeof(msg), "gimbal_rotate_ok p=%.1f r=%.1f y=%.1f", pitch, roll, yaw);
        Ws_SendCommandResult(fd, reqId, 1, msg);
    } else {
        snprintf(msg, sizeof(msg), "gimbal_rotate_fail_0x%08X", (unsigned) ret);
        Ws_SendCommandResult(fd, reqId, 0, msg);
    }
#endif
}

static void *Ws_FlightCmdTask(void *arg)
{
    T_WsFlightJob *job = (T_WsFlightJob *) arg;
    T_DjiReturnCode ret;
    int fd = -1;

    if (job == NULL) {
        s_flightBusy = 0;
        return NULL;
    }

    USER_LOG_INFO("ws flight cmd start sample=%d req=%s", (int) job->sample, job->reqId);
    ret = DjiTest_FlightControlRunSample(job->sample);

    pthread_mutex_lock(&s_sendMu);
    if (s_liveConn && s_liveConn->connected) {
        fd = s_liveConn->fd;
    }
    pthread_mutex_unlock(&s_sendMu);

    if (fd >= 0) {
        if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            Ws_SendCommandResult(fd, job->reqId, 1, "flight_sample_done");
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "flight_sample_fail_0x%08X", (unsigned) ret);
            Ws_SendCommandResult(fd, job->reqId, 0, msg);
        }
    }

    USER_LOG_INFO("ws flight cmd end sample=%d ret=0x%08X", (int) job->sample, (unsigned) ret);
    free(job);
    s_flightBusy = 0;
    return NULL;
}

static int Ws_MapCommandToSample(const char *name, E_DjiTestFlightCtrlSampleSelect *out)
{
    if (name == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(name, "takeoff_landing") == 0 || strcmp(name, "takeoff_land") == 0) {
        *out = E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_LANDING;
        return 0;
    }
    if (strcmp(name, "position_ctrl") == 0 || strcmp(name, "takeoff_position_landing") == 0) {
        *out = E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_POSITION_CTRL_LANDING;
        return 0;
    }
    if (strcmp(name, "go_home_force_land") == 0 || strcmp(name, "gohome") == 0) {
        *out = E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_GO_HOME_FORCE_LANDING;
        return 0;
    }
    if (strcmp(name, "velocity_ctrl") == 0) {
        *out = E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_VELOCITY_CTRL_LANDING;
        return 0;
    }
    if (strcmp(name, "arrest_flying") == 0 || strcmp(name, "arrest") == 0) {
        *out = E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_ARREST_FLYING;
        return 0;
    }
    if (strcmp(name, "set_get_param") == 0 || strcmp(name, "param") == 0) {
        *out = E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_SET_GET_PARAM;
        return 0;
    }
    return -1;
}

static void Ws_HandleCommandJson(int fd, const char *json)
{
    char type[32];
    char name[WS_CMD_NAME_MAX];
    char reqId[WS_REQ_ID_MAX];
    E_DjiTestFlightCtrlSampleSelect sample;
    T_WsFlightJob *job;
    pthread_t th;

    if (Ws_JsonGetString(json, "type", type, sizeof(type)) != 0) {
        return;
    }
    if (strcmp(type, "command") != 0 && strcmp(type, "ping") != 0) {
        /* ignore auth_ok / telemetry echo etc. */
        return;
    }

    reqId[0] = '\0';
    (void) Ws_JsonGetString(json, "req_id", reqId, sizeof(reqId));
    name[0] = '\0';
    (void) Ws_JsonGetString(json, "name", name, sizeof(name));

    if (strcmp(type, "ping") == 0 || strcmp(name, "ping") == 0) {
        Ws_SendCommandResult(fd, reqId, 1, "pong");
        return;
    }

    if (name[0] == '\0') {
        Ws_SendCommandResult(fd, reqId, 0, "missing_payload_name");
        return;
    }

    if (strcmp(name, "status") == 0) {
        char msg[256];
        uint64_t liveBytes = 0;
        uint32_t liveCbs = 0;
        char livePath[128];
        livePath[0] = '\0';
        WsLiveview_GetStats(&liveBytes, &liveCbs, livePath, sizeof(livePath));
        snprintf(msg, sizeof(msg),
                 "online flight_busy=%d allow_flight=%d dry_run=%d allow_gimbal=%d "
                 "live=%d live_bytes=%llu live_cb=%u",
                 s_flightBusy, WS_TELEMETRY_ALLOW_FLIGHT_CMD,
                 WS_TELEMETRY_FLIGHT_DRY_RUN, WS_TELEMETRY_ALLOW_GIMBAL_CMD,
                 WsLiveview_IsRunning(),
                 (unsigned long long) liveBytes, (unsigned) liveCbs);
        Ws_SendCommandResult(fd, reqId, 1, msg);
        return;
    }

    if (strcmp(name, "live_start") == 0 || strcmp(name, "liveview_start") == 0) {
#if WS_LIVEVIEW_ENABLE
        {
            T_DjiReturnCode lret = WsLiveview_Start();
            if (lret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                char msg[320];
                char path[160];
                uint64_t b = 0;
                uint32_t c = 0;
                WsLiveview_GetStats(&b, &c, path, sizeof(path));
                snprintf(msg, sizeof(msg), "live_start_ok file=%s rtmp=%d",
                         path, WS_LIVEVIEW_PUSH_RTMP);
                Ws_SendCommandResult(fd, reqId, 1, msg);
            } else {
                char msg[80];
                snprintf(msg, sizeof(msg), "live_start_fail_0x%08X", (unsigned) lret);
                Ws_SendCommandResult(fd, reqId, 0, msg);
            }
        }
#else
        Ws_SendCommandResult(fd, reqId, 0, "liveview_disabled");
#endif
        return;
    }

    if (strcmp(name, "live_stop") == 0 || strcmp(name, "liveview_stop") == 0) {
#if WS_LIVEVIEW_ENABLE
        {
            T_DjiReturnCode lret = WsLiveview_Stop();
            char msg[320];
            char path[160];
            uint64_t b = 0;
            uint32_t c = 0;
            WsLiveview_GetStats(&b, &c, path, sizeof(path));
            snprintf(msg, sizeof(msg), "live_stop_ok bytes=%llu cbs=%u file=%s rc=0x%08X",
                     (unsigned long long) b, (unsigned) c, path, (unsigned) lret);
            Ws_SendCommandResult(fd, reqId, 1, msg);
        }
#else
        Ws_SendCommandResult(fd, reqId, 0, "liveview_disabled");
#endif
        return;
    }

    if (strcmp(name, "live_status") == 0) {
        char msg[320];
        char path[160];
        uint64_t b = 0;
        uint32_t c = 0;
        path[0] = '\0';
        WsLiveview_GetStats(&b, &c, path, sizeof(path));
        snprintf(msg, sizeof(msg), "live_running=%d bytes=%llu cbs=%u file=%s",
                 WsLiveview_IsRunning(), (unsigned long long) b, (unsigned) c, path);
        Ws_SendCommandResult(fd, reqId, 1, msg);
        return;
    }

    if (strcmp(name, "gimbal_rotate") == 0 || strcmp(name, "gimbal_look") == 0 ||
        strcmp(name, "gimbal_reset") == 0 || strcmp(name, "gimbal_mode") == 0) {
        Ws_HandleGimbalCommand(fd, json, name, reqId);
        return;
    }

    if (Ws_MapCommandToSample(name, &sample) != 0) {
        Ws_SendCommandResult(fd, reqId, 0, "unknown_command");
        return;
    }

#if !WS_TELEMETRY_ALLOW_FLIGHT_CMD
    Ws_SendCommandResult(fd, reqId, 0, "flight_cmd_disabled");
    return;
#else
    if (!Ws_JsonHasTrue(json, "confirm")) {
        Ws_SendCommandResult(fd, reqId, 0, "need_confirm_true");
        return;
    }

#if WS_TELEMETRY_FLIGHT_DRY_RUN
    /* Indoor simulation: full WS path, no motors / no takeoff. */
    USER_LOG_INFO("ws DRY_RUN flight cmd name=%s sample=%d req=%s", name, (int) sample, reqId);
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "dry_run_ok sample=%d name=%s (no flight)",
                 (int) sample, name);
        Ws_SendCommandResult(fd, reqId, 1, msg);
    }
    return;
#else
    if (s_flightBusy) {
        Ws_SendCommandResult(fd, reqId, 0, "flight_busy");
        return;
    }

    job = (T_WsFlightJob *) malloc(sizeof(T_WsFlightJob));
    if (job == NULL) {
        Ws_SendCommandResult(fd, reqId, 0, "oom");
        return;
    }
    job->sample = sample;
    snprintf(job->reqId, sizeof(job->reqId), "%s", reqId);
    s_flightBusy = 1;
    if (pthread_create(&th, NULL, Ws_FlightCmdTask, job) != 0) {
        free(job);
        s_flightBusy = 0;
        Ws_SendCommandResult(fd, reqId, 0, "thread_create_fail");
        return;
    }
    pthread_detach(th);
    Ws_SendCommandResult(fd, reqId, 1, "flight_sample_accepted");
#endif /* WS_TELEMETRY_FLIGHT_DRY_RUN */
#endif /* WS_TELEMETRY_ALLOW_FLIGHT_CMD */
}

/**
 * @brief Read available bytes, parse WS frames (server->client, unmasked).
 * @return 0 ok, -1 peer closed / error
 */
static int Ws_PollIncoming(T_WsConn *c)
{
    ssize_t r;
    size_t i;

    if (c == NULL || c->fd < 0) {
        return -1;
    }

    Ws_SetNonBlock(c->fd, 1);
    r = recv(c->fd, s_rxBuf + s_rxLen, sizeof(s_rxBuf) - s_rxLen, 0);
    Ws_SetNonBlock(c->fd, 0);

    if (r == 0) {
        return -1;
    }
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* still try parse buffered */
        } else {
            return -1;
        }
    } else {
        s_rxLen += (size_t) r;
    }

    i = 0;
    while (s_rxLen - i >= 2) {
        uint8_t b0 = s_rxBuf[i];
        uint8_t b1 = s_rxBuf[i + 1];
        uint8_t opcode = (uint8_t) (b0 & 0x0F);
        int masked = (b1 & 0x80) != 0;
        uint64_t payloadLen = (uint64_t) (b1 & 0x7F);
        size_t hdr = 2;
        size_t maskOff = 0;
        size_t frameLen;
        const uint8_t *payload;

        if (payloadLen == 126) {
            if (s_rxLen - i < 4) {
                break;
            }
            payloadLen = ((uint64_t) s_rxBuf[i + 2] << 8) | s_rxBuf[i + 3];
            hdr = 4;
        } else if (payloadLen == 127) {
            /* too large for our buffer — drop connection */
            USER_LOG_WARN("ws frame too large");
            return -1;
        }
        if (masked) {
            maskOff = hdr;
            hdr += 4;
        }
        if (s_rxLen - i < hdr + payloadLen) {
            break;
        }

        payload = s_rxBuf + i + hdr;
        frameLen = hdr + (size_t) payloadLen;

        if (opcode == 0x8) { /* close */
            return -1;
        } else if (opcode == 0x9) { /* ping */
            Ws_SendPong(c->fd, payload, (size_t) payloadLen);
        } else if (opcode == 0x1 || opcode == 0x0) { /* text / continuation as text */
            char *text = (char *) malloc((size_t) payloadLen + 1);
            if (text != NULL) {
                size_t k;
                if (masked) {
                    const uint8_t *m = s_rxBuf + i + maskOff;
                    for (k = 0; k < payloadLen; k++) {
                        text[k] = (char) (payload[k] ^ m[k % 4]);
                    }
                } else {
                    memcpy(text, payload, (size_t) payloadLen);
                }
                text[payloadLen] = '\0';
                USER_LOG_INFO("ws rx: %.160s", text);
                Ws_HandleCommandJson(c->fd, text);
                free(text);
            }
        }

        i += frameLen;
    }

    if (i > 0) {
        memmove(s_rxBuf, s_rxBuf + i, s_rxLen - i);
        s_rxLen -= i;
    }
    if (s_rxLen >= sizeof(s_rxBuf)) {
        USER_LOG_WARN("ws rx buffer full, reset");
        s_rxLen = 0;
    }
    return 0;
}

static void Ws_Close(T_WsConn *c)
{
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    c->connected = 0;
}

static int Ws_ConnectHub(T_WsConn *c)
{
    char path[WS_PATH_MAX];

    Ws_Close(c);
    /* Exact path only: /manifold-ws?device_id=...&token=...&role=device */
    snprintf(path, sizeof(path),
             "%s?device_id=%s&token=%s&role=device",
             WS_TELEMETRY_WS_PATH,
             WS_TELEMETRY_DEVICE_ID, WS_TELEMETRY_DEVICE_TOKEN);

    c->fd = Ws_TcpConnect(WS_TELEMETRY_HOST, WS_TELEMETRY_PORT);
    if (c->fd < 0) {
        return -1;
    }
    if (Ws_HttpUpgrade(c->fd, WS_TELEMETRY_HOST, WS_TELEMETRY_PORT, path) != 0) {
        Ws_Close(c);
        return -1;
    }
    c->connected = 1;
    USER_LOG_INFO("ws telemetry connected to %s:%d%s as %s",
                  WS_TELEMETRY_HOST, WS_TELEMETRY_PORT,
                  WS_TELEMETRY_WS_PATH, WS_TELEMETRY_DEVICE_ID);
    return 0;
}

static void Ws_QuatToEulerDeg(const T_DjiFcSubscriptionQuaternion *q,
                              double *rollDeg, double *pitchDeg, double *yawDeg)
{
    /* Hamilton: q0=w, q1=x, q2=y, q3=z */
    double w = q->q0;
    double x = q->q1;
    double y = q->q2;
    double z = q->q3;
    double sinr = 2.0 * (w * x + y * z);
    double cosr = 1.0 - 2.0 * (x * x + y * y);
    double sinp = 2.0 * (w * y - z * x);
    double siny = 2.0 * (w * z + x * y);
    double cosy = 1.0 - 2.0 * (y * y + z * z);
    double roll;
    double pitch;
    double yaw;

    roll = atan2(sinr, cosr);
    if (fabs(sinp) >= 1.0) {
        pitch = copysign(M_PI / 2.0, sinp);
    } else {
        pitch = asin(sinp);
    }
    yaw = atan2(siny, cosy);

    *rollDeg = roll * 180.0 / M_PI;
    *pitchDeg = pitch * 180.0 / M_PI;
    *yawDeg = yaw * 180.0 / M_PI;
}

static int64_t Ws_NowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *Ws_DisplayModeToStr(uint8_t mode)
{
    switch (mode) {
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_MANUAL_CTRL:
            return "manual";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_ATTITUDE:
            return "attitude";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_P_GPS:
            return "p_gps";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_HOTPOINT_MODE:
            return "hotpoint";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_ASSISTED_TAKEOFF:
            return "assisted_takeoff";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_AUTO_TAKEOFF:
            return "auto_takeoff";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_AUTO_LANDING:
            return "auto_landing";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_NAVI_GO_HOME:
            return "go_home";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_NAVI_SDK_CTRL:
            return "sdk_ctrl";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_FORCE_AUTO_LANDING:
            return "force_landing";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_SEARCH_MODE:
            return "search";
        case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_ENGINE_START:
            return "engine_start";
        default:
            return "unknown";
    }
}

static const char *Ws_FlightStatusToStr(uint8_t status)
{
    switch (status) {
        case DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_STOPED:
            return "stopped";
        case DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_ON_GROUND:
            return "on_ground";
        case DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_IN_AIR:
            return "in_air";
        default:
            return "unknown";
    }
}

static void Ws_BuildAndSendTelemetry(int fd)
{
    T_DjiReturnCode ret;
    T_DjiDataTimestamp ts = {0};
    T_DjiFcSubscriptionQuaternion quat = {0};
    T_DjiFcSubscriptionVelocity velocity = {0};
    T_DjiFcSubscriptionPositionFused pos = {0};
    T_DjiFcSubscriptionGpsPosition gpsPos = {0};
    T_DjiFcSubscriptionGpsDetails gpsDetail = {0};
    T_DjiFcSubscriptionGpsSignalLevel gpsSignal = 0;
    T_DjiFcSubscriptionWholeBatteryInfo batteryWhole = {0};
    T_DjiFcSubscriptionSingleBatteryInfo batterySingle = {0};
    T_DjiFcSubscriptionAltitudeFused alt = 0;
    T_DjiFcSubscriptionFlightStatus flightStatus = 0xFF;
    T_DjiFcSubscriptionDisplaymode displayMode = 0xFF;
    T_DjiFcSubscriptionHomePointInfo home = {0};
    T_DjiFcSubscriptionHomePointSetStatus homeSet = 0;
    T_DjiFcSubscriptionGimbalAngles gimbalAngles = {0};
    T_DjiAircraftInfoBaseInfo aircraft = {0};
    double roll = 0, pitch = 0, yaw = 0;
    double gRoll = 0, gPitch = 0, gYaw = 0;
    int gimbalOk = 0;
    double lat = 0, lon = 0, altM = 0;
    double homeLat = 0, homeLon = 0;
    int sats = 0;
    int batPct = -1;
    double batVolt = -1.0;
    int velHealth = 0;
    int gpsOk = 0;
    int homeOk = 0;
    int armed = 0;
    int gpsSignalLevel = -1;
    int gpsFixState = -1;
    const char *flightMode = "unknown";
    const char *flightStatusStr = "unknown";
    const char *aircraftType = "unknown";
    char json[WS_SEND_BUF];
    char batPctStr[16];
    char batVoltStr[32];
    char homeLatStr[32];
    char homeLonStr[32];
    int n;

    (void) DjiAircraftInfo_GetBaseInfo(&aircraft);
    if (aircraft.aircraftType == DJI_AIRCRAFT_TYPE_M4TD) {
        aircraftType = "M4TD";
    } else if (aircraft.aircraftType == DJI_AIRCRAFT_TYPE_M4D) {
        aircraftType = "M4D";
    } else if (aircraft.aircraftType == DJI_AIRCRAFT_TYPE_M4T) {
        aircraftType = "M4T";
    } else if (aircraft.aircraftType == DJI_AIRCRAFT_TYPE_M4E) {
        aircraftType = "M4E";
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
                                                  (uint8_t *) &quat, sizeof(quat), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        Ws_QuatToEulerDeg(&quat, &roll, &pitch, &yaw);
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
                                                  (uint8_t *) &velocity, sizeof(velocity), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        velHealth = velocity.health;
    } else {
        memset(&velocity, 0, sizeof(velocity));
    }

    /* Position: prefer fused when horizontal looks valid; else GPS raw (deg*1e-7). */
    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                                                  (uint8_t *) &pos, sizeof(pos), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        lat = pos.latitude * 180.0 / M_PI;
        lon = pos.longitude * 180.0 / M_PI;
        altM = pos.altitude;
        sats = pos.visibleSatelliteNumber;
        if (sats > 0 && (fabs(lat) > 1e-5 || fabs(lon) > 1e-5)) {
            gpsOk = 1;
        }
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION,
                                                  (uint8_t *) &gpsPos, sizeof(gpsPos), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        /* x=lon, y=lat, unit: deg*1e-7; z=alt mm */
        double gpsLat = (double) gpsPos.y * 1e-7;
        double gpsLon = (double) gpsPos.x * 1e-7;
        double gpsAlt = (double) gpsPos.z * 1e-3;
        if (!gpsOk && (fabs(gpsLat) > 1e-5 || fabs(gpsLon) > 1e-5)) {
            lat = gpsLat;
            lon = gpsLon;
            if (gpsAlt != 0.0) {
                altM = gpsAlt;
            }
            gpsOk = 1;
        } else if (gpsOk && altM == 0.0 && gpsAlt != 0.0) {
            altM = gpsAlt;
        }
    }

    if (altM == 0.0) {
        ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED,
                                                      (uint8_t *) &alt, sizeof(alt), &ts);
        if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            altM = alt;
        }
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_DETAILS,
                                                  (uint8_t *) &gpsDetail, sizeof(gpsDetail), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        if (gpsDetail.totalSatelliteNumberUsed > 0) {
            sats = (int) gpsDetail.totalSatelliteNumberUsed;
        } else if (gpsDetail.gpsSatelliteNumberUsed > 0) {
            sats = (int) gpsDetail.gpsSatelliteNumberUsed;
        }
        gpsFixState = (int) gpsDetail.fixState;
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_SIGNAL_LEVEL,
                                                  (uint8_t *) &gpsSignal, sizeof(gpsSignal), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        gpsSignalLevel = (int) gpsSignal;
    }

    /* M4T: single battery index1 is more reliable than whole-battery topic. */
    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_SINGLE_INFO_INDEX1,
                                                  (uint8_t *) &batterySingle, sizeof(batterySingle), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS && batterySingle.batteryCapacityPercent <= 100) {
        batPct = batterySingle.batteryCapacityPercent;
        batVolt = (double) batterySingle.currentVoltage / 1000.0;
    } else {
        ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
                                                      (uint8_t *) &batteryWhole, sizeof(batteryWhole), &ts);
        if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            batPct = batteryWhole.percentage;
            batVolt = (double) batteryWhole.voltage / 1000.0;
        }
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT,
                                                  (uint8_t *) &flightStatus, sizeof(flightStatus), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        flightStatusStr = Ws_FlightStatusToStr(flightStatus);
        /* stopped = motors still; on_ground/in_air => motors active or flying */
        armed = (flightStatus != DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_STOPED) ? 1 : 0;
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE,
                                                  (uint8_t *) &displayMode, sizeof(displayMode), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        flightMode = Ws_DisplayModeToStr(displayMode);
        if (displayMode == DJI_FC_SUBSCRIPTION_DISPLAY_MODE_ENGINE_START) {
            armed = 1;
        }
    }

    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_SET_STATUS,
                                                  (uint8_t *) &homeSet, sizeof(homeSet), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS &&
        homeSet == DJI_FC_SUBSCRIPTION_HOME_POINT_SET_STATUS_SUCCESS) {
        ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_INFO,
                                                      (uint8_t *) &home, sizeof(home), &ts);
        if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            homeLat = home.latitude * 180.0 / M_PI;
            homeLon = home.longitude * 180.0 / M_PI;
            if (fabs(homeLat) > 1e-5 || fabs(homeLon) > 1e-5) {
                homeOk = 1;
            }
        }
    }

    /* Gimbal: official FC subscription TOPIC_GIMBAL_ANGLES, unit deg (pitch/roll/yaw). */
    ret = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
                                                  (uint8_t *) &gimbalAngles, sizeof(gimbalAngles), &ts);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        /* Vector3f: x=pitch, y=roll, z=yaw (see FC subscription gimbal angles). */
        gPitch = gimbalAngles.x;
        gRoll = gimbalAngles.y;
        gYaw = gimbalAngles.z;
        gimbalOk = 1;
    }

    if (batPct >= 0) {
        snprintf(batPctStr, sizeof(batPctStr), "%d", batPct);
    } else {
        snprintf(batPctStr, sizeof(batPctStr), "null");
    }
    if (batVolt >= 0.0) {
        snprintf(batVoltStr, sizeof(batVoltStr), "%.3f", batVolt);
    } else {
        snprintf(batVoltStr, sizeof(batVoltStr), "null");
    }
    if (homeOk) {
        snprintf(homeLatStr, sizeof(homeLatStr), "%.7f", homeLat);
        snprintf(homeLonStr, sizeof(homeLonStr), "%.7f", homeLon);
    } else {
        snprintf(homeLatStr, sizeof(homeLatStr), "null");
        snprintf(homeLonStr, sizeof(homeLonStr), "null");
    }

    s_seq++;
    {
        char sigStr[16];
        char fixStr[16];
        char dispStr[16];

        if (gpsSignalLevel >= 0) {
            snprintf(sigStr, sizeof(sigStr), "%d", gpsSignalLevel);
        } else {
            snprintf(sigStr, sizeof(sigStr), "null");
        }
        if (gpsFixState >= 0) {
            snprintf(fixStr, sizeof(fixStr), "%d", gpsFixState);
        } else {
            snprintf(fixStr, sizeof(fixStr), "null");
        }
        if (displayMode != 0xFF) {
            snprintf(dispStr, sizeof(dispStr), "%u", (unsigned) displayMode);
        } else {
            snprintf(dispStr, sizeof(dispStr), "null");
        }

        n = snprintf(json, sizeof(json),
                     "{"
                     "\"type\":\"telemetry\","
                     "\"ts\":%lld,"
                     "\"device_id\":\"%s\","
                     "\"payload\":{"
                     "\"seq\":%u,"
                     "\"aircraft_type\":\"%s\","
                     "\"flight_mode\":\"%s\","
                     "\"flight_status\":\"%s\","
                     "\"armed\":%s,"
                     "\"gps\":{"
                     "\"lat\":%.7f,\"lon\":%.7f,\"alt_m\":%.2f,\"satellites\":%d,"
                     "\"valid\":%s,\"signal_level\":%s,\"fix_state\":%s"
                     "},"
                     "\"attitude\":{\"roll_deg\":%.2f,\"pitch_deg\":%.2f,\"yaw_deg\":%.2f},"
                     "\"gimbal\":{\"pitch_deg\":%.2f,\"roll_deg\":%.2f,\"yaw_deg\":%.2f,\"valid\":%s},"
                     "\"velocity\":{\"vx_m_s\":%.3f,\"vy_m_s\":%.3f,\"vz_m_s\":%.3f,\"health\":%d},"
                     "\"battery\":{\"percent\":%s,\"voltage_v\":%s},"
                     "\"home\":{\"lat\":%s,\"lon\":%s,\"set\":%s},"
                     "\"extra\":{\"display_mode\":%s,\"gps_ok\":%s,\"gimbal_ok\":%s}"
                     "}"
                     "}",
                     (long long) Ws_NowMs(),
                     WS_TELEMETRY_DEVICE_ID,
                     (unsigned) s_seq,
                     aircraftType,
                     flightMode,
                     flightStatusStr,
                     armed ? "true" : "false",
                     lat, lon, altM, sats,
                     gpsOk ? "true" : "false",
                     sigStr, fixStr,
                     roll, pitch, yaw,
                     gPitch, gRoll, gYaw, gimbalOk ? "true" : "false",
                     velocity.data.x, velocity.data.y, velocity.data.z, velHealth,
                     batPctStr, batVoltStr,
                     homeLatStr, homeLonStr,
                     homeOk ? "true" : "false",
                     dispStr,
                     gpsOk ? "true" : "false",
                     gimbalOk ? "true" : "false");
    }

    if (n <= 0 || n >= (int) sizeof(json)) {
        USER_LOG_ERROR("ws telemetry json overflow n=%d", n);
        return;
    }

    if (Ws_SendText(fd, json) != 0) {
        USER_LOG_WARN("ws telemetry send failed");
    } else if ((s_seq % 5) == 0) {
        USER_LOG_INFO("ws telemetry seq=%u att_yaw=%.1f gimbal p=%.1f r=%.1f y=%.1f ok=%d bat=%s",
                      (unsigned) s_seq, yaw, gPitch, gRoll, gYaw, gimbalOk, batPctStr);
    }
}

static void Ws_SendHeartbeat(int fd)
{
    char json[256];
    long uptime = 0;

    if (s_startTime > 0) {
        uptime = (long) (time(NULL) - s_startTime);
    }
    snprintf(json, sizeof(json),
             "{\"type\":\"heartbeat\",\"ts\":%lld,\"device_id\":\"%s\","
             "\"payload\":{\"uptime_s\":%ld}}",
             (long long) Ws_NowMs(), WS_TELEMETRY_DEVICE_ID, uptime);
    if (Ws_SendText(fd, json) != 0) {
        USER_LOG_WARN("ws heartbeat send failed");
    }
}

static void *Ws_Task(void *arg)
{
    T_WsConn conn = {.fd = -1, .connected = 0};
    int backoffSec = 1;
    int tick = 0;
    T_DjiOsalHandler *osal = DjiPlatform_GetOsalHandler();

    USER_UTIL_UNUSED(arg);
    s_startTime = time(NULL);
    srand((unsigned) time(NULL) ^ (unsigned) getpid());

    USER_LOG_INFO("ws telemetry task start -> %s:%d%s device=%s",
                  WS_TELEMETRY_HOST, WS_TELEMETRY_PORT,
                  WS_TELEMETRY_WS_PATH, WS_TELEMETRY_DEVICE_ID);

    while (s_running) {
        if (!conn.connected) {
            pthread_mutex_lock(&s_sendMu);
            s_liveConn = NULL;
            pthread_mutex_unlock(&s_sendMu);
            s_rxLen = 0;
            if (Ws_ConnectHub(&conn) != 0) {
                USER_LOG_WARN("ws connect failed, retry in %ds", backoffSec);
                osal->TaskSleepMs((uint32_t) backoffSec * 1000);
                if (backoffSec < 30) {
                    backoffSec *= 2;
                }
                continue;
            }
            pthread_mutex_lock(&s_sendMu);
            s_liveConn = &conn;
            pthread_mutex_unlock(&s_sendMu);
            backoffSec = 1;
            tick = 0;
            s_rxLen = 0;
            USER_LOG_INFO("ws bidirectional ready (telemetry up + command down)");
        }

        if (Ws_PollIncoming(&conn) != 0) {
            USER_LOG_WARN("ws peer closed or rx error, reconnect");
            pthread_mutex_lock(&s_sendMu);
            s_liveConn = NULL;
            pthread_mutex_unlock(&s_sendMu);
            Ws_Close(&conn);
            s_rxLen = 0;
            continue;
        }

        Ws_BuildAndSendTelemetry(conn.fd);
        tick++;
        if ((tick % WS_TELEMETRY_HEARTBEAT_S) == 0) {
            Ws_SendHeartbeat(conn.fd);
        }

        osal->TaskSleepMs(1000 / WS_TELEMETRY_HZ);
    }

    pthread_mutex_lock(&s_sendMu);
    s_liveConn = NULL;
    pthread_mutex_unlock(&s_sendMu);
    Ws_Close(&conn);
    return NULL;
}

static void Ws_SubscribeOne(E_DjiFcSubscriptionTopic topic, E_DjiDataSubscriptionTopicFreq freq, const char *name)
{
    T_DjiReturnCode ret = DjiFcSubscription_SubscribeTopic(topic, freq, NULL);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("ws: subscribe %s rc=0x%08X (may already exist)", name, ret);
    }
}

static T_DjiReturnCode Ws_EnsureFcTopics(void)
{
    T_DjiReturnCode ret;

    /* If FC sample already called Init, this may fail — continue anyway. */
    ret = DjiFcSubscription_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("ws: FcSubscription_Init rc=0x%08X (reuse if already inited)", ret);
    }

    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
                    DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ, "quaternion");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
                    DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ, "velocity");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                    DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, "position_fused");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED,
                    DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, "altitude_fused");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION,
                    DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, "gps_position");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_GPS_DETAILS,
                    DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, "gps_details");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_GPS_SIGNAL_LEVEL,
                    DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, "gps_signal_level");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
                    DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, "battery_info");
    /* M4T / M4 series: single pack is more reliable */
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_SINGLE_INFO_INDEX1,
                    DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, "battery_single_1");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT,
                    DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, "status_flight");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE,
                    DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, "status_displaymode");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_SET_STATUS,
                    DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, "home_set_status");
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_INFO,
                    DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, "home_point_info");
    /* Gimbal angles @ up to 50Hz — good indoor test: move gimbal on RC and watch WS */
    Ws_SubscribeOne(DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
                    DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, "gimbal_angles");

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/* Exported -----------------------------------------------------------------*/
T_DjiReturnCode DjiTest_WsTelemetryStartService(void)
{
    T_DjiReturnCode ret;
    T_DjiOsalHandler *osal = DjiPlatform_GetOsalHandler();

    if (osal == NULL) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    ret = Ws_EnsureFcTopics();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return ret;
    }

    s_running = 1;
    if (osal->TaskCreate("ws_telemetry", Ws_Task, WS_TELEMETRY_TASK_STACK, NULL, &s_wsTask)
        != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("ws telemetry task create failed");
        s_running = 0;
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }

    USER_LOG_INFO("ws telemetry service started");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
