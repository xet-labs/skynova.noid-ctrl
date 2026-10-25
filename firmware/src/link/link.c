/*
 * link.c - UART0 frame link: PING/PONG, HELLO at the start of a session, session timeout.
 *
 * Purpose : give the Pi a heartbeat it can measure (ping_test) and a way to know the board is alive
 *           (HELLO). The session rules are pure and tested (lib/skynoid_link); this file only owns the
 *           clock, the sequence counter and the calls into the rest of the firmware.
 * Called by: console_uart.c (link_on_frame, link_tick), any task (link_send).
 * Test    : on the board: python -m skynoid.tools.ping_test --port ..., expect 200 of 200 PONGs.
 * Spec    : protocol-v0.md sections 3 and 9. Not run on hardware yet.
 *
 * Rules kept here:
 *   - silent until spoken to: nothing binary is sent unless a PING arrived (the one HELLO at boot of the
 *     spec is NOT sent yet, see docs/steps/step-1.4-link.md "Known limits").
 *   - the first PING of a session is answered HELLO, then PONG; later PINGs only PONG.
 *   - 500 ms without a PING ends the session: base_link_lost() brakes and goes SAFE.
 *   - a CMD frame goes to the dispatcher (lib/skynoid_dispatch, step 1.5a) which calls the SAME base_cmd_*
 *     functions as the console. CHOICE OF THIS FIRMWARE, not in the spec: arm and drive without a session
 *     (no PING yet) are refused with NACK busy, because only a session has the 500 ms link-loss timer.
 *   - events from the base are queued without blocking and sent only while a session runs; an event that
 *     happens between sessions (link_lost, cliff) waits for the next session. 16 deep, oldest dropped.
 *   - telemetry (TLM base_status) flows only after a subscribe, once per period, only during a session.
 */
#include <string.h>

#include "esp_attr.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "app_config.h"
#include "base.h"
#include "core/logger.h"
#include "link/link.h"
#include "skynoid_dispatch.h"
#include "skynoid_link.h"
#include "skynoid_wire.h"

static sklk_session_t s_session;
static uint16_t s_seq;                         /* node frame counter, wraps at 65535 */
static sklk_hello_t s_hello;
static skd_stream_t s_stream;                  /* telemetry stream timer (console task only) */
static QueueHandle_t s_evq;                    /* base events waiting for a session */
static volatile uint32_t s_events_dropped;
static uint32_t s_events_dropped_seen;

#define EVENT_QUEUE_LEN 16
#define EVENTS_PER_TICK 4                      /* at most this many EVT frames per link_tick */

/* Boot counter in RTC noinit RAM: counts resets since the last power-on (random RAM at power-on is
 * recognised by the magic and restarts at 1). It is NOT a lifetime counter; that needs NVS (step 1.6). */
#define BOOT_MAGIC 0x534B4E31u                 /* "SKN1" */
static RTC_NOINIT_ATTR uint32_t s_boot_magic;
static RTC_NOINIT_ATTR uint32_t s_boot_count;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static uint8_t reset_code(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return 1;
    case ESP_RST_BROWNOUT:  return 2;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return 3;
    case ESP_RST_SW:        return 4;
    case ESP_RST_PANIC:     return 5;
    default:                return 0;
    }
}

/* ---- the base functions the dispatcher may call (same ones as the console) ---- */
static int op_arm(void *c)         { (void)c; return base_cmd_arm(); }
static int op_disarm(void *c)      { (void)c; return base_cmd_disarm(); }
static int op_estop(void *c)       { (void)c; return base_cmd_estop(); }
static int op_clear_estop(void *c) { (void)c; return base_cmd_clear_estop(); }
static int op_stop(void *c)        { (void)c; return base_cmd_stop(); }
static int op_cal_floor(void *c)   { (void)c; return base_cmd_cal_floor(); }
static int op_drive(void *c, int32_t v, int32_t w, uint32_t ttl) { (void)c; return base_cmd_drive(v, w, ttl); }
static int op_set_param(void *c, uint8_t id, int32_t v) { (void)c; return base_param_set(id, v); }
static uint16_t op_detail(void *c) { (void)c; return base_last_detail(); }

static const skd_ops_t s_ops = {
    .ctx = NULL, .arm = op_arm, .disarm = op_disarm, .estop = op_estop, .clear_estop = op_clear_estop,
    .drive = op_drive, .stop = op_stop, .cal_floor = op_cal_floor, .set_param = op_set_param,
    .last_detail = op_detail,
};

/* Called from the base safety task: must not block. Keeps the newest events (drops the oldest). */
static void on_base_event(const skb_event_t *ev)
{
    if (s_evq == NULL) { return; }
    if (xQueueSend(s_evq, ev, 0) != pdTRUE) {
        skb_event_t old;
        (void)xQueueReceive(s_evq, &old, 0);
        s_events_dropped++;
        (void)xQueueSend(s_evq, ev, 0);
    }
}

void link_init(void)
{
    sklk_init(&s_session);
    s_seq = 0;
    skd_stream_init(&s_stream);
    s_evq = xQueueCreate(EVENT_QUEUE_LEN, sizeof(skb_event_t));
    base_set_event_sink(on_base_event);
    if (s_boot_magic != BOOT_MAGIC) { s_boot_magic = BOOT_MAGIC; s_boot_count = 0; }
    s_boot_count++;
    memset(&s_hello, 0, sizeof s_hello);
    s_hello.role = ROLE_SINGLE;
    s_hello.caps = FW_CAPS;
    s_hello.fw_major = FW_MAJOR;
    s_hello.fw_minor = FW_MINOR;
    s_hello.proto = PROTO_VERSION;
    s_hello.reset_reason = reset_code(esp_reset_reason());
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    memcpy(s_hello.chip_id, mac, sizeof s_hello.chip_id);
    s_hello.boot_count = (uint16_t)s_boot_count;
    s_hello.schema_hash = 0;
}

bool link_session_active(void) { return s_session.active; }

bool link_send(uint8_t type, uint8_t msg, const uint8_t *payload, uint16_t len, uint16_t ack)
{
    if (len > SKW_MAX_PAYLOAD || (len > 0 && payload == NULL)) { return false; }
    skw_frame_t f;
    memset(&f, 0, sizeof f);
    f.type = type;
    f.msg = msg;
    f.ack = ack;
    f.len = len;
    if (len > 0) { memcpy(f.payload, payload, len); }
    f.seq = __atomic_fetch_add(&s_seq, 1, __ATOMIC_RELAXED);
    uint8_t wire[SKW_MAX_WIRE];
    const size_t n = skw_frame_encode(&f, wire, sizeof wire);
    if (n == 0) { return false; }
    return logger_write_bin(wire, n);          /* one writer for the UART: ordered with the text */
}

static void on_ping(const skw_frame_t *f)
{
    uint32_t host_t;
    if (!sklk_parse_ping(f->payload, f->len, &host_t)) {
        const uint8_t detail[3] = { NACK_BAD_LEN, (uint8_t)SKLK_PING_LEN, 0 };
        link_send(SKW_T_NACK, f->msg, detail, sizeof detail, f->seq);
        return;
    }
    const uint32_t t = now_ms();
    if (sklk_on_ping(&s_session, t)) {
        skd_stream_stop(&s_stream);            /* a new session starts with no telemetry until it subscribes */
        uint8_t hello[SKLK_HELLO_LEN];
        sklk_build_hello(hello, &s_hello);
        link_send(SKW_T_HELLO, 1, hello, SKLK_HELLO_LEN, 0);
        LOGI("session started");
    }
    uint8_t pong[SKLK_PONG_LEN];
    sklk_build_pong(pong, host_t, t, base_hazard());
    link_send(SKW_T_PONG, 1, pong, SKLK_PONG_LEN, f->seq);
}

static void send_reply(const skw_frame_t *f, const skd_reply_t *r)
{
    if (r->is_ack) {
        link_send(SKW_T_ACK, f->msg, NULL, 0, f->seq);
    } else {
        uint8_t d[3];
        skd_build_nack(d, r->code, r->detail);
        link_send(SKW_T_NACK, f->msg, d, sizeof d, f->seq);
    }
}

static void on_cmd(const skw_frame_t *f)
{
    skd_reply_t r;
    if (!s_session.active && (f->msg == SKD_CMD_ARM || f->msg == SKD_CMD_DRIVE)) {
        r.is_ack = false; r.code = SKB_NACK_BUSY; r.detail = 0;       /* no session, no link-loss timer */
    } else {
        skd_handle_cmd(&s_ops, &s_stream, f->msg, f->payload, f->len, now_ms(), &r);
    }
    send_reply(f, &r);
}

void link_on_frame(const skw_frame_t *f)
{
    switch (f->type) {
    case SKW_T_PING:
        on_ping(f);
        break;
    case SKW_T_CMD:
        on_cmd(f);
        break;
    default:                                   /* node-to-host types or CONFIG: ignored */
        break;
    }
}

static void send_events(void)
{
    for (int i = 0; i < EVENTS_PER_TICK; i++) {
        skb_event_t ev;
        if (xQueuePeek(s_evq, &ev, 0) != pdTRUE) { return; }
        uint8_t payload[2];
        uint8_t msg = 0;
        const int n = skd_build_event(payload, &msg, &ev);
        if (n >= 0 && !link_send(SKW_T_EVT, msg, payload, (uint16_t)n, 0)) { return; }   /* queue full: retry next tick */
        (void)xQueueReceive(s_evq, &ev, 0);                                             /* sent (or no wire form): remove */
    }
}

static void send_status(void)
{
    base_status_t st;
    base_get_status(&st);
    skd_status_t ds = { .hazard = st.hazard, .range_mm = st.range_mm, .range_status = st.range_status,
                        .batt_mv = st.batt_mv, .motor_ma = st.motor_ma, .state = st.state };
    for (int i = 0; i < 4; i++) { ds.ir_raw[i] = st.ir_raw[i]; }
    uint8_t p[SKD_STATUS_LEN];
    skd_build_status(p, &ds);
    link_send(SKW_T_TLM, 0x10, p, SKD_STATUS_LEN, 0);
}

void link_tick(void)
{
    if (sklk_tick(&s_session, now_ms(), LINK_TIMEOUT_MS) == SKLK_TICK_SESSION_ENDED) {
        skd_stream_stop(&s_stream);            /* spec section 9: stop streaming */
        base_link_lost();                      /* brake, SAFE (spec R3); the link_lost event waits for the next session */
        LOGW("link lost: no PING for %d ms", LINK_TIMEOUT_MS);
        return;
    }
    if (!s_session.active) { return; }
    const uint32_t dropped = s_events_dropped;
    if (dropped != s_events_dropped_seen) {
        s_events_dropped_seen = dropped;
        LOGW("event queue overflowed: %u dropped so far", (unsigned)dropped);
    }
    send_events();
    if (skd_stream_due(&s_stream, now_ms())) { send_status(); }
}
