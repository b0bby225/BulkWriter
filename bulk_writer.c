/**
 * Bulk Writer ΓÇö Flipper Zero 125kHz Tag Reprogrammer
 *
 * Reads 125kHz LF tags, substitutes the facility code with a user-preset
 * value, and writes back to T5577-based cards. Designed for hands-free
 * bulk processing with LED/vibration feedback per card.
 *
 * Architecture mirrors ClayLoop: ViewPort + input callback + draw callback,
 * state machine driven by input events, persistent settings via FlipperFormat.
 *
 * Author: Bobby Gibbs (@b0bby225)
 * License: MIT
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <lib/lfrfid/lfrfid_worker.h>
#include <lib/lfrfid/protocols/lfrfid_protocols.h>
#include <lib/bit_lib/bit_lib.h>

#define TAG "BulkWriter"

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  CONSTANTS & DEFAULTS
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

/** Persistent settings file path (same pattern as ClayLoop) */
#define BULKWRITER_CONFIG_DIR  "/ext/apps_data/bulk_writer"
#define BULKWRITER_CONFIG_PATH "/ext/apps_data/bulk_writer/config.ff"

/** Facility code bounds */
#define FC_MIN 0
#define FC_MAX 255
#define FC_DEFAULT 1

/** Card number mode */
typedef enum {
    CardNumMode_Preserve = 0,  /** Keep original card number */
    CardNumMode_Sequential,     /** Assign sequential numbers starting from base */
    CardNumMode_Fixed,          /** Write a fixed card number */
    CardNumMode_COUNT
} CardNumMode;

#define CARD_NUM_DEFAULT      0
#define CARD_NUM_MODE_DEFAULT CardNumMode_Preserve

/** Modulation selection ΓÇö determines LFRFIDWorkerReadType */
typedef enum {
    Mod_Auto = 0,    /** Auto-detect (cycles ASK+PSK) */
    Mod_ASK,          /** ASK only (HID, EM4100, most common) */
    Mod_PSK,          /** PSK only (Indala, AWID, etc.) */
    Mod_COUNT
} ModSelect;

#define MOD_DEFAULT Mod_Auto

/** Generic protocol: treat data[0] as FC, data[1..2] or data[3..4] as CN */
static bool generic_extract(
    const uint8_t* data,
    size_t data_size,
    uint8_t* fc_out,
    uint16_t* cn_out) {
    if(data_size < 2) return false;
    *fc_out = data[0];
    if(data_size >= 5) {
        *cn_out = ((uint16_t)data[3] << 8) | data[4];
    } else if(data_size >= 3) {
        *cn_out = ((uint16_t)data[1] << 8) | data[2];
    } else {
        *cn_out = data[1];
    }
    return true;
}

static bool generic_encode(
    uint8_t* data,
    size_t data_size,
    uint8_t fc,
    uint16_t cn) {
    if(data_size < 2) return false;
    data[0] = fc;
    if(data_size >= 5) {
        data[3] = (cn >> 8) & 0xFF;
        data[4] = cn & 0xFF;
    } else if(data_size >= 3) {
        data[1] = (cn >> 8) & 0xFF;
        data[2] = cn & 0xFF;
    } else {
        data[1] = cn & 0xFF;
    }
    return true;
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  APPLICATION SCREENS (state machine ΓÇö same pattern as ClayLoop)
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

typedef enum {
    Screen_Config,       /** FC, card number mode, modulation selection */
    Screen_RefScan,      /** "Scan Reference Card" ΓÇö learning modulation + protocol */
    Screen_RefResult,    /** Show what was detected from reference scan */
    Screen_Ready,        /** "Place card on reader" ΓÇö idle waiting */
    Screen_Reading,      /** Active read in progress */
    Screen_Writing,      /** Active write in progress */
    Screen_Success,      /** Card written OK ΓÇö brief flash */
    Screen_Error,        /** Write failed ΓÇö brief flash */
    Screen_Summary,      /** Session stats */
} AppScreen;

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  APPLICATION STATE
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

typedef struct {
    /* GUI */
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;

    /* Notifications (LED, vibro, speaker) */
    NotificationApp* notifications;

    /* LFRFID */
    LFRFIDWorker* lf_worker;
    ProtocolDict* protocol_dict;

    /* Current screen state */
    AppScreen current_screen;

    /* Config (persisted) */
    uint8_t facility_code;
    CardNumMode card_num_mode;
    uint16_t card_num_base;       /** Base for sequential or fixed value */
    ModSelect mod_select;          /** ASK / PSK / Auto modulation */

    /* Config screen cursor */
    uint8_t config_cursor;        /** Which config field is selected */
    uint16_t repeat_count;        /** Consecutive repeat events for acceleration */

    /* Reference scan state */
    bool ref_scanned;              /** True if a reference card was scanned */
    LFRFIDProtocol ref_protocol;   /** Protocol learned from reference scan */
    LFRFIDWorkerReadType ref_read_type; /** Read type learned (ASK/PSK) */
    char ref_proto_name[32];       /** Human-readable protocol name from reference */
    uint8_t ref_fc;                /** FC extracted from reference card */
    uint16_t ref_cn;               /** CN extracted from reference card */

    /* Runtime state */
    bool running;                 /** Main processing loop active */
    bool worker_busy;             /** LFRFID worker callback in progress */

    /* Last read data */
    LFRFIDProtocol last_protocol; /** Protocol detected on last read */
    uint8_t last_data[16];        /** Raw protocol data from last read */
    size_t last_data_size;

    /* Session counters */
    uint16_t cards_written;
    uint16_t cards_failed;
    uint16_t next_sequential;     /** Next card number for sequential mode */
} BulkWriterApp;

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  LED / NOTIFICATION SEQUENCES
 *  (Same approach as ClayLoop ΓÇö predefined notification_message sequences)
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

/** Green blink + short vibro = card written OK (fast turnaround) */
static const NotificationSequence seq_success = {
    &message_green_255,
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_delay_250,
    &message_green_0,
    NULL,
};

/** Red blink + double vibro = write failed (fast turnaround) */
static const NotificationSequence seq_error = {
    &message_red_255,
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_delay_100,
    &message_red_0,
    NULL,
};

/** Purple (red+blue) pulse = writing */
static const NotificationSequence seq_writing = {
    &message_red_255,
    &message_blue_255,
    &message_delay_250,
    &message_red_0,
    &message_blue_0,
    NULL,
};

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  SPEAKER BEEP HELPERS (same approach as ClayLoop)
 *
 *  Success: ascending double beep (880 Hz ΓåÆ 1100 Hz) ΓÇö cheerful
 *  Error:   descending double buzz (440 Hz ΓåÆ 220 Hz) ΓÇö unmistakably bad
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static void beep_success(void) {
    if(furi_hal_speaker_acquire(100)) {
        /* First tone: 880 Hz for 60ms */
        furi_hal_speaker_start(880.0f, 1.0f);
        furi_delay_ms(60);
        furi_hal_speaker_stop();
        furi_delay_ms(30);
        /* Second tone: 1100 Hz for 60ms */
        furi_hal_speaker_start(1100.0f, 1.0f);
        furi_delay_ms(60);
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
}

static void beep_error(void) {
    if(furi_hal_speaker_acquire(100)) {
        /* First tone: 440 Hz for 80ms */
        furi_hal_speaker_start(440.0f, 1.0f);
        furi_delay_ms(80);
        furi_hal_speaker_stop();
        furi_delay_ms(30);
        /* Second tone: 220 Hz for 120ms ΓÇö low buzz */
        furi_hal_speaker_start(220.0f, 1.0f);
        furi_delay_ms(120);
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  FORWARD DECLARATIONS
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static void app_draw_callback(Canvas* canvas, void* context);
static void app_input_callback(InputEvent* input_event, void* context);
static void app_save_settings(BulkWriterApp* app);
static void app_load_settings(BulkWriterApp* app);
static void app_draw_config(Canvas* canvas, BulkWriterApp* app);
static void app_draw_ref_scan(Canvas* canvas, BulkWriterApp* app);
static void app_draw_ref_result(Canvas* canvas, BulkWriterApp* app);
static void app_draw_ready(Canvas* canvas, BulkWriterApp* app);
static void app_draw_processing(Canvas* canvas, BulkWriterApp* app);
static void app_draw_result(Canvas* canvas, BulkWriterApp* app);
static void app_draw_summary(Canvas* canvas, BulkWriterApp* app);
static LFRFIDWorkerReadType app_get_read_type(BulkWriterApp* app);

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  PROTOCOL HELPERS
 *
 *  HID H10301 (26-bit): [1 parity][8 FC][16 CN][1 parity]
 *  EM4100:              [8-bit version/FC][32-bit ID] with row/col parity
 *
 *  These helpers extract/inject facility code and card number from the
 *  raw protocol data bytes stored by the LFRFID protocol dictionary.
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

/**
 * Extract facility code and card number from HID H10301 26-bit data.
 * Flipper firmware stores decoded H10301 as 3 clean bytes:
 *   data[0] = FC,  data[1] = CN high byte,  data[2] = CN low byte
 * Wiegand parity is handled by the firmware's protocol encoder/decoder.
 */
static bool hid_h10301_extract(
    const uint8_t* data,
    size_t data_size,
    uint8_t* fc_out,
    uint16_t* cn_out) {
    if(data_size < 3) return false;
    *fc_out = data[0];
    *cn_out = ((uint16_t)data[1] << 8) | data[2];
    return true;
}

/**
 * Encode HID H10301 data with new FC and CN.
 * Wiegand parity is recalculated by the firmware when writing to T5577.
 */
static bool hid_h10301_encode(
    uint8_t* data,
    size_t data_size,
    uint8_t fc,
    uint16_t cn) {
    if(data_size < 3) return false;
    data[0] = fc;
    data[1] = (cn >> 8) & 0xFF;
    data[2] = cn & 0xFF;
    return true;
}

/**
 * Extract facility code and card number from EM4100 data.
 * Flipper firmware stores EM4100 as 5 bytes of the 40-bit ID.
 * The firmware's render_data shows:  FC = data[2],  CN = data[3..4]
 * data[0..1] are upper ID bytes (preserved, not displayed as FC).
 */
static bool em4100_extract(
    const uint8_t* data,
    size_t data_size,
    uint8_t* fc_out,
    uint16_t* cn_out) {
    if(data_size < 5) return false;

    *fc_out = data[2];
    *cn_out = ((uint16_t)data[3] << 8) | data[4];
    return true;
}

/**
 * Encode EM4100 data with new FC and CN.
 * Preserves data[0..1] (upper ID bytes) from the original card.
 */
static bool em4100_encode(
    uint8_t* data,
    size_t data_size,
    uint8_t fc,
    uint16_t cn) {
    if(data_size < 5) return false;

    data[2] = fc;
    data[3] = (cn >> 8) & 0xFF;
    data[4] = cn & 0xFF;
    return true;
}

/**
 * Indala26: 4 bytes (28 decoded bits). FC and CN are in scattered bit positions.
 * Bit map from Flipper firmware protocol_indala26.c get_fc/get_cn.
 */
static const uint8_t indala26_fc_bits[8] = {24, 16, 11, 14, 15, 20, 6, 25};
static const uint8_t indala26_cn_bits[16] = {9, 12, 10, 7, 19, 3, 2, 18, 13, 0, 4, 21, 23, 26, 17, 8};

static bool indala26_extract(const uint8_t* data, size_t data_size, uint8_t* fc_out, uint16_t* cn_out) {
    if(data_size < 4) return false;
    uint8_t fc = 0;
    for(int i = 0; i < 8; i++) fc = (fc << 1) | bit_lib_get_bit(data, indala26_fc_bits[i]);
    uint16_t cn = 0;
    for(int i = 0; i < 16; i++) cn = (cn << 1) | bit_lib_get_bit(data, indala26_cn_bits[i]);
    *fc_out = fc;
    *cn_out = cn;
    return true;
}

static bool indala26_encode(uint8_t* data, size_t data_size, uint8_t fc, uint16_t cn) {
    if(data_size < 4) return false;
    /* Write FC bits (MSB first) */
    for(int i = 0; i < 8; i++)
        bit_lib_set_bit(data, indala26_fc_bits[i], (fc >> (7 - i)) & 1);
    /* Write CN bits (MSB first) */
    for(int i = 0; i < 16; i++)
        bit_lib_set_bit(data, indala26_cn_bits[i], (cn >> (15 - i)) & 1);

    /* Recalculate parity and checksum (from Flipper firmware render_data) */
    uint32_t fc_and_card = ((uint32_t)fc << 16) | cn;

    /* Even parity over bits 12..23 of fc_and_card → stored at data bit 1 */
    uint8_t ep = 0;
    for(int i = 12; i < 24; i++) ep += (fc_and_card >> i) & 1;
    bit_lib_set_bit(data, 1, ep % 2);

    /* Odd parity over bits 0..11 of fc_and_card → stored at data bit 5 */
    uint8_t op = 1;
    for(int i = 0; i < 12; i++) op += (fc_and_card >> i) & 1;
    bit_lib_set_bit(data, 5, op % 2);

    /* Indala checksum → stored at data bits 27..28 */
    uint8_t cs = 0;
    cs += (fc_and_card >> 14) & 1;
    cs += (fc_and_card >> 12) & 1;
    cs += (fc_and_card >> 9) & 1;
    cs += (fc_and_card >> 8) & 1;
    cs += (fc_and_card >> 6) & 1;
    cs += (fc_and_card >> 5) & 1;
    cs += (fc_and_card >> 2) & 1;
    cs += (fc_and_card >> 0) & 1;
    if((cs & 1) == 1) {
        bit_lib_set_bit(data, 27, 0);
        bit_lib_set_bit(data, 28, 1);
    } else {
        bit_lib_set_bit(data, 27, 1);
        bit_lib_set_bit(data, 28, 0);
    }
    return true;
}

/**
 * IoProxXSF: 4 bytes.  FC=data[0], version=data[1], CN=data[2..3].
 */
static bool ioprox_extract(const uint8_t* data, size_t data_size, uint8_t* fc_out, uint16_t* cn_out) {
    if(data_size < 4) return false;
    *fc_out = data[0];
    *cn_out = ((uint16_t)data[2] << 8) | data[3];
    return true;
}

static bool ioprox_encode(uint8_t* data, size_t data_size, uint8_t fc, uint16_t cn) {
    if(data_size < 4) return false;
    data[0] = fc;
    data[2] = (cn >> 8) & 0xFF;
    data[3] = cn & 0xFF;
    return true;
}

/**
 * AWID (26-bit format): 9 bytes. data[0]=format(26).
 * FC at bits 9..16, CN at bits 17..32.
 * Wiegand parity: even at bit 8, odd at bit 33.
 */
static bool awid_extract(const uint8_t* data, size_t data_size, uint8_t* fc_out, uint16_t* cn_out) {
    if(data_size < 9) return false;
    if(data[0] != 26) return false; /* Only 26-bit format supported */
    *fc_out = bit_lib_get_bits(data, 9, 8);
    *cn_out = bit_lib_get_bits_16(data, 17, 16);
    return true;
}

static bool awid_encode(uint8_t* data, size_t data_size, uint8_t fc, uint16_t cn) {
    if(data_size < 9) return false;
    if(data[0] != 26) return false;
    bit_lib_set_bits(data, 9, fc, 8);
    bit_lib_set_bits(data, 17, (cn >> 8) & 0xFF, 8);
    bit_lib_set_bits(data, 25, cn & 0xFF, 8);
    /* Wiegand even parity over FC + high CN (bits 9..20 = 12 bits) */
    uint8_t ep = 0;
    for(int i = 9; i <= 20; i++) ep ^= bit_lib_get_bit(data, i);
    bit_lib_set_bit(data, 8, ep);
    /* Wiegand odd parity over low CN (bits 21..32 = 12 bits) */
    uint8_t op = 1;
    for(int i = 21; i <= 32; i++) op ^= bit_lib_get_bit(data, i);
    bit_lib_set_bit(data, 33, op);
    return true;
}

/**
 * Pyramid (26-bit format): 4 bytes. data[0]=format(26).
 * FC at bits 8..15, CN at bits 16..31.
 */
static bool pyramid_extract(const uint8_t* data, size_t data_size, uint8_t* fc_out, uint16_t* cn_out) {
    if(data_size < 4) return false;
    if(data[0] != 26) return false;
    *fc_out = bit_lib_get_bits(data, 8, 8);
    *cn_out = bit_lib_get_bits_16(data, 16, 16);
    return true;
}

static bool pyramid_encode(uint8_t* data, size_t data_size, uint8_t fc, uint16_t cn) {
    if(data_size < 4) return false;
    if(data[0] != 26) return false;
    bit_lib_set_bits(data, 8, fc, 8);
    bit_lib_set_bits(data, 16, (cn >> 8) & 0xFF, 8);
    bit_lib_set_bits(data, 24, cn & 0xFF, 8);
    return true;
}

/**
 * Paradox: 6 bytes. FC at bits 10..17, CN at bits 18..33, CRC at bits 34..41.
 * CRC uses bit_lib_crc8 over Manchester-encoded FC+CN.
 */
static bool paradox_extract(const uint8_t* data, size_t data_size, uint8_t* fc_out, uint16_t* cn_out) {
    if(data_size < 6) return false;
    *fc_out = bit_lib_get_bits(data, 10, 8);
    *cn_out = bit_lib_get_bits_16(data, 18, 16);
    return true;
}

static uint8_t paradox_calc_checksum(uint8_t fc, uint16_t cn) {
    uint8_t arr[5] = {0, 0, fc, (cn >> 8) & 0xFF, cn & 0xFF};
    uint8_t manchester[9];
    memset(manchester, 0, sizeof(manchester));

    /* 4 leading zero bits */
    bit_lib_push_bit(manchester, 9, false);
    bit_lib_push_bit(manchester, 9, false);
    bit_lib_push_bit(manchester, 9, false);
    bit_lib_push_bit(manchester, 9, false);

    /* Manchester-encode bits 6..39 of arr */
    for(uint8_t i = 6; i < 40; i++) {
        if(bit_lib_get_bit(arr, i)) {
            bit_lib_push_bit(manchester, 9, true);
            bit_lib_push_bit(manchester, 9, false);
        } else {
            bit_lib_push_bit(manchester, 9, false);
            bit_lib_push_bit(manchester, 9, true);
        }
    }
    return bit_lib_crc8(manchester, 9, 0x31, 0x00, true, true, 0x06);
}

static bool paradox_encode(uint8_t* data, size_t data_size, uint8_t fc, uint16_t cn) {
    if(data_size < 6) return false;
    bit_lib_set_bits(data, 10, fc, 8);
    bit_lib_set_bits(data, 18, (cn >> 8) & 0xFF, 8);
    bit_lib_set_bits(data, 26, cn & 0xFF, 8);
    /* Recalculate and write CRC */
    uint8_t crc = paradox_calc_checksum(fc, cn);
    bit_lib_set_bits(data, 34, crc, 8);
    return true;
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  LFRFID WORKER CALLBACKS
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

/** Called by LFRFID worker when a tag is successfully read */
static void lfrfid_read_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    BulkWriterApp* app = context;

    if(result == LFRFIDWorkerReadDone) {
        app->last_protocol = protocol;
        app->last_data_size = protocol_dict_get_data_size(app->protocol_dict, protocol);
        if(app->last_data_size > sizeof(app->last_data)) {
            app->last_data_size = sizeof(app->last_data);
        }
        protocol_dict_get_data(app->protocol_dict, protocol, app->last_data, app->last_data_size);

        FURI_LOG_I(TAG, "Read OK: protocol=%ld, data_size=%zu", protocol, app->last_data_size);

        /* Signal main loop: transition to write phase */
        InputEvent event = {.type = InputTypePress, .key = InputKeyMAX};
        furi_message_queue_put(app->event_queue, &event, 0);
    }
}

/**
 * Called by LFRFID worker during reference card scan.
 * Learns the protocol, modulation type, FC and CN from the scanned card
 * and locks the read type for all subsequent bulk reads.
 */
static void lfrfid_ref_scan_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    BulkWriterApp* app = context;

    if(result == LFRFIDWorkerReadDone) {
        /* Store protocol info */
        app->ref_protocol = protocol;
        app->ref_scanned = true;

        /* Get protocol name */
        const char* name = protocol_dict_get_name(app->protocol_dict, protocol);
        snprintf(app->ref_proto_name, sizeof(app->ref_proto_name), "%s", name);

        /* Read data to extract FC/CN for display */
        size_t data_size = protocol_dict_get_data_size(app->protocol_dict, protocol);
        uint8_t data[16];
        if(data_size > sizeof(data)) data_size = sizeof(data);
        protocol_dict_get_data(app->protocol_dict, protocol, data, data_size);

        /* Determine modulation from data size heuristic:
         * ASK protocols (HID, EM4100, FDX-B, etc.) are the vast majority of 125kHz.
         * PSK protocols (Indala, AWID, IoProx) are less common.
         * The LFRFID protocol registry groups them, but we can infer from the
         * protocol name or just default to ASK since the worker already decoded it.
         *
         * A more robust approach: try ASK-only read first, if it succeeds the card
         * is ASK. But since we used Auto for the ref scan, we check protocol names.
         */
        /* Default to ASK ΓÇö most 125kHz cards are ASK modulated */
        app->ref_read_type = LFRFIDWorkerReadTypeASKOnly;

        /* Known PSK protocols */
        if(strstr(app->ref_proto_name, "Indala") ||
           strstr(app->ref_proto_name, "AWID") ||
           strstr(app->ref_proto_name, "IoProx") ||
           strstr(app->ref_proto_name, "Paradox") ||
           strstr(app->ref_proto_name, "Keri")) {
            app->ref_read_type = LFRFIDWorkerReadTypePSKOnly;
        }

        /* Extract FC/CN for display — use protocol name, not data_size */
        app->ref_fc = 0;
        app->ref_cn = 0;
        if(strstr(app->ref_proto_name, "H10301")) {
            hid_h10301_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else if(strstr(app->ref_proto_name, "EM4100") || strstr(app->ref_proto_name, "EM410")) {
            em4100_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else if(strstr(app->ref_proto_name, "Indala26")) {
            indala26_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else if(strstr(app->ref_proto_name, "AWID")) {
            awid_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else if(strstr(app->ref_proto_name, "IoProx")) {
            ioprox_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else if(strstr(app->ref_proto_name, "Pyramid")) {
            pyramid_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else if(strstr(app->ref_proto_name, "Paradox")) {
            paradox_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else {
            generic_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        }

        FURI_LOG_I(TAG, "Reference scan: %s, FC=%d CN=%d, read_type=%d",
            app->ref_proto_name, app->ref_fc, app->ref_cn, app->ref_read_type);

        /* Signal main loop */
        InputEvent event = {.type = InputTypePress, .key = InputKeyMAX};
        furi_message_queue_put(app->event_queue, &event, 0);
    }
}

/** Called by LFRFID worker when a write completes.
 *  The write worker keeps running (retransmitting) until explicitly stopped,
 *  so this callback may fire multiple times. We use worker_busy as a
 *  one-shot gate to process only the first result per write cycle. */
static void lfrfid_write_callback(LFRFIDWorkerWriteResult result, void* context) {
    BulkWriterApp* app = context;

    /* Only process the first callback per write — ignore subsequent retransmits */
    if(!app->worker_busy) return;
    app->worker_busy = false;

    if(result == LFRFIDWorkerWriteOK) {
        app->cards_written++;
        app->current_screen = Screen_Success;
        FURI_LOG_I(TAG, "Write OK: total=%d", app->cards_written);
    } else {
        app->cards_failed++;
        app->current_screen = Screen_Error;
        FURI_LOG_E(TAG, "Write FAIL: total_fail=%d", app->cards_failed);
    }

    /* Signal main loop to update display */
    InputEvent event = {.type = InputTypePress, .key = InputKeyMAX};
    furi_message_queue_put(app->event_queue, &event, 0);
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  CORE PROCESSING LOGIC
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

/**
 * Process a read tag: extract FC/CN, substitute FC, optionally adjust CN,
 * encode back, and initiate write.
 *
 * Protocol detection: uses data_size from the protocol dict to determine
 * whether this is HID H10301 (3 bytes) or EM4100 (5 bytes). If a reference
 * card was scanned, we already know which protocol to expect.
 */
static bool app_process_tag(BulkWriterApp* app) {
    uint8_t orig_fc = 0;
    uint16_t orig_cn = 0;
    bool extracted = false;

    /* Identify protocol by name, not data_size */
    const char* proto_name = protocol_dict_get_name(app->protocol_dict, app->last_protocol);
    FURI_LOG_I(TAG, "Processing protocol: %s (data_size=%zu)", proto_name, app->last_data_size);

    /* Determine encoding type from protocol name */
    typedef enum {
        Enc_HID, Enc_EM4100, Enc_Indala26, Enc_AWID, Enc_IoProx,
        Enc_Pyramid, Enc_Paradox, Enc_Generic
    } EncType;
    EncType enc_type = Enc_Generic;

    if(strstr(proto_name, "H10301")) {
        enc_type = Enc_HID;
    } else if(strstr(proto_name, "EM4100") || strstr(proto_name, "EM410")) {
        enc_type = Enc_EM4100;
    } else if(strstr(proto_name, "Indala26")) {
        enc_type = Enc_Indala26;
    } else if(strstr(proto_name, "AWID")) {
        enc_type = Enc_AWID;
    } else if(strstr(proto_name, "IoProx")) {
        enc_type = Enc_IoProx;
    } else if(strstr(proto_name, "Pyramid")) {
        enc_type = Enc_Pyramid;
    } else if(strstr(proto_name, "Paradox")) {
        enc_type = Enc_Paradox;
    }

    /* Extract FC/CN using appropriate decoder */
    switch(enc_type) {
        case Enc_HID:
            extracted = hid_h10301_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
        case Enc_EM4100:
            extracted = em4100_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
        case Enc_Indala26:
            extracted = indala26_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
        case Enc_AWID:
            extracted = awid_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
        case Enc_IoProx:
            extracted = ioprox_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
        case Enc_Pyramid:
            extracted = pyramid_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
        case Enc_Paradox:
            extracted = paradox_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
        case Enc_Generic:
            extracted = generic_extract(app->last_data, app->last_data_size, &orig_fc, &orig_cn);
            break;
    }

    if(!extracted) {
        FURI_LOG_E(TAG, "Could not extract FC/CN from protocol data");
        return false;
    }

    FURI_LOG_I(TAG, "Original: FC=%d CN=%d", orig_fc, orig_cn);

    /* Determine new card number based on mode */
    uint16_t new_cn = orig_cn;
    switch(app->card_num_mode) {
        case CardNumMode_Preserve:
            new_cn = orig_cn;
            break;
        case CardNumMode_Sequential:
            new_cn = app->next_sequential++;
            break;
        case CardNumMode_Fixed:
            new_cn = app->card_num_base;
            break;
        default:
            break;
    }

    /* Encode with new FC and CN */
    bool encoded = false;
    uint8_t write_data[16];
    memcpy(write_data, app->last_data, app->last_data_size);

    switch(enc_type) {
        case Enc_HID:
            encoded = hid_h10301_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
        case Enc_EM4100:
            encoded = em4100_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
        case Enc_Indala26:
            encoded = indala26_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
        case Enc_AWID:
            encoded = awid_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
        case Enc_IoProx:
            encoded = ioprox_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
        case Enc_Pyramid:
            encoded = pyramid_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
        case Enc_Paradox:
            encoded = paradox_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
        case Enc_Generic:
            encoded = generic_encode(write_data, app->last_data_size, app->facility_code, new_cn);
            break;
    }

    if(!encoded) {
        FURI_LOG_E(TAG, "Encoding failed");
        return false;
    }

    FURI_LOG_I(TAG, "New: FC=%d CN=%d", app->facility_code, new_cn);

    /* Load encoded data into protocol dict and start write */
    protocol_dict_set_data(app->protocol_dict, app->last_protocol, write_data, app->last_data_size);
    app->worker_busy = true;
    lfrfid_worker_write_start(app->lf_worker, app->last_protocol, lfrfid_write_callback, app);

    return true;
}

/**
 * Get the LFRFIDWorkerReadType based on current modulation config.
 * If a reference card was scanned, use its learned read type for speed.
 * Otherwise fall back to the manual mod_select setting.
 */
static LFRFIDWorkerReadType app_get_read_type(BulkWriterApp* app) {
    if(app->ref_scanned) {
        return app->ref_read_type;
    }
    switch(app->mod_select) {
        case Mod_ASK: return LFRFIDWorkerReadTypeASKOnly;
        case Mod_PSK: return LFRFIDWorkerReadTypePSKOnly;
        default:      return LFRFIDWorkerReadTypeAuto;
    }
}

/**
 * Start the readΓåÆwrite processing loop.
 * Uses locked modulation type for faster reads when configured.
 */
static void app_start_processing(BulkWriterApp* app) {
    app->running = true;
    app->current_screen = Screen_Ready;
    LFRFIDWorkerReadType read_type = app_get_read_type(app);
    lfrfid_worker_read_start(app->lf_worker, read_type, lfrfid_read_callback, app);

    FURI_LOG_I(TAG, "Processing started, read_type=%d", read_type);
}

/** Stop the processing loop */
static void app_stop_processing(BulkWriterApp* app) {
    app->running = false;
    lfrfid_worker_stop(app->lf_worker);
    FURI_LOG_I(TAG, "Processing stopped");
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  DRAW CALLBACKS
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static void app_draw_callback(Canvas* canvas, void* context) {
    BulkWriterApp* app = context;
    canvas_clear(canvas);

    switch(app->current_screen) {
        case Screen_Config:
            app_draw_config(canvas, app);
            break;
        case Screen_RefScan:
            app_draw_ref_scan(canvas, app);
            break;
        case Screen_RefResult:
            app_draw_ref_result(canvas, app);
            break;
        case Screen_Ready:
        case Screen_Reading:
            app_draw_ready(canvas, app);
            break;
        case Screen_Writing:
            app_draw_processing(canvas, app);
            break;
        case Screen_Success:
        case Screen_Error:
            app_draw_result(canvas, app);
            break;
        case Screen_Summary:
            app_draw_summary(canvas, app);
            break;
    }
}

/** Config screen ΓÇö edit FC, card number mode, modulation, scan reference */
static void app_draw_config(Canvas* canvas, BulkWriterApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Bulk Writer Setup");
    canvas_draw_line(canvas, 0, 15, 128, 15);

    canvas_set_font(canvas, FontSecondary);
    char buf[48];
    uint8_t y = 27;
    uint8_t row = 0;

    /* Row 0: Facility Code */
    snprintf(buf, sizeof(buf), "FC: %d", app->facility_code);
    canvas_draw_str(canvas, 4, y, buf);
    if(app->config_cursor == row) {
        canvas_draw_str(canvas, 0, y, ">");
        canvas_draw_str(canvas, 100, y, "<L/R>");
    }
    row++; y += 10;

    /* Row 1: Card Number Mode */
    const char* mode_names[] = {"Preserve", "Sequential", "Fixed"};
    snprintf(buf, sizeof(buf), "CN: %s", mode_names[app->card_num_mode]);
    canvas_draw_str(canvas, 4, y, buf);
    if(app->config_cursor == row) {
        canvas_draw_str(canvas, 0, y, ">");
        canvas_draw_str(canvas, 100, y, "<L/R>");
    }
    row++; y += 10;

    /* Row 2 (conditional): Card number base */
    if(app->card_num_mode != CardNumMode_Preserve) {
        snprintf(buf, sizeof(buf), "Base#: %d", app->card_num_base);
        canvas_draw_str(canvas, 4, y, buf);
        if(app->config_cursor == row) {
            canvas_draw_str(canvas, 0, y, ">");
            canvas_draw_str(canvas, 100, y, "<L/R>");
        }
        row++; y += 10;
    }

    /* Row N: Modulation ΓÇö or show reference scan result */
    if(app->ref_scanned) {
        snprintf(buf, sizeof(buf), "Ref: %s", app->ref_proto_name);
        canvas_draw_str(canvas, 4, y, buf);
        if(app->config_cursor == row) {
            canvas_draw_str(canvas, 0, y, ">");
            canvas_draw_str(canvas, 90, y, "Rescan>");
        }
    } else {
        const char* mod_names[] = {"Auto", "ASK", "PSK"};
        snprintf(buf, sizeof(buf), "Mod: %s", mod_names[app->mod_select]);
        canvas_draw_str(canvas, 4, y, buf);
        if(app->config_cursor == row) {
            canvas_draw_str(canvas, 0, y, ">");
            canvas_draw_str(canvas, 90, y, "Scan >");
        }
    }

    /* Footer */
    elements_button_center(canvas, "Start");
}

/** Reference scan screen ΓÇö waiting for reference card */
static void app_draw_ref_scan(Canvas* canvas, BulkWriterApp* app) {
    UNUSED(app);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignCenter, "Scan Reference Card");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "Place a sample card");
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "on the reader...");

    elements_button_left(canvas, "Cancel");
}

/** Reference scan result screen */
static void app_draw_ref_result(Canvas* canvas, BulkWriterApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "Reference Detected!");

    canvas_set_font(canvas, FontSecondary);
    char buf[48];

    snprintf(buf, sizeof(buf), "Protocol: %s", app->ref_proto_name);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, buf);

    snprintf(buf, sizeof(buf), "FC: %d  CN: %d", app->ref_fc, app->ref_cn);
    canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, buf);

    const char* mod = (app->ref_read_type == LFRFIDWorkerReadTypeASKOnly) ? "ASK" : "PSK";
    snprintf(buf, sizeof(buf), "Modulation: %s (locked)", mod);
    canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, buf);

    elements_button_center(canvas, "OK");
}

/** Ready/Reading screen ΓÇö waiting for card */
static void app_draw_ready(Canvas* canvas, BulkWriterApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignCenter, "Bulk Writer");

    canvas_set_font(canvas, FontSecondary);

    char buf[40];
    snprintf(buf, sizeof(buf), "FC: %d  |  Cards: %d", app->facility_code, app->cards_written);
    canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, buf);

    if(app->current_screen == Screen_Reading) {
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Reading tag...");
    } else {
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Place card on reader");
    }

    if(app->cards_failed > 0) {
        snprintf(buf, sizeof(buf), "Failed: %d", app->cards_failed);
        canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, buf);
    }

    elements_button_left(canvas, "Stop");
}

/** Writing in progress screen */
static void app_draw_processing(Canvas* canvas, BulkWriterApp* app) {
    UNUSED(app);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "Writing...");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "Keep card on reader");
}

/** Success or error flash */
static void app_draw_result(Canvas* canvas, BulkWriterApp* app) {
    canvas_set_font(canvas, FontPrimary);

    if(app->current_screen == Screen_Success) {
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "OK!");
        canvas_set_font(canvas, FontSecondary);
        char buf[32];
        snprintf(buf, sizeof(buf), "Card #%d written", app->cards_written);
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, buf);
    } else {
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "WRITE FAILED");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "Check card is T5577");
    }
}

/** Session summary screen */
static void app_draw_summary(Canvas* canvas, BulkWriterApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Session Summary");
    canvas_draw_line(canvas, 0, 15, 128, 15);

    canvas_set_font(canvas, FontSecondary);
    char buf[40];

    snprintf(buf, sizeof(buf), "Written: %d", app->cards_written);
    canvas_draw_str(canvas, 4, 28, buf);

    snprintf(buf, sizeof(buf), "Failed:  %d", app->cards_failed);
    canvas_draw_str(canvas, 4, 40, buf);

    snprintf(buf, sizeof(buf), "FC used: %d", app->facility_code);
    canvas_draw_str(canvas, 4, 52, buf);

    elements_button_center(canvas, "OK");
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  INPUT CALLBACK
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static void app_input_callback(InputEvent* input_event, void* context) {
    BulkWriterApp* app = context;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  INPUT HANDLING ΓÇö CONFIG SCREEN
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static void app_handle_config_input(BulkWriterApp* app, InputEvent* event) {
    if(event->type != InputTypePress && event->type != InputTypeRepeat) return;

    /* Track hold duration for acceleration (~10 repeats/sec on Flipper) */
    if(event->type == InputTypeRepeat) {
        app->repeat_count++;
    } else {
        app->repeat_count = 0;
    }

    /* Calculate max cursor position based on visible fields */
    uint8_t max_cursor = (app->card_num_mode != CardNumMode_Preserve) ? 3 : 2;
    /* The last row is always the modulation / reference scan row */
    uint8_t mod_row = max_cursor;

    switch(event->key) {
        case InputKeyUp:
            if(app->config_cursor > 0) app->config_cursor--;
            break;

        case InputKeyDown:
            if(app->config_cursor < max_cursor) app->config_cursor++;
            break;

        case InputKeyLeft:
        case InputKeyRight: {
            int8_t dir = (event->key == InputKeyRight) ? 1 : -1;

            if(app->config_cursor == 0) {
                /* Facility Code: 0-255 */
                int16_t new_fc = (int16_t)app->facility_code + dir;
                if(new_fc < FC_MIN) new_fc = FC_MAX;
                if(new_fc > FC_MAX) new_fc = FC_MIN;
                app->facility_code = (uint8_t)new_fc;

            } else if(app->config_cursor == 1) {
                /* Card Number Mode */
                int8_t new_mode = (int8_t)app->card_num_mode + dir;
                if(new_mode < 0) new_mode = CardNumMode_COUNT - 1;
                if(new_mode >= (int8_t)CardNumMode_COUNT) new_mode = 0;
                app->card_num_mode = (CardNumMode)new_mode;

            } else if(app->config_cursor == 2 && app->card_num_mode != CardNumMode_Preserve) {
                /* Card number base: 0-65535 with hold-to-accelerate */
                int32_t step = 1;
                if(app->repeat_count > 100) step = 1000;      /* ~10s: +1000 */
                else if(app->repeat_count > 50) step = 100;    /* ~5s:  +100  */
                else if(app->repeat_count > 20) step = 10;     /* ~2s:  +10   */

                int32_t new_base = (int32_t)app->card_num_base + dir * step;
                if(new_base < 0) new_base = 65535;
                if(new_base > 65535) new_base = 0;
                app->card_num_base = (uint16_t)new_base;

            } else if(app->config_cursor == mod_row) {
                if(event->key == InputKeyRight) {
                    /* Right on mod row = start reference scan */
                    app->current_screen = Screen_RefScan;
                    lfrfid_worker_read_start(
                        app->lf_worker, LFRFIDWorkerReadTypeAuto,
                        lfrfid_ref_scan_callback, app);
                } else {
                    /* Left on mod row = cycle modulation (clears ref scan) */
                    app->ref_scanned = false;
                    int8_t new_mod = (int8_t)app->mod_select - 1;
                    if(new_mod < 0) new_mod = Mod_COUNT - 1;
                    app->mod_select = (ModSelect)new_mod;
                }
            }
            break;
        }

        case InputKeyOk:
            /* Start processing */
            app->next_sequential = app->card_num_base;
            app_start_processing(app);
            break;

        case InputKeyBack:
            /* Will be caught by main loop to exit app */
            break;

        default:
            break;
    }
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  INPUT HANDLING ΓÇö PROCESSING SCREENS
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static void app_handle_processing_input(BulkWriterApp* app, InputEvent* event) {
    if(event->type != InputTypePress) return;

    switch(event->key) {
        case InputKeyLeft:
        case InputKeyBack:
            /* Stop processing, show summary */
            app_stop_processing(app);
            app->current_screen = Screen_Summary;
            break;
        default:
            break;
    }
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  PERSISTENT SETTINGS (same pattern as ClayLoop)
 *
 *  Settings saved to /ext/apps_data/bulk_writer/config.ff using FlipperFormat.
 *  Called at app exit. Loaded at startup to override defaults.
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static void app_save_settings(BulkWriterApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    /* Ensure directory exists */
    storage_simply_mkdir(storage, BULKWRITER_CONFIG_DIR);

    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_always(ff, BULKWRITER_CONFIG_PATH)) {
            FURI_LOG_E(TAG, "Failed to open config for writing");
            break;
        }

        flipper_format_write_header_cstr(ff, "BulkWriter Config", 1);

        uint32_t fc = app->facility_code;
        uint32_t mode = app->card_num_mode;
        uint32_t base = app->card_num_base;
        uint32_t mod = app->mod_select;

        flipper_format_write_uint32(ff, "FacilityCode", &fc, 1);
        flipper_format_write_uint32(ff, "CardNumMode", &mode, 1);
        flipper_format_write_uint32(ff, "CardNumBase", &base, 1);
        flipper_format_write_uint32(ff, "Modulation", &mod, 1);

        FURI_LOG_I(TAG, "Settings saved (FC=%lu mode=%lu base=%lu mod=%lu)",
            fc, mode, base, mod);
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void app_load_settings(BulkWriterApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_existing(ff, BULKWRITER_CONFIG_PATH)) {
            FURI_LOG_I(TAG, "No saved config found, using defaults");
            break;
        }

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        bool header_ok = flipper_format_read_header(ff, header, &version);
        furi_string_free(header);
        if(!header_ok) break;

        uint32_t val;

        if(flipper_format_read_uint32(ff, "FacilityCode", &val, 1)) {
            if(val <= FC_MAX) app->facility_code = (uint8_t)val;
        }
        if(flipper_format_read_uint32(ff, "CardNumMode", &val, 1)) {
            if(val < CardNumMode_COUNT) app->card_num_mode = (CardNumMode)val;
        }
        if(flipper_format_read_uint32(ff, "CardNumBase", &val, 1)) {
            if(val <= 65535) app->card_num_base = (uint16_t)val;
        }
        if(flipper_format_read_uint32(ff, "Modulation", &val, 1)) {
            if(val < Mod_COUNT) app->mod_select = (ModSelect)val;
        }

        FURI_LOG_I(TAG, "Settings loaded (FC=%d mode=%d base=%d mod=%d)",
            app->facility_code, app->card_num_mode, app->card_num_base, app->mod_select);
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  ALLOC / FREE (same lifecycle as ClayLoop)
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

static BulkWriterApp* app_alloc(void) {
    BulkWriterApp* app = malloc(sizeof(BulkWriterApp));
    memset(app, 0, sizeof(BulkWriterApp));

    /* Set defaults before loading persisted values */
    app->facility_code = FC_DEFAULT;
    app->card_num_mode = CARD_NUM_MODE_DEFAULT;
    app->card_num_base = CARD_NUM_DEFAULT;
    app->mod_select = MOD_DEFAULT;
    app->current_screen = Screen_Config;
    app->config_cursor = 0;
    app->ref_scanned = false;

    /* Override defaults from SD card config */
    app_load_settings(app);

    /* Reset runtime counters */
    app->next_sequential = app->card_num_base;
    app->cards_written = 0;
    app->cards_failed = 0;

    /* GUI setup */
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, app_draw_callback, app);
    view_port_input_callback_set(app->view_port, app_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    /* LFRFID subsystem */
    app->protocol_dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    app->lf_worker = lfrfid_worker_alloc(app->protocol_dict);
    lfrfid_worker_start_thread(app->lf_worker);

    FURI_LOG_I(TAG, "App allocated, FC=%d", app->facility_code);
    return app;
}

static void app_free(BulkWriterApp* app) {
    FURI_LOG_I(TAG, "Cleaning up...");

    /* Save settings before teardown */
    app_save_settings(app);

    /* Stop LFRFID worker */
    if(app->running) {
        lfrfid_worker_stop(app->lf_worker);
    }
    lfrfid_worker_stop_thread(app->lf_worker);
    lfrfid_worker_free(app->lf_worker);
    protocol_dict_free(app->protocol_dict);

    /* GUI teardown */
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

/* ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ
 *  MAIN ENTRY POINT
 * ΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉΓòÉ */

int32_t bulk_writer_app(void* p) {
    UNUSED(p);

    BulkWriterApp* app = app_alloc();
    InputEvent event;
    bool exit_requested = false;

    while(!exit_requested) {
        FuriStatus status = furi_message_queue_get(app->event_queue, &event, 100);

        if(status == FuriStatusOk) {
            /* Internal signal from LFRFID callbacks (key = InputKeyMAX) */
            if(event.key == InputKeyMAX) {

                if(app->current_screen == Screen_RefScan) {
                    /* Reference scan completed ΓÇö stop worker, show result */
                    lfrfid_worker_stop(app->lf_worker);
                    beep_success();
                    app->current_screen = Screen_RefResult;

                } else if(app->current_screen == Screen_Success ||
                   app->current_screen == Screen_Error) {
                    /* Fire LED + audible notification (tight timing for bulk speed) */
                    if(app->current_screen == Screen_Success) {
                        notification_message(app->notifications, &seq_success);
                        beep_success();
                        view_port_update(app->view_port);
                        furi_delay_ms(300);
                    } else {
                        notification_message(app->notifications, &seq_error);
                        beep_error();
                        view_port_update(app->view_port);
                        furi_delay_ms(500);
                    }

                    /*
                     * AUTO-RESUME: stop the write worker (it keeps
                     * retransmitting until stopped), then restart the
                     * reader for the next card.
                     */
                    if(app->running) {
                        lfrfid_worker_stop(app->lf_worker);
                        app->current_screen = Screen_Ready;
                        LFRFIDWorkerReadType read_type = app_get_read_type(app);
                        lfrfid_worker_read_start(
                            app->lf_worker, read_type,
                            lfrfid_read_callback, app);
                    }
                } else if(app->current_screen == Screen_Ready ||
                          app->current_screen == Screen_Reading) {
                    /* Tag was read during bulk processing ΓÇö process it */
                    lfrfid_worker_stop(app->lf_worker);
                    app->current_screen = Screen_Writing;
                    view_port_update(app->view_port);
                    notification_message(app->notifications, &seq_writing);

                    if(!app_process_tag(app)) {
                        app->cards_failed++;
                        app->current_screen = Screen_Error;
                        app->worker_busy = false;
                        /* Re-trigger to show error */
                        InputEvent err_event = {.type = InputTypePress, .key = InputKeyMAX};
                        furi_message_queue_put(app->event_queue, &err_event, 0);
                    }
                }
            } else {
                /* Real user input */
                switch(app->current_screen) {
                    case Screen_Config:
                        if(event.key == InputKeyBack && event.type == InputTypePress) {
                            exit_requested = true;
                        } else {
                            app_handle_config_input(app, &event);
                        }
                        break;

                    case Screen_RefScan:
                        /* Cancel reference scan */
                        if(event.type == InputTypePress &&
                           (event.key == InputKeyBack || event.key == InputKeyLeft)) {
                            lfrfid_worker_stop(app->lf_worker);
                            app->current_screen = Screen_Config;
                        }
                        break;

                    case Screen_RefResult:
                        /* Acknowledge reference result ΓåÆ back to config */
                        if(event.type == InputTypePress &&
                           (event.key == InputKeyOk || event.key == InputKeyBack)) {
                            app->current_screen = Screen_Config;
                        }
                        break;

                    case Screen_Ready:
                    case Screen_Reading:
                    case Screen_Writing:
                    case Screen_Success:
                    case Screen_Error:
                        app_handle_processing_input(app, &event);
                        break;

                    case Screen_Summary:
                        if(event.type == InputTypePress &&
                           (event.key == InputKeyOk || event.key == InputKeyBack)) {
                            /* Return to config for another batch */
                            app->current_screen = Screen_Config;
                            app->cards_written = 0;
                            app->cards_failed = 0;
                        }
                        break;
                }
            }
        }

        view_port_update(app->view_port);
    }

    app_free(app);
    return 0;
}
