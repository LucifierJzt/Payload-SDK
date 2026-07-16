/**
 * @file ws_telemetry_config.h
 * @brief Public WebSocket hub connection settings for Manifold 3.
 *
 * Change these before rebuild/redeploy if server or tokens change.
 */
#ifndef WS_TELEMETRY_CONFIG_H
#define WS_TELEMETRY_CONFIG_H

/* Hub: ws://1.13.253.227:6791/manifold-ws?...  (no /v1/ws suffix) */
#define WS_TELEMETRY_HOST           "1.13.253.227"
#define WS_TELEMETRY_PORT           6791
/* Full WS path (no query). Query device_id/token/role appended in code. */
#define WS_TELEMETRY_WS_PATH        "/manifold-ws"

/* Must match manifold-ws-hub .env */
#define WS_TELEMETRY_DEVICE_ID      "manifold-001"
#define WS_TELEMETRY_DEVICE_TOKEN   "e107c791b4a7f9e541a959f51d6470d2"

/* Report period */
#define WS_TELEMETRY_HZ             1
#define WS_TELEMETRY_HEARTBEAT_S    5

/*
 * Bidirectional command channel (viewer/cloud -> device).
 * Flight actions require payload.confirm == true.
 * 0 = only ping / status; 1 = allow flight samples via WS.
 */
#define WS_TELEMETRY_ALLOW_FLIGHT_CMD  1

/*
 * Indoor / no-fly simulation:
 * 1 = accept flight commands over WS, reply command_result, DO NOT call
 *     DjiTest_FlightControlRunSample (no takeoff/land/move).
 * 0 = real flight samples (outdoor only, RC ready).
 */
#define WS_TELEMETRY_FLIGHT_DRY_RUN    1

/* Gimbal control over same WS (M4T onboard: try PAYLOAD_PORT_NO1 = 1) */
#define WS_TELEMETRY_ALLOW_GIMBAL_CMD  1
#define WS_TELEMETRY_GIMBAL_MOUNT_POS  1

/*
 * Liveview (aircraft camera H264 on Manifold 3).
 * Doc "camera video stream transmission" = payload PUSH to Pilot.
 * Manifold PULLS stream via DjiLiveview_StartH264Stream, then optional RTMP.
 *
 * M4T sources: 1=VIS, 2=IR, 3=4K  (see dji_liveview.h M4T_*)
 * Position: 1 = PAYLOAD_PORT_NO1 (aircraft camera)
 */
#define WS_LIVEVIEW_ENABLE           1
#define WS_LIVEVIEW_CAMERA_POSITION  1
#define WS_LIVEVIEW_CAMERA_SOURCE    1
#define WS_LIVEVIEW_OUT_DIR          "/home/dji/live_out"
/*
 * Realtime push (needs ffmpeg on Manifold):
 * - rtmp://host:1935/live/key   → cloud player / OBS / flv.js
 * - udp://PC_LAN_IP:5000        → Mac: ffplay -fflags nobuffer udp://0.0.0.0:5000
 * live_start can override with payload.rtmp or payload.push_url
 */
#define WS_LIVEVIEW_PUSH_ENABLE      1
/* Default RTMP (stream key: manifold). Override via live_start payload.rtmp */
#define WS_LIVEVIEW_PUSH_URL         "rtmp://121.40.203.74/live/manifold"

#endif /* WS_TELEMETRY_CONFIG_H */
