/**
 * @file ws_liveview.c
 * @brief Start/stop aircraft camera H264 liveview on Manifold 3 (M4T).
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

static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int s_running = 0;
static volatile int s_inited = 0;
static FILE *s_fp = NULL;
static FILE *s_ffmpeg = NULL;
static char s_outPath[WS_LIVE_PATH_MAX];
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
        if ((s_cbCount % 100) == 0) {
            fflush(s_fp);
        }
    }
    if (s_ffmpeg != NULL) {
        n = fwrite(buf, 1, bufLen, s_ffmpeg);
        if (n != bufLen) {
            USER_LOG_WARN("ws live ffmpeg pipe short %zu/%u errno=%d", n, bufLen, errno);
        }
    }
    pthread_mutex_unlock(&s_mu);

    if ((s_cbCount % 200) == 1) {
        USER_LOG_INFO("ws live h264 cb#%u len=%u total_bytes=%llu",
                      (unsigned) s_cbCount, (unsigned) bufLen,
                      (unsigned long long) s_bytes);
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

T_DjiReturnCode WsLiveview_Start(void)
{
    T_DjiReturnCode ret;
    time_t now;
    struct tm tmNow;
    char cmd[512];

#if !WS_LIVEVIEW_ENABLE
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
#endif

    ret = WsLiveview_InitOnce();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return ret;
    }

    pthread_mutex_lock(&s_mu);
    if (s_running) {
        pthread_mutex_unlock(&s_mu);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    s_pos = (E_DjiLiveViewCameraPosition) WS_LIVEVIEW_CAMERA_POSITION;
    s_src = WsLiveview_PickSource();
    s_bytes = 0;
    s_cbCount = 0;

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
#if WS_LIVEVIEW_PUSH_RTMP
    if (WS_LIVEVIEW_RTMP_URL[0] != '\0') {
        /* Requires ffmpeg on Manifold; H264 annex-B from Liveview. */
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -loglevel warning -fflags nobuffer -f h264 -i pipe:0 "
                 "-c copy -f flv \"%s\" 2>/tmp/ws_live_ffmpeg.log",
                 WS_LIVEVIEW_RTMP_URL);
        s_ffmpeg = popen(cmd, "w");
        if (s_ffmpeg == NULL) {
            USER_LOG_WARN("ws live ffmpeg popen fail (file-only mode): %s", strerror(errno));
        } else {
            USER_LOG_INFO("ws live rtmp push started -> %s", WS_LIVEVIEW_RTMP_URL);
        }
    }
#endif

    pthread_mutex_unlock(&s_mu);

    USER_LOG_INFO("ws live start H264 pos=%d src=%d file=%s",
                  (int) s_pos, (int) s_src, s_outPath);

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
    USER_LOG_INFO("ws live streaming");
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

    USER_LOG_INFO("ws live stopped bytes=%llu cbs=%u file=%s",
                  (unsigned long long) s_bytes, (unsigned) s_cbCount, s_outPath);
    return ret;
}

int WsLiveview_IsRunning(void)
{
    return s_running;
}

void WsLiveview_GetStats(uint64_t *bytesOut, uint32_t *framesOut, char *pathBuf, size_t pathLen)
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
    pthread_mutex_unlock(&s_mu);
}
