#ifndef SESSION_CORE_H
#define SESSION_CORE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SESSION_IDLE, SESSION_WARMUP, SESSION_BASELINE, SESSION_READY,
    SESSION_BLOWING, SESSION_MEASURING, SESSION_PURGING,
    SESSION_COMPLETED, SESSION_INVALID, SESSION_ERROR, SESSION_STATE_COUNT
} session_state_t;

/* Selected capture path, never evidence of a gas value or its validity. */
typedef enum {
    SESSION_SOURCE_NONE, SESSION_SOURCE_AUDIO_TEST, SESSION_SOURCE_SENSOR
} session_source_t;

typedef enum {
    SESSION_REASON_NONE, SESSION_REASON_COMPLETED, SESSION_REASON_AUDIO_ONLY,
    SESSION_REASON_CANCELLED, SESSION_REASON_INTERRUPTED,
    SESSION_REASON_SAMPLE_INVALID, SESSION_REASON_HARDWARE_FAULT,
    SESSION_REASON_TIMEOUT, SESSION_REASON_RETRY, SESSION_REASON_RESET
} session_reason_t;

typedef enum {
    SESSION_START_AUDIO_TEST, SESSION_START_SENSOR,
    SESSION_WARMUP_DONE, SESSION_BASELINE_DONE,
    SESSION_AUDIO_ACTIVITY, SESSION_BLOW_STARTED, SESSION_BLOW_FINISHED,
    SESSION_SENSOR_CAPTURE_DONE, SESSION_PURGE_DONE, SESSION_TICK,
    SESSION_CANCEL, SESSION_INTERRUPT, SESSION_SAMPLE_INVALID,
    SESSION_HARDWARE_ERROR, SESSION_RETRY, SESSION_RESET, SESSION_EVENT_COUNT
} session_event_t;

typedef enum {
    SESSION_ACCEPTED, SESSION_ILLEGAL_EVENT, SESSION_STALE_SESSION,
    SESSION_INVALID_TIME, SESSION_BAD_ARGUMENT, SESSION_ID_EXHAUSTED
} session_status_t;

typedef enum {
    SESSION_ACTION_NONE = 0,
    SESSION_ACTION_STOP = 1 << 0,
    SESSION_ACTION_WARMUP = 1 << 1,
    SESSION_ACTION_BASELINE = 1 << 2,
    SESSION_ACTION_AWAIT_BLOW = 1 << 3,
    SESSION_ACTION_CAPTURE = 1 << 4,
    SESSION_ACTION_MEASURE = 1 << 5,
    SESSION_ACTION_PURGE = 1 << 6,
    SESSION_ACTION_COMPLETED = 1 << 7
} session_action_t;

typedef struct {
    /* Active states require 1..INT32_MAX ms; idle/terminal states require 0. */
    uint32_t timeout_ms[SESSION_STATE_COUNT];
} session_config_t;

typedef struct {
    uint32_t session_id; /* 0 when idle; local to this initialized instance. */
    session_source_t source;
    session_state_t state;
    session_reason_t reason;
    uint32_t started_at_ms;       /* Meaningful when session_id != 0. */
    uint32_t state_entered_at_ms;
    uint32_t updated_at_ms;       /* Last accepted event, including TICK. */
    uint32_t ended_at_ms;         /* Meaningful only in a terminal state. */
} session_record_t;

/* Stack-allocatable, library-managed RAM. Do not edit fields after init. */
typedef struct {
    session_config_t config;
    uint32_t session_counter; /* Preserved by RESET; exhaustion is rejected. */
    session_record_t record;
} session_core_t;

typedef struct {
    session_status_t status;
    session_state_t previous_state;
    session_record_t record;
    uint32_t actions; /* Bitmask of intents, issued once per transition. */
} session_result_t;

/* Invalid configuration leaves core untouched. Init starts a new RAM lifetime. */
bool session_core_init(session_core_t *core, const session_config_t *config,
                       uint32_t now_ms);

/*
 * Serial calls only, after successful init. Pass the current record.session_id
 * (0 for START after init/RESET). All callbacks must retain their original ID.
 * now_ms is an injected uint32_t monotonic millisecond counter, wrapping modulo
 * 2^32; successive accepted calls must be <= INT32_MAX ms apart. Backwards or
 * ambiguous time is rejected. Tick regularly, including while awaiting drivers.
 * Rejections do not mutate core or emit actions. A due deadline instead accepts
 * a timeout transition before processing ordinary events, including late DONE.
 * Valid RESET/CANCEL/INTERRUPT/SAMPLE_INVALID/HARDWARE_ERROR take precedence.
 */
session_result_t session_core_step(session_core_t *core, session_event_t event,
                                   uint32_t session_id, uint32_t now_ms);

#endif
