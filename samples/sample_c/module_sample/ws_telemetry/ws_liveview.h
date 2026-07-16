/**
 * @file ws_liveview.h
 * @brief Aircraft camera H264 via DjiLiveview; start/stop from WS commands.
 *
 * Note: DJI "camera video stream transmission" doc is for payload-as-camera
 * pushing to Pilot. Manifold 3 on M4T uses Liveview to pull aircraft stream.
 */
#ifndef WS_LIVEVIEW_H
#define WS_LIVEVIEW_H

#include "dji_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

T_DjiReturnCode WsLiveview_InitOnce(void);
T_DjiReturnCode WsLiveview_Start(void);
T_DjiReturnCode WsLiveview_Stop(void);
int WsLiveview_IsRunning(void);
void WsLiveview_GetStats(uint64_t *bytesOut, uint32_t *framesOut, char *pathBuf, size_t pathLen);

#ifdef __cplusplus
}
#endif

#endif /* WS_LIVEVIEW_H */
