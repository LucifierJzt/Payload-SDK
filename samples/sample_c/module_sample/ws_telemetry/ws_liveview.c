/**
 * @file ws_liveview.c
 * @brief Realtime H264 from aircraft camera; file + optional RTMP/UDP push via ffmpeg.
 */

#include "ws_liveview.h"
#include "ws_telemetry_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dji_liveview.h"
#include "dji_logger.h"
#include "dji_platform.h"
#include "dji_aircraft_info.h"
#include "utils/util_misc.h"

#define WS_LIVE_PATH_MAX  256
#define WS_LIVE_PUSH_MAX  320

static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int s_running = 0;
static volatile int s_inited = 0;
static FILE *s_fp = NULL;
static FILE *s_ffmpeg = NULL;
static char s_outPath[WS_LIVE_PATH_MAX];
static char s_pushUrl[WS_LIVE_PUSH_MAX];
static uint64_t s_bytes = 0;
static uint32_t s_cbCount = 0;
static E_DjiLiveViewCameraPosition s_pos;
static E_DjiLiveViewCameraSource s_src;

static void WsLiveview_H264Callback(E_DjiLiveViewCameraPosition position, const uint8_t *buf, uint32_t bufLen)
{
    size_t n;

    USER_UTIL_UNUSED(position);
    if (buf == NULL || bufLen == 0) {
        return;
    }

    pthread_mutex_lock(&s_mu);
    if (!s_running) {
        pthread_mutex_unlock(&s_mu);
        return;
    }
    s_cbCount++;
    s_bytes += bufLen;

    if (s_fp != NULL) {
        n = fwrite(buf, 1, bufLen, s_fp);
        if (n != bufLen) {
            USER_LOG_WARN("ws live fwrite short %zu/%u", n, bufLen);
        }
        if ((s_cbCount % 50) == 0) {
            fflush(s_fp);
        }
    }
    if (s_ffmpeg != NULL) {
        n = fwrite(buf, 1, bufLen, s_ffmpeg);
        if (n != bufLen) {
            USER_LOG_WARN("ws live ffmpeg pipe short %zu/%u errno=%d", n, bufLen, errno);
        } else if ((s_cbCount % 30) == 0) {
            fflush(s_ffmpeg);
        }
    }
    pthread_mutex_unlock(&s_mu);

    if ((s_cbCount % 150) == 1) {
        USER_LOG_INFO("ws live h264 cb#%u len=%u total=%llu push=%s",
                      (unsigned) s_cbCount, (unsigned) bufLen,
                      (unsigned long long) s_bytes,
                      s_pushUrl[0] ? s_pushUrl : "file-only");
    }
}

static E_DjiLiveViewCameraSource WsLiveview_PickSource(void)
{
    T_DjiAircraftInfoBaseInfo info = {0};

    if (DjiAircraftInfo_GetBaseInfo(&info) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return (E_DjiLiveViewCameraSource) WS_LIVEVIEW_CAMERA_SOURCE;
    }

    if (info.aircraftType == DJI_AIRCRAFT_TYPE_M4T ||
        info.aircraftType == DJI_AIRCRAFT_TYPE_M4TD) {
        if (WS_LIVEVIEW_CAMERA_SOURCE == 2) {
            return DJI_LIVEVIEW_CAMERA_SOURCE_M4T_IR;
        }
        if (WS_LIVEVIEW_CAMERA_SOURCE == 3) {
            return DJI_LIVEVIEW_CAMERA_SOURCE_M4T_4K;
        }
        return DJI_LIVEVIEW_CAMERA_SOURCE_M4T_VIS;
    }
    return (E_DjiLiveViewCameraSource) WS_LIVEVIEW_CAMERA_SOURCE;
}

static int WsLiveview_OpenPush(const char *url)
{
    char cmd[700];

    if (url == NULL || url[0] == '\0') {
        return 0;
    }
    if (access("/usr/bin/ffmpeg", X_OK) != 0 && access("/bin/ffmpeg", X_OK) != 0 &&
        access("/usr/local/bin/ffmpeg", X_OK) != 0) {
        /* still try PATH */
        USER_LOG_WARN("ws live: ffmpeg may be missing on PATH");
    }

    if (strncmp(url, "rtmp://", 7) == 0 || strncmp(url, "rtmps://", 8) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -hide_banner -loglevel warning -fflags nobuffer -flags low_delay "
                 "-f h264 -i pipe:0 -c:v copy -an -f flv \"%s\" 2>>/tmp/ws_live_ffmpeg.log",
                 url);
    } else if (strncmp(url, "udp://", 6) == 0) {
        /* LAN realtime: Mac runs ffplay udp://0.0.0.0:PORT */
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -hide_banner -loglevel warning -fflags nobuffer -flags low_delay "
                 "-f h264 -i pipe:0 -c:v copy -an -f mpegts \"%s?pkt_size=1316\" "
                 "2>>/tmp/ws_live_ffmpeg.log",
                 url);
    } else if (strncmp(url, "tcp://", 6) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -hide_banner -loglevel warning -fflags nobuffer -flags low_delay "
                 "-f h264 -i pipe:0 -c:v copy -an -f mpegts \"%s\" "
                 "2>>/tmp/ws_live_ffmpeg.log",
                 url);
    } else {
        USER_LOG_ERROR("ws live unsupported push url (use rtmp:// or udp://): %s", url);
        return -1;
    }

    s_ffmpeg = popen(cmd, "w");
    if (s_ffmpeg == NULL) {
        USER_LOG_ERROR("ws live popen ffmpeg failed: %s", strerror(errno));
        return -1;
    }
    /* line-buffered-ish */
    setvbuf(s_ffmpeg, NULL, _IONBF, 0);
    USER_LOG_INFO("ws live push pipeline -> %s", url);
    return 0;
}

T_DjiReturnCode WsLiveview_InitOnce(void)
{
    T_DjiReturnCode ret;

    pthread_mutex_lock(&s_mu);
    if (s_inited) {
        pthread_mutex_unlock(&s_mu);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    ret = DjiLiveview_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        pthread_mutex_unlock(&s_mu);
        USER_LOG_ERROR("ws liveview init fail 0x%08X", (unsigned) ret);
        return ret;
    }
    s_inited = 1;
    pthread_mutex_unlock(&s_mu);
    USER_LOG_INFO("ws liveview module init ok");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode WsLiveview_Start(const char *pushUrl)
{
    T_DjiReturnCode ret;
    time_t now;
    struct tm tmNow;
    char effectivePush[WS_LIVE_PUSH_MAX];

#if !WS_LIVEVIEW_ENABLE
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
#endif

    ret = WsLiveview_InitOnce();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return ret;
    }

    /* already running: restart if new push requested */
    if (s_running) {
        (void) WsLiveview_Stop();
    }

    effectivePush[0] = '\0';
    if (pushUrl != NULL && pushUrl[0] != '\0') {
        snprintf(effectivePush, sizeof(effectivePush), "%s", pushUrl);
    } else if (WS_LIVEVIEW_PUSH_ENABLE && WS_LIVEVIEW_PUSH_URL[0] != '\0') {
        snprintf(effectivePush, sizeof(effectivePush), "%s", WS_LIVEVIEW_PUSH_URL);
    }

    pthread_mutex_lock(&s_mu);
    s_pos = (E_DjiLiveViewCameraPosition) WS_LIVEVIEW_CAMERA_POSITION;
    s_src = WsLiveview_PickSource();
    s_bytes = 0;
    s_cbCount = 0;
    snprintf(s_pushUrl, sizeof(s_pushUrl), "%s", effectivePush);

    (void) mkdir(WS_LIVEVIEW_OUT_DIR, 0755);
    now = time(NULL);
    localtime_r(&now, &tmNow);
    snprintf(s_outPath, sizeof(s_outPath),
             "%s/live_m4t_%04d%02d%02d_%02d%02d%02d.h264",
             WS_LIVEVIEW_OUT_DIR,
             tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
             tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

    s_fp = fopen(s_outPath, "wb");
    if (s_fp == NULL) {
        USER_LOG_ERROR("ws live open file fail %s errno=%d", s_outPath, errno);
        pthread_mutex_unlock(&s_mu);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    s_ffmpeg = NULL;
    if (s_pushUrl[0] != '\0') {
        if (WsLiveview_OpenPush(s_pushUrl) != 0) {
            USER_LOG_WARN("ws live continue file-only (push failed)");
            s_pushUrl[0] = '\0';
        }
    }

    pthread_mutex_unlock(&s_mu);

    USER_LOG_INFO("ws live start H264 pos=%d src=%d file=%s push=%s",
                  (int) s_pos, (int) s_src, s_outPath,
                  s_pushUrl[0] ? s_pushUrl : "(none)");

    ret = DjiLiveview_StartH264Stream(s_pos, s_src, WsLiveview_H264Callback);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("ws live StartH264Stream fail 0x%08X", (unsigned) ret);
        pthread_mutex_lock(&s_mu);
        if (s_fp) {
            fclose(s_fp);
            s_fp = NULL;
        }
        if (s_ffmpeg) {
            pclose(s_ffmpeg);
            s_ffmpeg = NULL;
        }
        pthread_mutex_unlock(&s_mu);
        return ret;
    }

    pthread_mutex_lock(&s_mu);
    s_running = 1;
    pthread_mutex_unlock(&s_mu);
    USER_LOG_INFO("ws live streaming (realtime capture%s)",
                  s_pushUrl[0] ? " + push" : ", file only");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode WsLiveview_Stop(void)
{
    T_DjiReturnCode ret = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;

    pthread_mutex_lock(&s_mu);
    if (!s_running) {
        pthread_mutex_unlock(&s_mu);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    s_running = 0;
    pthread_mutex_unlock(&s_mu);

    ret = DjiLiveview_StopH264Stream(s_pos, s_src);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("ws live StopH264Stream 0x%08X", (unsigned) ret);
    }

    pthread_mutex_lock(&s_mu);
    if (s_fp) {
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    if (s_ffmpeg) {
        pclose(s_ffmpeg);
        s_ffmpeg = NULL;
    }
    pthread_mutex_unlock(&s_mu);

    USER_LOG_INFO("ws live stopped bytes=%llu cbs=%u",
                  (unsigned long long) s_bytes, (unsigned) s_cbCount);
    return ret;
}

int WsLiveview_IsRunning(void)
{
    return s_running;
}

void WsLiveview_GetStats(uint64_t *bytesOut, uint32_t *framesOut,
                         char *pathBuf, size_t pathLen,
                         char *pushBuf, size_t pushLen)
{
    pthread_mutex_lock(&s_mu);
    if (bytesOut) {
        *bytesOut = s_bytes;
    }
    if (framesOut) {
        *framesOut = s_cbCount;
    }
    if (pathBuf && pathLen > 0) {
        snprintf(pathBuf, pathLen, "%s", s_outPath);
    }
    if (pushBuf && pushLen > 0) {
        snprintf(pushBuf, pushLen, "%s", s_pushUrl);
    }
    pthread_mutex_unlock(&s_mu);
}
