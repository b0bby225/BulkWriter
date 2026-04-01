/**
 * Bulk Writer V2 — Multi-Protocol RFID Tag Reprogrammer
 *
 * Enhanced version supporting both:
 * - LF RFID (125kHz): HID H10301, EM4100, Indala, AWID
 * - NFC (13.56MHz): NTAG213/215/216, Mifare Classic 1K/4K
 *
 * Reads tags, substitutes the facility code with a user-preset value,
 * optionally adjusts the card number, and writes back. Designed for
 * hands-free bulk processing with LED/vibration/speaker feedback.
 *
 * Built on V1 foundation — same ViewPort + input callback + draw callback
 * architecture, state machine driven by input events, persistent settings.
 *
 * NFC support uses Flipper's synchronous poller API in a background thread,
 * while LF RFID uses the async LFRFIDWorker (same as V1).
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

/* LF RFID (125kHz) — same as V1 */
#include <lib/lfrfid/lfrfid_worker.h>
#include <lib/lfrfid/protocols/lfrfid_protocols.h>

/* NFC (13.56MHz) — V2 addition */
#include <lib/nfc/nfc.h>
#include <lib/nfc/nfc_scanner.h>
#include <lib/nfc/protocols/nfc_protocol.h>
#include <lib/nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <lib/nfc/protocols/mf_ultralight/mf_ultralight_poller_sync.h>
#include <lib/nfc/protocols/mf_classic/mf_classic.h>
#include <lib/nfc/protocols/mf_classic/mf_classic_poller_sync.h>

#define TAG "BulkWriterV2"

/* ══════════════════════════════════════════════════════════════════════════════
 *  CONSTANTS & DEFAULTS
 * ══════════════════════════════════════════════════════════════════════════════ */

#define BULKWRITER_CONFIG_DIR  "/ext/apps_data/bulk_writer_v2"
#define BULKWRITER_CONFIG_PATH "/ext/apps_data/bulk_writer_v2/config.ff"

#define FC_MIN 0
#define FC_MAX 255
#define FC_DEFAULT 1

/** Card number mode */
typedef enum {
    CardNumMode_Preserve = 0, /** Keep original card number */
    CardNumMode_Sequential,   /** Assign sequential numbers starting from base */
    CardNumMode_Fixed,        /** Write a fixed card number */
    CardNumMode_COUNT
} CardNumMode;

#define CARD_NUM_DEFAULT      0
#define CARD_NUM_MODE_DEFAULT CardNumMode_Preserve

/** Modulation selection — determines LFRFIDWorkerReadType (LF only) */
typedef enum {
    Mod_Auto = 0, /** Auto-detect (slowest — cycles ASK+PSK) */
    Mod_ASK,      /** ASK only (HID, EM4100, most common) */
    Mod_PSK,      /** PSK only (Indala, AWID, etc.) */
    Mod_COUNT
} ModSelect;

#define MOD_DEFAULT Mod_Auto

/** V2 Reader type — which antenna/protocol to use */
typedef enum {
    ReaderType_Auto = 0, /** Auto-detect: try LF first, then NFC */
    ReaderType_LF,       /** 125kHz LF RFID only */
    ReaderType_NFC,      /** 13.56MHz NFC only */
    ReaderType_COUNT
} ReaderType;

#define READER_TYPE_DEFAULT ReaderType_Auto

/** Max cards we track in session stats */
#define MAX_LOG_ENTRIES 999

/** NFC data page/block where we store FC/CN */
#define NFC_NTAG_DATA_PAGE   4
#define NFC_MFC_DATA_BLOCK   4

/** NFC BulkWriter marker byte */
#define NFC_BW_MARKER 0x42

/* ══════════════════════════════════════════════════════════════════════════════
 *  APPLICATION SCREENS
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    Screen_Config,    /** Setup: FC, CN mode, reader type, modulation */
    Screen_RefScan,   /** "Scan Reference Card" — learning protocol */
    Screen_RefResult, /** Show what was detected from reference scan */
    Screen_Ready,     /** "Place card on reader" — idle waiting */
    Screen_Reading,   /** Active read in progress */
    Screen_Writing,   /** Active write in progress */
    Screen_Success,   /** Card written OK — brief flash */
    Screen_Error,     /** Write failed — brief flash */
    Screen_Summary,   /** Session stats */
} AppScreen;

/** NFC operation type for the background thread */
typedef enum {
    NfcOp_RefScan,      /** Reference scan: detect card, read FC/CN */
    NfcOp_ReadAndWrite,  /** Bulk: read, modify FC, write back */
} NfcOpType;

/* ══════════════════════════════════════════════════════════════════════════════
 *  APPLICATION STATE
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* GUI */
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;

    /* Notifications (LED, vibro, speaker) */
    NotificationApp* notifications;

    /* LFRFID (125kHz) */
    LFRFIDWorker* lf_worker;
    ProtocolDict* protocol_dict;

    /* NFC (13.56MHz) */
    Nfc* nfc;
    FuriThread* nfc_thread;

    /* Current screen state */
    AppScreen current_screen;

    /* Config (persisted) */
    uint8_t facility_code;
    CardNumMode card_num_mode;
    uint16_t card_num_base;
    ModSelect mod_select;
    ReaderType reader_type;

    /* Config screen cursor */
    uint8_t config_cursor;

    /* Reference scan state */
    bool ref_scanned;
    bool ref_is_nfc;                      /** true = NFC card, false = LF card */
    LFRFIDWorkerReadType ref_read_type;   /** Learned LF read type */
    NfcProtocol ref_nfc_protocol;         /** NFC protocol (MfUltralight/MfClassic) */
    MfUltralightType ref_ntag_type;       /** NTAG subtype */
    MfClassicType ref_mf_classic_type;    /** MfClassic subtype */
    char ref_proto_name[32];
    uint8_t ref_fc;
    uint16_t ref_cn;

    /* Runtime state */
    bool running;
    bool worker_busy;

    /* LF last read data */
    LFRFIDProtocol last_lf_protocol;
    uint8_t last_lf_data[16];
    size_t last_lf_data_size;

    /* NFC thread shared state */
    NfcOpType nfc_op;
    volatile bool nfc_thread_running;
    volatile bool nfc_cancel;
    bool nfc_success;
    NfcProtocol nfc_detected;
    MfUltralightType nfc_ntag_type;
    MfClassicType nfc_mf_classic_type;
    uint8_t nfc_read_fc;
    uint16_t nfc_read_cn;
    char nfc_proto_name[32];

    /* Session counters */
    uint16_t cards_written;
    uint16_t cards_failed;
    uint16_t next_sequential;
} BulkWriterV2App;

/* ══════════════════════════════════════════════════════════════════════════════
 *  LED / NOTIFICATION SEQUENCES
 * ══════════════════════════════════════════════════════════════════════════════ */

/** Green blink + short vibro = card written OK */
static const NotificationSequence seq_success = {
    &message_green_255,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_500,
    &message_green_0,
    NULL,
};

/** Red blink + double vibro = write failed */
static const NotificationSequence seq_error = {
    &message_red_255,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_250,
    &message_red_0,
    NULL,
};

/** Blue pulse = reading */
static const NotificationSequence seq_reading = {
    &message_blue_255,
    &message_delay_250,
    &message_blue_0,
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

/* ══════════════════════════════════════════════════════════════════════════════
 *  SPEAKER BEEP HELPERS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void beep_success(void) {
    if(furi_hal_speaker_acquire(100)) {
        furi_hal_speaker_start(880.0f, 1.0f);
        furi_delay_ms(100);
        furi_hal_speaker_stop();
        furi_delay_ms(50);
        furi_hal_speaker_start(1100.0f, 1.0f);
        furi_delay_ms(100);
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
}

static void beep_error(void) {
    if(furi_hal_speaker_acquire(100)) {
        furi_hal_speaker_start(440.0f, 1.0f);
        furi_delay_ms(150);
        furi_hal_speaker_stop();
        furi_delay_ms(50);
        furi_hal_speaker_start(220.0f, 1.0f);
        furi_delay_ms(250);
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  FORWARD DECLARATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void app_draw_callback(Canvas* canvas, void* context);
static void app_input_callback(InputEvent* input_event, void* context);
static void app_save_settings(BulkWriterV2App* app);
static void app_load_settings(BulkWriterV2App* app);
static void app_draw_config(Canvas* canvas, BulkWriterV2App* app);
static void app_draw_ref_scan(Canvas* canvas, BulkWriterV2App* app);
static void app_draw_ref_result(Canvas* canvas, BulkWriterV2App* app);
static void app_draw_ready(Canvas* canvas, BulkWriterV2App* app);
static void app_draw_processing(Canvas* canvas, BulkWriterV2App* app);
static void app_draw_result(Canvas* canvas, BulkWriterV2App* app);
static void app_draw_summary(Canvas* canvas, BulkWriterV2App* app);
static LFRFIDWorkerReadType app_get_lf_read_type(BulkWriterV2App* app);
static void app_start_nfc_op(BulkWriterV2App* app, NfcOpType op);
static void app_stop_nfc(BulkWriterV2App* app);

/* ══════════════════════════════════════════════════════════════════════════════
 *  LF RFID PROTOCOL HELPERS (from V1)
 * ══════════════════════════════════════════════════════════════════════════════ */

/**
 * Extract FC/CN from HID H10301 26-bit data.
 * Layout: 3 bytes = [P+FC_high][FC_low+CN_high][CN_low+P]
 * Bits: P FFFFFFFF CCCCCCCCCCCCCCCC P
 */
static bool hid_h10301_extract(
    const uint8_t* data, size_t data_size,
    uint8_t* fc_out, uint16_t* cn_out) {
    if(data_size < 3) return false;
    uint32_t raw = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    *fc_out = (raw >> 17) & 0xFF;
    *cn_out = (raw >> 1) & 0xFFFF;
    return true;
}

/** Encode HID H10301 26-bit data with new FC/CN, recalculating parity. */
static bool hid_h10301_encode(
    uint8_t* data, size_t data_size,
    uint8_t fc, uint16_t cn) {
    if(data_size < 3) return false;

    uint32_t raw = 0;
    raw |= ((uint32_t)fc << 17);
    raw |= ((uint32_t)cn << 1);

    /* Even parity over bits 25..14 */
    uint8_t even_p = 0;
    for(int i = 25; i >= 14; i--) even_p ^= (raw >> i) & 1;
    if(even_p) raw |= (1 << 25);

    /* Odd parity over bits 12..1 */
    uint8_t odd_p = 1;
    for(int i = 12; i >= 1; i--) odd_p ^= (raw >> i) & 1;
    if(odd_p) raw |= 1;

    data[0] = (raw >> 16) & 0xFF;
    data[1] = (raw >> 8) & 0xFF;
    data[2] = raw & 0xFF;
    return true;
}

/**
 * Extract FC/CN from EM4100 data.
 * Layout: 5 bytes = [FC/version][ID3][ID2][ID1][ID0]
 */
static bool em4100_extract(
    const uint8_t* data, size_t data_size,
    uint8_t* fc_out, uint16_t* cn_out) {
    if(data_size < 5) return false;
    *fc_out = data[0];
    *cn_out = ((uint16_t)data[3] << 8) | data[4];
    return true;
}

/** Encode EM4100 data with new FC, preserving upper ID bytes. */
static bool em4100_encode(
    uint8_t* data, size_t data_size,
    uint8_t fc, uint16_t cn) {
    if(data_size < 5) return false;
    data[0] = fc;
    data[3] = (cn >> 8) & 0xFF;
    data[4] = cn & 0xFF;
    return true;
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  LFRFID WORKER CALLBACKS (from V1)
 * ══════════════════════════════════════════════════════════════════════════════ */

/** Called when a tag is successfully read during bulk processing */
static void lfrfid_read_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    BulkWriterV2App* app = context;

    if(result == LFRFIDWorkerReadDone) {
        app->last_lf_protocol = protocol;
        app->last_lf_data_size = protocol_dict_get_data_size(app->protocol_dict, protocol);
        if(app->last_lf_data_size > sizeof(app->last_lf_data)) {
            app->last_lf_data_size = sizeof(app->last_lf_data);
        }
        protocol_dict_get_data(app->protocol_dict, protocol, app->last_lf_data, app->last_lf_data_size);
        FURI_LOG_I(TAG, "LF Read OK: proto=%ld, size=%zu", protocol, app->last_lf_data_size);

        /* Signal main loop */
        InputEvent event = {.type = InputTypePress, .key = InputKeyMAX};
        furi_message_queue_put(app->event_queue, &event, 0);
    }
}

/** Called during reference card scan — learns protocol, modulation, FC/CN */
static void lfrfid_ref_scan_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    BulkWriterV2App* app = context;

    if(result == LFRFIDWorkerReadDone) {
        app->ref_scanned = true;
        app->ref_is_nfc = false;

        const char* name = protocol_dict_get_name(app->protocol_dict, protocol);
        snprintf(app->ref_proto_name, sizeof(app->ref_proto_name), "%s", name);

        size_t data_size = protocol_dict_get_data_size(app->protocol_dict, protocol);
        uint8_t data[16];
        if(data_size > sizeof(data)) data_size = sizeof(data);
        protocol_dict_get_data(app->protocol_dict, protocol, data, data_size);

        /* Determine modulation from protocol name */
        app->ref_read_type = LFRFIDWorkerReadTypeASKOnly;
        if(strstr(app->ref_proto_name, "Indala") ||
           strstr(app->ref_proto_name, "AWID") ||
           strstr(app->ref_proto_name, "IoProx") ||
           strstr(app->ref_proto_name, "Paradox") ||
           strstr(app->ref_proto_name, "Keri")) {
            app->ref_read_type = LFRFIDWorkerReadTypePSKOnly;
        }

        /* Extract FC/CN */
        app->ref_fc = 0;
        app->ref_cn = 0;
        if(data_size == 3) {
            hid_h10301_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        } else if(data_size >= 5) {
            em4100_extract(data, data_size, &app->ref_fc, &app->ref_cn);
        }

        FURI_LOG_I(TAG, "LF Ref: %s FC=%d CN=%d type=%d",
            app->ref_proto_name, app->ref_fc, app->ref_cn, app->ref_read_type);

        InputEvent event = {.type = InputTypePress, .key = InputKeyMAX};
        furi_message_queue_put(app->event_queue, &event, 0);
    }
}

/** Called when a write completes */
static void lfrfid_write_callback(LFRFIDWorkerWriteResult result, void* context) {
    BulkWriterV2App* app = context;

    if(result == LFRFIDWorkerWriteOK) {
        app->cards_written++;
        app->current_screen = Screen_Success;
        FURI_LOG_I(TAG, "LF Write OK: total=%d", app->cards_written);
    } else {
        app->cards_failed++;
        app->current_screen = Screen_Error;
        FURI_LOG_E(TAG, "LF Write FAIL: total_fail=%d", app->cards_failed);
    }
    app->worker_busy = false;

    InputEvent event = {.type = InputTypePress, .key = InputKeyMAX};
    furi_message_queue_put(app->event_queue, &event, 0);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  NFC OPERATION THREAD (V2)
 *
 *  Runs NFC sync operations in a background thread to avoid blocking the
 *  main event loop. Signals main loop via message queue when done.
 * ══════════════════════════════════════════════════════════════════════════════ */

/** Get NTAG type name */
static const char* ntag_type_name(MfUltralightType type) {
    switch(type) {
        case MfUltralightTypeNTAG213: return "NTAG213";
        case MfUltralightTypeNTAG215: return "NTAG215";
        case MfUltralightTypeNTAG216: return "NTAG216";
        case MfUltralightTypeUL11:    return "UL11";
        case MfUltralightTypeUL21:    return "UL21";
        case MfUltralightTypeMfulC:   return "MF UL-C";
        default:                      return "NFC-UL";
    }
}

/** Get MfClassic type name */
static const char* mf_classic_type_name(MfClassicType type) {
    switch(type) {
        case MfClassicTypeMini: return "MF Mini";
        case MfClassicType1k:   return "MF Classic 1K";
        case MfClassicType4k:   return "MF Classic 4K";
        default:                return "MF Classic";
    }
}

/** Try to detect and read an NTAG/MfUltralight card. Returns true on success. */
static bool nfc_try_read_ntag(BulkWriterV2App* app) {
    /* Try to read page 4 (user memory start) */
    MfUltralightPage page;
    MfUltralightError err = mf_ultralight_poller_sync_read_page(app->nfc, NFC_NTAG_DATA_PAGE, &page);
    if(err != MfUltralightErrorNone) return false;

    app->nfc_detected = NfcProtocolMfUltralight;
    app->nfc_read_fc = page.data[0];
    app->nfc_read_cn = ((uint16_t)page.data[1] << 8) | page.data[2];

    /* Get version to identify NTAG subtype */
    MfUltralightVersion ver;
    if(mf_ultralight_poller_sync_read_version(app->nfc, &ver) == MfUltralightErrorNone) {
        app->nfc_ntag_type = mf_ultralight_get_type_by_version(&ver);
    } else {
        app->nfc_ntag_type = MfUltralightTypeOrigin;
    }

    snprintf(app->nfc_proto_name, sizeof(app->nfc_proto_name), "%s",
             ntag_type_name(app->nfc_ntag_type));

    FURI_LOG_I(TAG, "NFC NTAG read: %s FC=%d CN=%d",
        app->nfc_proto_name, app->nfc_read_fc, app->nfc_read_cn);
    return true;
}

/** Try to detect and read a Mifare Classic card. Returns true on success. */
static bool nfc_try_read_mf_classic(BulkWriterV2App* app) {
    MfClassicType mf_type;
    MfClassicError err = mf_classic_poller_sync_detect_type(app->nfc, &mf_type);
    if(err != MfClassicErrorNone) return false;

    /* Try to read block 4 (sector 1) with default key A */
    MfClassicKey default_key = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    MfClassicBlock block;
    err = mf_classic_poller_sync_read_block(
        app->nfc, NFC_MFC_DATA_BLOCK, &default_key, MfClassicKeyTypeA, &block);
    if(err != MfClassicErrorNone) return false;

    app->nfc_detected = NfcProtocolMfClassic;
    app->nfc_mf_classic_type = mf_type;
    app->nfc_read_fc = block.data[0];
    app->nfc_read_cn = ((uint16_t)block.data[1] << 8) | block.data[2];

    snprintf(app->nfc_proto_name, sizeof(app->nfc_proto_name), "%s",
             mf_classic_type_name(mf_type));

    FURI_LOG_I(TAG, "NFC MfClassic read: %s FC=%d CN=%d",
        app->nfc_proto_name, app->nfc_read_fc, app->nfc_read_cn);
    return true;
}

/** Write FC/CN to an NTAG card. Returns true on success. */
static bool nfc_write_ntag(BulkWriterV2App* app, uint8_t fc, uint16_t cn) {
    MfUltralightPage page;
    page.data[0] = fc;
    page.data[1] = (cn >> 8) & 0xFF;
    page.data[2] = cn & 0xFF;
    page.data[3] = NFC_BW_MARKER;

    MfUltralightError err = mf_ultralight_poller_sync_write_page(
        app->nfc, NFC_NTAG_DATA_PAGE, &page);

    FURI_LOG_I(TAG, "NFC NTAG write: FC=%d CN=%d result=%d", fc, cn, err);
    return (err == MfUltralightErrorNone);
}

/** Write FC/CN to a Mifare Classic card. Returns true on success. */
static bool nfc_write_mf_classic(BulkWriterV2App* app, uint8_t fc, uint16_t cn) {
    MfClassicKey default_key = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

    /* Read existing block data first to preserve other bytes */
    MfClassicBlock block;
    MfClassicError err = mf_classic_poller_sync_read_block(
        app->nfc, NFC_MFC_DATA_BLOCK, &default_key, MfClassicKeyTypeA, &block);
    if(err != MfClassicErrorNone) {
        /* If read fails, start with zeros */
        memset(&block, 0, sizeof(block));
    }

    /* Modify FC/CN bytes */
    block.data[0] = fc;
    block.data[1] = (cn >> 8) & 0xFF;
    block.data[2] = cn & 0xFF;
    block.data[3] = NFC_BW_MARKER;

    err = mf_classic_poller_sync_write_block(
        app->nfc, NFC_MFC_DATA_BLOCK, &default_key, MfClassicKeyTypeA, &block);

    FURI_LOG_I(TAG, "NFC MfClassic write: FC=%d CN=%d result=%d", fc, cn, err);
    return (err == MfClassicErrorNone);
}

/** Background thread entry point for NFC operations */
static int32_t nfc_operation_thread(void* context) {
    BulkWriterV2App* app = context;
    app->nfc_success = false;
    app->nfc_detected = NfcProtocolInvalid;

    switch(app->nfc_op) {
    case NfcOp_RefScan: {
        /* Reference scan: detect card and read FC/CN */
        if(!app->nfc_cancel && nfc_try_read_ntag(app)) {
            app->nfc_success = true;
        } else if(!app->nfc_cancel && nfc_try_read_mf_classic(app)) {
            app->nfc_success = true;
        }
        break;
    }

    case NfcOp_ReadAndWrite: {
        /* Bulk operation: read card, modify FC/CN, write back */
        bool read_ok = false;

        /* Detect card type — try NTAG first (more common writable NFC) */
        if(!app->nfc_cancel && nfc_try_read_ntag(app)) {
            read_ok = true;
        } else if(!app->nfc_cancel && nfc_try_read_mf_classic(app)) {
            read_ok = true;
        }

        if(!read_ok || app->nfc_cancel) break;

        /* Determine new card number */
        uint16_t new_cn = app->nfc_read_cn;
        switch(app->card_num_mode) {
            case CardNumMode_Preserve:
                new_cn = app->nfc_read_cn;
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

        /* Write back with new FC/CN */
        if(app->nfc_detected == NfcProtocolMfUltralight) {
            app->nfc_success = nfc_write_ntag(app, app->facility_code, new_cn);
        } else if(app->nfc_detected == NfcProtocolMfClassic) {
            app->nfc_success = nfc_write_mf_classic(app, app->facility_code, new_cn);
        }

        if(app->nfc_success) {
            app->cards_written++;
            app->current_screen = Screen_Success;
        } else {
            app->cards_failed++;
            app->current_screen = Screen_Error;
        }
        break;
    }
    }

    app->nfc_thread_running = false;

    /* Signal main loop */
    InputEvent event = {.type = InputTypePress, .key = InputKeyMAX};
    furi_message_queue_put(app->event_queue, &event, 0);
    return 0;
}

/** Start an NFC operation in the background thread */
static void app_start_nfc_op(BulkWriterV2App* app, NfcOpType op) {
    if(app->nfc_thread_running) return;

    app->nfc_op = op;
    app->nfc_cancel = false;
    app->nfc_success = false;
    app->nfc_thread_running = true;
    furi_thread_start(app->nfc_thread);
}

/** Stop NFC operations and wait for thread to finish */
static void app_stop_nfc(BulkWriterV2App* app) {
    if(!app->nfc_thread_running) return;
    app->nfc_cancel = true;
    furi_thread_join(app->nfc_thread);
    app->nfc_thread_running = false;
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  CORE PROCESSING LOGIC (LF — from V1)
 * ══════════════════════════════════════════════════════════════════════════════ */

/**
 * Process a read LF tag: extract FC/CN, substitute FC, optionally adjust CN,
 * encode back, and initiate write.
 */
static bool app_process_lf_tag(BulkWriterV2App* app) {
    uint8_t orig_fc = 0;
    uint16_t orig_cn = 0;
    bool extracted = false;
    bool is_hid = false;

    const char* proto_name = protocol_dict_get_name(app->protocol_dict, app->last_lf_protocol);
    FURI_LOG_I(TAG, "LF Processing: %s (size=%zu)", proto_name, app->last_lf_data_size);

    /* Try HID H10301 (3 bytes = 26-bit) */
    if(app->last_lf_data_size == 3) {
        extracted = hid_h10301_extract(app->last_lf_data, app->last_lf_data_size, &orig_fc, &orig_cn);
        if(extracted) is_hid = true;
    }

    /* Try EM4100 (5 bytes) */
    if(!extracted && app->last_lf_data_size >= 5) {
        extracted = em4100_extract(app->last_lf_data, app->last_lf_data_size, &orig_fc, &orig_cn);
    }

    if(!extracted) {
        FURI_LOG_E(TAG, "Could not extract FC/CN");
        return false;
    }

    FURI_LOG_I(TAG, "Original: FC=%d CN=%d", orig_fc, orig_cn);

    /* Determine new card number */
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
    uint8_t write_data[16];
    memcpy(write_data, app->last_lf_data, app->last_lf_data_size);

    bool encoded;
    if(is_hid) {
        encoded = hid_h10301_encode(write_data, app->last_lf_data_size, app->facility_code, new_cn);
    } else {
        encoded = em4100_encode(write_data, app->last_lf_data_size, app->facility_code, new_cn);
    }

    if(!encoded) {
        FURI_LOG_E(TAG, "Encoding failed");
        return false;
    }

    FURI_LOG_I(TAG, "New: FC=%d CN=%d", app->facility_code, new_cn);

    /* Load into protocol dict and start write */
    protocol_dict_set_data(app->protocol_dict, app->last_lf_protocol, write_data, app->last_lf_data_size);
    app->worker_busy = true;
    lfrfid_worker_write_start(app->lf_worker, app->last_lf_protocol, lfrfid_write_callback, app);
    return true;
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  READ TYPE / START / STOP
 * ══════════════════════════════════════════════════════════════════════════════ */

static LFRFIDWorkerReadType app_get_lf_read_type(BulkWriterV2App* app) {
    if(app->ref_scanned && !app->ref_is_nfc) {
        return app->ref_read_type;
    }
    switch(app->mod_select) {
        case Mod_ASK: return LFRFIDWorkerReadTypeASKOnly;
        case Mod_PSK: return LFRFIDWorkerReadTypePSKOnly;
        default:      return LFRFIDWorkerReadTypeAuto;
    }
}

/** Start the read→write processing loop */
static void app_start_processing(BulkWriterV2App* app) {
    app->running = true;
    app->current_screen = Screen_Ready;

    bool use_nfc = (app->reader_type == ReaderType_NFC) ||
                   (app->reader_type == ReaderType_Auto && app->ref_scanned && app->ref_is_nfc);

    if(use_nfc) {
        /* NFC mode */
        app_start_nfc_op(app, NfcOp_ReadAndWrite);
    } else {
        /* LF mode */
        LFRFIDWorkerReadType read_type = app_get_lf_read_type(app);
        lfrfid_worker_read_start(app->lf_worker, read_type, lfrfid_read_callback, app);
        FURI_LOG_I(TAG, "LF Processing started (type=%d)", read_type);
    }
}

/** Stop the processing loop */
static void app_stop_processing(BulkWriterV2App* app) {
    app->running = false;
    lfrfid_worker_stop(app->lf_worker);
    app_stop_nfc(app);
    FURI_LOG_I(TAG, "Processing stopped");
}

/** Resume processing after success/error — restart read for next card */
static void app_resume_processing(BulkWriterV2App* app) {
    if(!app->running) return;
    app->current_screen = Screen_Ready;

    bool use_nfc = (app->reader_type == ReaderType_NFC) ||
                   (app->reader_type == ReaderType_Auto && app->ref_scanned && app->ref_is_nfc);

    if(use_nfc) {
        app_start_nfc_op(app, NfcOp_ReadAndWrite);
    } else {
        LFRFIDWorkerReadType read_type = app_get_lf_read_type(app);
        lfrfid_worker_read_start(app->lf_worker, read_type, lfrfid_read_callback, app);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  DRAW CALLBACKS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void app_draw_callback(Canvas* canvas, void* context) {
    BulkWriterV2App* app = context;
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

/** Config screen — FC, CN mode, reader type, modulation, reference */
static void app_draw_config(Canvas* canvas, BulkWriterV2App* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Bulk Writer V2");
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

    /* Row N: Reader type */
    const char* reader_names[] = {"Auto", "LF 125kHz", "NFC 13.56MHz"};
    snprintf(buf, sizeof(buf), "Reader: %s", reader_names[app->reader_type]);
    canvas_draw_str(canvas, 4, y, buf);
    if(app->config_cursor == row) {
        canvas_draw_str(canvas, 0, y, ">");
        canvas_draw_str(canvas, 100, y, "<L/R>");
    }
    row++; y += 10;

    /* Row N+1: Modulation / reference (only for LF modes) */
    if(app->reader_type != ReaderType_NFC) {
        if(app->ref_scanned && !app->ref_is_nfc) {
            snprintf(buf, sizeof(buf), "Ref: %s", app->ref_proto_name);
            canvas_draw_str(canvas, 4, y, buf);
            if(app->config_cursor == row) {
                canvas_draw_str(canvas, 0, y, ">");
                canvas_draw_str(canvas, 90, y, "Rescan>");
            }
        } else if(app->ref_scanned && app->ref_is_nfc) {
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
    } else {
        /* NFC-only mode: show NFC reference if available */
        if(app->ref_scanned && app->ref_is_nfc) {
            snprintf(buf, sizeof(buf), "Ref: %s", app->ref_proto_name);
        } else {
            snprintf(buf, sizeof(buf), "NFC: Scan ref >");
        }
        canvas_draw_str(canvas, 4, y, buf);
        if(app->config_cursor == row) {
            canvas_draw_str(canvas, 0, y, ">");
            canvas_draw_str(canvas, 90, y, "Scan >");
        }
    }

    /* Footer */
    elements_button_center(canvas, "Start");
}

/** Reference scan screen */
static void app_draw_ref_scan(Canvas* canvas, BulkWriterV2App* app) {
    UNUSED(app);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignCenter, "Scan Reference Card");

    canvas_set_font(canvas, FontSecondary);

    bool nfc_mode = (app->reader_type == ReaderType_NFC);
    if(nfc_mode) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Place NFC card");
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "on the reader...");
    } else {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Place 125kHz card");
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "on the reader...");
    }

    elements_button_left(canvas, "Cancel");
}

/** Reference scan result screen */
static void app_draw_ref_result(Canvas* canvas, BulkWriterV2App* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "Reference Detected!");

    canvas_set_font(canvas, FontSecondary);
    char buf[48];

    snprintf(buf, sizeof(buf), "Protocol: %s", app->ref_proto_name);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, buf);

    snprintf(buf, sizeof(buf), "FC: %d  CN: %d", app->ref_fc, app->ref_cn);
    canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, buf);

    if(app->ref_is_nfc) {
        canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, "Type: NFC 13.56MHz");
    } else {
        const char* mod = (app->ref_read_type == LFRFIDWorkerReadTypeASKOnly) ? "ASK" : "PSK";
        snprintf(buf, sizeof(buf), "Mod: %s (locked)", mod);
        canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, buf);
    }

    elements_button_center(canvas, "OK");
}

/** Ready/Reading screen */
static void app_draw_ready(Canvas* canvas, BulkWriterV2App* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignCenter, "Bulk Writer V2");

    canvas_set_font(canvas, FontSecondary);
    char buf[48];

    snprintf(buf, sizeof(buf), "FC: %d  |  Cards: %d", app->facility_code, app->cards_written);
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, buf);

    /* Reader type indicator */
    bool use_nfc = (app->reader_type == ReaderType_NFC) ||
                   (app->reader_type == ReaderType_Auto && app->ref_scanned && app->ref_is_nfc);
    if(use_nfc) {
        canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignCenter, "[NFC 13.56MHz]");
    } else {
        canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignCenter, "[LF 125kHz]");
    }

    if(app->current_screen == Screen_Reading || app->nfc_thread_running) {
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Reading tag...");
    } else {
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Place card on reader");
    }

    if(app->cards_failed > 0) {
        snprintf(buf, sizeof(buf), "Failed: %d", app->cards_failed);
        canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignCenter, buf);
    }

    elements_button_left(canvas, "Stop");
}

/** Writing in progress screen */
static void app_draw_processing(Canvas* canvas, BulkWriterV2App* app) {
    UNUSED(app);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "Writing...");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "Keep card on reader");
}

/** Success or error flash */
static void app_draw_result(Canvas* canvas, BulkWriterV2App* app) {
    canvas_set_font(canvas, FontPrimary);

    if(app->current_screen == Screen_Success) {
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "OK!");
        canvas_set_font(canvas, FontSecondary);
        char buf[32];
        snprintf(buf, sizeof(buf), "Card #%d written", app->cards_written);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, buf);

        if(app->ref_scanned) {
            char proto_buf[40];
            snprintf(proto_buf, sizeof(proto_buf), "(%s)", app->ref_proto_name);
            canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, proto_buf);
        }
    } else {
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "WRITE FAILED");
        canvas_set_font(canvas, FontSecondary);
        bool use_nfc = (app->reader_type == ReaderType_NFC) ||
                       (app->ref_scanned && app->ref_is_nfc);
        if(use_nfc) {
            canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Check NFC card is writable");
        } else {
            canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Check card is T5577");
        }
    }
}

/** Session summary screen */
static void app_draw_summary(Canvas* canvas, BulkWriterV2App* app) {
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

/* ══════════════════════════════════════════════════════════════════════════════
 *  INPUT CALLBACK
 * ══════════════════════════════════════════════════════════════════════════════ */

static void app_input_callback(InputEvent* input_event, void* context) {
    BulkWriterV2App* app = context;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  INPUT HANDLING — CONFIG SCREEN
 * ══════════════════════════════════════════════════════════════════════════════ */

static void app_handle_config_input(BulkWriterV2App* app, InputEvent* event) {
    if(event->type != InputTypePress && event->type != InputTypeRepeat) return;

    /* Calculate max cursor: FC, CN, [Base#], Reader, Mod/Ref */
    uint8_t max_cursor = (app->card_num_mode != CardNumMode_Preserve) ? 4 : 3;
    uint8_t base_row = (app->card_num_mode != CardNumMode_Preserve) ? 2 : 255;
    uint8_t reader_row = (app->card_num_mode != CardNumMode_Preserve) ? 3 : 2;
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
                /* Facility Code: 0-255 with wrap */
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

            } else if(app->config_cursor == base_row) {
                /* Card number base: 0-65535 with wrap */
                int32_t new_base = (int32_t)app->card_num_base + dir;
                if(new_base < 0) new_base = 65535;
                if(new_base > 65535) new_base = 0;
                app->card_num_base = (uint16_t)new_base;

            } else if(app->config_cursor == reader_row) {
                /* Reader type */
                int8_t new_rt = (int8_t)app->reader_type + dir;
                if(new_rt < 0) new_rt = ReaderType_COUNT - 1;
                if(new_rt >= (int8_t)ReaderType_COUNT) new_rt = 0;
                app->reader_type = (ReaderType)new_rt;

            } else if(app->config_cursor == mod_row) {
                if(event->key == InputKeyRight) {
                    /* Right = start reference scan */
                    bool nfc_ref = (app->reader_type == ReaderType_NFC);
                    app->current_screen = Screen_RefScan;

                    if(nfc_ref) {
                        /* NFC reference scan */
                        app_start_nfc_op(app, NfcOp_RefScan);
                    } else {
                        /* LF reference scan */
                        lfrfid_worker_read_start(
                            app->lf_worker, LFRFIDWorkerReadTypeAuto,
                            lfrfid_ref_scan_callback, app);
                    }
                } else {
                    /* Left = cycle modulation (clears LF ref) */
                    if(!app->ref_is_nfc) app->ref_scanned = false;
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

        default:
            break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  INPUT HANDLING — PROCESSING SCREENS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void app_handle_processing_input(BulkWriterV2App* app, InputEvent* event) {
    if(event->type != InputTypePress) return;

    switch(event->key) {
        case InputKeyLeft:
        case InputKeyBack:
            app_stop_processing(app);
            app->current_screen = Screen_Summary;
            break;
        default:
            break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  PERSISTENT SETTINGS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void app_save_settings(BulkWriterV2App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, BULKWRITER_CONFIG_DIR);

    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_always(ff, BULKWRITER_CONFIG_PATH)) {
            FURI_LOG_E(TAG, "Failed to open config for writing");
            break;
        }

        flipper_format_write_header_cstr(ff, "BulkWriter V2 Config", 2);

        uint32_t fc = app->facility_code;
        uint32_t mode = app->card_num_mode;
        uint32_t base = app->card_num_base;
        uint32_t mod = app->mod_select;
        uint32_t rt = app->reader_type;

        flipper_format_write_uint32(ff, "FacilityCode", &fc, 1);
        flipper_format_write_uint32(ff, "CardNumMode", &mode, 1);
        flipper_format_write_uint32(ff, "CardNumBase", &base, 1);
        flipper_format_write_uint32(ff, "Modulation", &mod, 1);
        flipper_format_write_uint32(ff, "ReaderType", &rt, 1);

        FURI_LOG_I(TAG, "Settings saved (FC=%lu mode=%lu base=%lu mod=%lu reader=%lu)",
            fc, mode, base, mod, rt);
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void app_load_settings(BulkWriterV2App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_existing(ff, BULKWRITER_CONFIG_PATH)) {
            FURI_LOG_I(TAG, "No saved config, using defaults");
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
        if(flipper_format_read_uint32(ff, "ReaderType", &val, 1)) {
            if(val < ReaderType_COUNT) app->reader_type = (ReaderType)val;
        }

        FURI_LOG_I(TAG, "Settings loaded (FC=%d mode=%d base=%d mod=%d reader=%d)",
            app->facility_code, app->card_num_mode, app->card_num_base,
            app->mod_select, app->reader_type);
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  ALLOC / FREE
 * ══════════════════════════════════════════════════════════════════════════════ */

static BulkWriterV2App* app_alloc(void) {
    BulkWriterV2App* app = malloc(sizeof(BulkWriterV2App));
    memset(app, 0, sizeof(BulkWriterV2App));

    /* Defaults */
    app->facility_code = FC_DEFAULT;
    app->card_num_mode = CARD_NUM_MODE_DEFAULT;
    app->card_num_base = CARD_NUM_DEFAULT;
    app->mod_select = MOD_DEFAULT;
    app->reader_type = READER_TYPE_DEFAULT;
    app->current_screen = Screen_Config;
    app->config_cursor = 0;
    app->ref_scanned = false;
    app->ref_is_nfc = false;
    app->ref_nfc_protocol = NfcProtocolInvalid;

    /* Load persisted settings */
    app_load_settings(app);

    /* Reset runtime */
    app->next_sequential = app->card_num_base;
    app->cards_written = 0;
    app->cards_failed = 0;
    app->nfc_thread_running = false;
    app->nfc_cancel = false;

    /* GUI */
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, app_draw_callback, app);
    view_port_input_callback_set(app->view_port, app_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    /* LF RFID */
    app->protocol_dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    app->lf_worker = lfrfid_worker_alloc(app->protocol_dict);
    lfrfid_worker_start_thread(app->lf_worker);

    /* NFC */
    app->nfc = nfc_alloc();
    app->nfc_thread = furi_thread_alloc_ex("BWv2NFC", 4 * 1024, nfc_operation_thread, app);

    FURI_LOG_I(TAG, "V2 App allocated, FC=%d reader=%d", app->facility_code, app->reader_type);
    return app;
}

static void app_free(BulkWriterV2App* app) {
    FURI_LOG_I(TAG, "Cleaning up...");

    /* Save settings */
    app_save_settings(app);

    /* Stop workers */
    if(app->running) {
        lfrfid_worker_stop(app->lf_worker);
    }
    app_stop_nfc(app);

    /* Free NFC */
    furi_thread_free(app->nfc_thread);
    nfc_free(app->nfc);

    /* Free LF RFID */
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

/* ══════════════════════════════════════════════════════════════════════════════
 *  MAIN ENTRY POINT
 * ══════════════════════════════════════════════════════════════════════════════ */

int32_t bulk_writer_v2_app(void* p) {
    UNUSED(p);

    BulkWriterV2App* app = app_alloc();
    InputEvent event;
    bool exit_requested = false;

    while(!exit_requested) {
        FuriStatus status = furi_message_queue_get(app->event_queue, &event, 100);

        if(status == FuriStatusOk) {
            /* Internal signal from worker callbacks (key == InputKeyMAX) */
            if(event.key == InputKeyMAX) {

                if(app->current_screen == Screen_RefScan) {
                    /* Reference scan completed */
                    lfrfid_worker_stop(app->lf_worker);

                    if(app->nfc_success && app->nfc_detected != NfcProtocolInvalid) {
                        /* NFC reference scan succeeded */
                        app->ref_scanned = true;
                        app->ref_is_nfc = true;
                        app->ref_nfc_protocol = app->nfc_detected;
                        app->ref_fc = app->nfc_read_fc;
                        app->ref_cn = app->nfc_read_cn;
                        snprintf(app->ref_proto_name, sizeof(app->ref_proto_name),
                                 "%s", app->nfc_proto_name);
                        if(app->nfc_detected == NfcProtocolMfUltralight) {
                            app->ref_ntag_type = app->nfc_ntag_type;
                        } else {
                            app->ref_mf_classic_type = app->nfc_mf_classic_type;
                        }
                    }
                    /* LF reference scan result is already stored by the callback */

                    if(app->ref_scanned) {
                        beep_success();
                        app->current_screen = Screen_RefResult;
                    } else {
                        /* Scan failed — back to config */
                        beep_error();
                        app->current_screen = Screen_Config;
                    }

                } else if(app->current_screen == Screen_Success ||
                          app->current_screen == Screen_Error) {
                    /* Success/error from a write operation */
                    if(app->current_screen == Screen_Success) {
                        notification_message(app->notifications, &seq_success);
                        beep_success();
                        view_port_update(app->view_port);
                        furi_delay_ms(800);
                    } else {
                        notification_message(app->notifications, &seq_error);
                        beep_error();
                        view_port_update(app->view_port);
                        furi_delay_ms(1200);
                    }

                    /* Auto-resume for next card */
                    app_resume_processing(app);

                } else if(app->nfc_thread_running == false && app->nfc_success &&
                          app->current_screen == Screen_Ready) {
                    /* NFC read+write completed (result already set by thread) */
                    /* The thread set Screen_Success or Screen_Error — re-signal */
                    InputEvent re_event = {.type = InputTypePress, .key = InputKeyMAX};
                    furi_message_queue_put(app->event_queue, &re_event, 0);

                } else {
                    /* LF tag was read during bulk processing — process it */
                    lfrfid_worker_stop(app->lf_worker);
                    notification_message(app->notifications, &seq_reading);
                    app->current_screen = Screen_Writing;
                    view_port_update(app->view_port);
                    notification_message(app->notifications, &seq_writing);

                    if(!app_process_lf_tag(app)) {
                        app->cards_failed++;
                        app->current_screen = Screen_Error;
                        app->worker_busy = false;
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
                        if(event.type == InputTypePress &&
                           (event.key == InputKeyBack || event.key == InputKeyLeft)) {
                            lfrfid_worker_stop(app->lf_worker);
                            app_stop_nfc(app);
                            app->current_screen = Screen_Config;
                        }
                        break;

                    case Screen_RefResult:
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
