/**
 * @file ws_liveview.h
 * @brief Aircraft camera H264 via DjiLiveview; optional realtime push (RTMP/UDP).
 */
#ifndef WS_LIVEVIEW_H
#define WS_LIVEVIEW_H

#include <stddef.h>
#include <stdint.h>
#include "dji_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

T_DjiReturnCode WsLiveview_InitOnce(void);

/**
 * @param pushUrl optional. Examples:
 *   rtmp://host:1935/live/key
 *   udp://192.168.31.10:5000
 *   NULL / "" → use config default if WS_LIVEVIEW_PUSH_ENABLE, else file-only
 */
T_DjiReturnCode WsLiveview_Start(const char *pushUrl);
T_DjiReturnCode WsLiveview_Stop(void);
int WsLiveview_IsRunning(void);
void WsLiveview_GetStats(uint64_t *bytesOut, uint32_t *framesOut,
                         char *pathBuf, size_t pathLen,
                         char *pushBuf, size_t pushLen);

#ifdef __cplusplus
}
#endif

#endif /* WS_LIVEVIEW_H */
