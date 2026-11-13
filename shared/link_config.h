// Communication timing defaults. Every value can be overridden from
// config/devices.h or with -D build flags.
#pragma once

#ifndef UFO_HEARTBEAT_INTERVAL_MS
#define UFO_HEARTBEAT_INTERVAL_MS 1000
#endif
#ifndef UFO_LINK_TIMEOUT_MS
#define UFO_LINK_TIMEOUT_MS 3000
#endif
#ifndef UFO_ACK_TIMEOUT_MS
#define UFO_ACK_TIMEOUT_MS 100
#endif
#ifndef UFO_MAX_RETRIES
#define UFO_MAX_RETRIES 3
#endif
// After the fast retries, device events keep being retried at this interval
// until the hub acknowledges them (or the session changes).
#ifndef UFO_EVENT_SLOW_RETRY_MS
#define UFO_EVENT_SLOW_RETRY_MS 500
#endif
// Hub -> PC: undelivered EVENT lines are re-sent at this interval until EVENT_ACK.
#ifndef UFO_PC_EVENT_RETRY_MS
#define UFO_PC_EVENT_RETRY_MS 500
#endif
// No line from the PC for this long -> the hub closes the session (devices go safe).
#ifndef UFO_PC_TIMEOUT_MS
#define UFO_PC_TIMEOUT_MS 3000
#endif
#ifndef UFO_SERIAL_MAX_LINE
#define UFO_SERIAL_MAX_LINE 512
#endif
