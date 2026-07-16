/**
 * @file ws_telemetry.h
 * @brief FC telemetry uplink + bidirectional WS commands (cloud -> flight control).
 */
#ifndef WS_TELEMETRY_H
#define WS_TELEMETRY_H

#include "dji_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start FC polling, WS uplink, and command downlink handler.
 * @note Call after DjiCore_ApplicationStart(). Safe if FC subscription already inited.
 *
 * Downlink JSON (from hub/viewer):
 *   {"type":"command","req_id":"...","payload":{"name":"ping"}}
 *   {"type":"command","req_id":"...","payload":{"name":"takeoff_landing","confirm":true}}
 * Uplink result:
 *   {"type":"command_result","req_id":"...","payload":{"ok":true,"message":"..."}}
 */
T_DjiReturnCode DjiTest_WsTelemetryStartService(void);

#ifdef __cplusplus
}
#endif

#endif /* WS_TELEMETRY_H */
