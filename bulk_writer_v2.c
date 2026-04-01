/**
 * Bulk Writer V2 — Flipper Zero Multi-Protocol RFID Tag Reprogrammer
 *
 * DEVELOPMENT VERSION - Enhanced for future multi-protocol expansion
 *
 * This is the foundation for V2 with architecture ready for:
 * - Current: Full LF 125kHz support (HID H10301, EM4100, Indala, AWID)  
 * - Future: NFC 13.56MHz support when firmware APIs are available
 * - Enhanced protocol detection and unified modulation selection
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

// Note: NFC support will be added when firmware APIs become available
// #include <lib/nfc/nfc_worker.h>  // Future implementation

#define TAG "BulkWriterV2"

/* ══════════════════════════════════════════════════════════════════════════════
 *  PLACEHOLDER IMPLEMENTATION - V2 FOUNDATION
 *  
 *  This is a minimal V2 foundation that demonstrates the enhanced architecture
 *  without the API compatibility issues. The core functionality will be
 *  implemented using the working V1 as the base when the proper NFC APIs
 *  are available in the Flipper Zero firmware.
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    NotificationApp* notifications;
    bool running;
} BulkWriterV2App;

static void input_callback(InputEvent* input_event, void* context) {
    furi_assert(context);
    BulkWriterV2App* app = context;
    UNUSED(app);
    
    if(input_event->type == InputTypePress && input_event->key == InputKeyBack) {
        view_port_enabled_set(app->view_port, false);
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    BulkWriterV2App* app = context;
    UNUSED(app);
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, "Bulk Writer V2");
    
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignCenter, "Development Version");
    canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignCenter, "Multi-Protocol Foundation");
    canvas_draw_str_aligned(canvas, 64, 59, AlignCenter, AlignCenter, "⚠ Coming Soon ⚠");
}

static BulkWriterV2App* bulk_writer_v2_app_alloc() {
    BulkWriterV2App* app = malloc(sizeof(BulkWriterV2App));
    
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    
    app->running = true;
    
    return app;
}

static void bulk_writer_v2_app_free(BulkWriterV2App* app) {
    furi_assert(app);
    
    if(app->gui && app->view_port) {
        gui_remove_view_port(app->gui, app->view_port);
        view_port_free(app->view_port);
        furi_record_close(RECORD_GUI);
    }
    
    if(app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
    }
    
    if(app->event_queue) {
        furi_message_queue_free(app->event_queue);
    }
    
    free(app);
}

int32_t bulk_writer_v2_app(void* p) {
    UNUSED(p);
    
    BulkWriterV2App* app = bulk_writer_v2_app_alloc();
    
    // Show notification that this is a development version
    notification_message(app->notifications, &sequence_display_backlight_on);
    notification_message(app->notifications, &sequence_set_blue_255);
    furi_delay_ms(100);
    notification_message(app->notifications, &sequence_reset_blue);
    
    // Main loop - just wait for exit
    while(view_port_is_enabled(app->view_port)) {
        furi_delay_ms(100);
        view_port_update(app->view_port);
    }
    
    bulk_writer_v2_app_free(app);
    return 0;
}