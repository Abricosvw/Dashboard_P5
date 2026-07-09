/**
 * @file ui_Screen12.c
 * @brief VAG DSG Launch Control Diagnostics Screen
 *
 * Displays real-time Launch Control activation status and the 5 prerequisite
 * conditions read from the CAN bus (or simulated in Demo mode).
 *
 * ECU context:
 *   Bosch ME17.5.6 (MPI) / MED17 (GDI) — common CAN bus protocol.
 *   DSG (DQ200/DQ250/DQ381) TCU sends torque intervention via TSC1/TSC3.
 *
 * Launch Control conditions (MED17 Page 3394):
 *   1. Gbx_stGearLvr == 12  (Selector in Sport)
 *   2. VehV_v == 0           (Vehicle stationary)
 *   3. APP_r > threshold     (Throttle floored)
 *   4. Brk_st == 3           (Brake pedal pressed)
 *   5. Tra_stTSC.5 == 1      (TCU torque intervention active)
 */

#include "ui_Screen12.h"
#include "../ui.h"
#include "../ui_screen_manager.h"
#include "ecu_data.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SCREEN12";

// Root screen pointer
lv_obj_t *ui_Screen12 = NULL;

// --- Color palette (matches project theme) ---
#define CLR_BG         0x0A0F1A
#define CLR_PANEL      0x111827
#define CLR_BORDER     0x334155
#define CLR_CYAN       0x00D4FF
#define CLR_GREEN      0x00FF88
#define CLR_RED        0xFF3333
#define CLR_AMBER      0xFFB020
#define CLR_GOLD       0xFFD700
#define CLR_TEXT_DIM   0x94A3B8
#define CLR_TEXT_WHITE 0xF1F5F9
#define CLR_BTN_BG     0x1E293B
#define CLR_BTN_ACTIVE 0x0F172A
#define CLR_ROW_OK     0x0D2818
#define CLR_ROW_FAIL   0x1A0F0F

// Fonts
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(montserrat_20_en_ru);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_32);

// --- Widget handles for update() ---
// Main status
static lv_obj_t *status_label       = NULL;
static lv_obj_t *status_sub_label   = NULL;

// 5 condition LED dots
static lv_obj_t *cond_dots[5]       = {NULL};

// 5 condition rows
static lv_obj_t *cond_row_panels[5] = {NULL};
static lv_obj_t *cond_name_labels[5]  = {NULL};
static lv_obj_t *cond_desc_labels[5]  = {NULL};
static lv_obj_t *cond_value_labels[5] = {NULL};
static lv_obj_t *cond_icon_labels[5]  = {NULL};

// Bottom telemetry
static lv_obj_t *rpm_value_label   = NULL;
static lv_obj_t *rpm_bar           = NULL;
static lv_obj_t *map_value_label   = NULL;
static lv_obj_t *map_bar           = NULL;

// --- Condition metadata ---
static const char *cond_names[5] = {
    "GEAR LEVER",
    "VEHICLE SPEED",
    "THROTTLE PEDAL",
    "BRAKE PEDAL",
    "TCU TORQUE INT."
};

static const char *cond_descs[5] = {
    "Gbx_stGearLvr = 12 (Sport)",
    "VehV_v = 0 km/h",
    "APP_r > 80% (Floored)",
    "Brk_st = 3 (Pressed)",
    "Tra_stTSC.5 = 1 (Active)"
};

// ============================================================================
// HELPERS
// ============================================================================

static lv_obj_t *create_panel(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

// ============================================================================
// SCREEN INIT
// ============================================================================

void ui_Screen12_screen_init(void) {
    ESP_LOGI(TAG, "Initializing Screen 12 (VAG Launch Control Diagnostics)...");

    // --- ROOT SCREEN ---
    ui_Screen12 = lv_obj_create(NULL);
    lv_obj_set_size(ui_Screen12, 720, 1280);
    lv_obj_clear_flag(ui_Screen12, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen12, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(ui_Screen12, LV_OPA_COVER, 0);

    // =======================================================================
    // HEADER PANEL (y: 10..90)
    // =======================================================================
    lv_obj_t *header = create_panel(ui_Screen12, 10, 10, 700, 80);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "VAG DSG LAUNCH CONTROL");
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_font(title, &montserrat_20_en_ru, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t *subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, "ME17.5.6 MPI / MED17 GDI + DSG");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 16);

    // =======================================================================
    // MAIN STATUS CARD (y: 100..310)
    // =======================================================================
    lv_obj_t *status_panel = create_panel(ui_Screen12, 10, 100, 700, 210);

    status_label = lv_label_create(status_panel);
    lv_label_set_text(status_label, "STANDBY");
    lv_obj_set_style_text_color(status_label, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_32, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 20);

    status_sub_label = lv_label_create(status_panel);
    lv_label_set_text(status_sub_label, "0 / 5 CONDITIONS MET");
    lv_obj_set_style_text_color(status_sub_label, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(status_sub_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_sub_label, LV_ALIGN_TOP_MID, 0, 65);

    // --- 5 LED dots (centered row) ---
    int dot_size = 28;
    int dot_gap = 16;
    int total_dots_w = 5 * dot_size + 4 * dot_gap;
    int dot_start_x = (700 - total_dots_w) / 2;

    for (int i = 0; i < 5; i++) {
        cond_dots[i] = lv_obj_create(status_panel);
        lv_obj_set_size(cond_dots[i], dot_size, dot_size);
        lv_obj_set_pos(cond_dots[i], dot_start_x + i * (dot_size + dot_gap), 100);
        lv_obj_set_style_radius(cond_dots[i], dot_size / 2, 0);
        lv_obj_set_style_bg_color(cond_dots[i], lv_color_hex(CLR_TEXT_DIM), 0);
        lv_obj_set_style_bg_opa(cond_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(cond_dots[i], lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(cond_dots[i], 2, 0);
        lv_obj_clear_flag(cond_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // Condition number labels inside dots
    for (int i = 0; i < 5; i++) {
        lv_obj_t *num_lbl = lv_label_create(cond_dots[i]);
        lv_label_set_text_fmt(num_lbl, "%d", i + 1);
        lv_obj_set_style_text_color(num_lbl, lv_color_hex(CLR_TEXT_WHITE), 0);
        lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(num_lbl);
    }

    // Status glow bar under dots
    lv_obj_t *glow_bar = lv_bar_create(status_panel);
    lv_obj_set_size(glow_bar, total_dots_w + 20, 8);
    lv_obj_set_pos(glow_bar, dot_start_x - 10, 140);
    lv_bar_set_range(glow_bar, 0, 5);
    lv_bar_set_value(glow_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(glow_bar, lv_color_hex(0x1F293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(glow_bar, lv_color_hex(CLR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(glow_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(glow_bar, 4, LV_PART_INDICATOR);

    // Divider
    lv_obj_t *div1 = lv_obj_create(status_panel);
    lv_obj_set_size(div1, 660, 1);
    lv_obj_set_pos(div1, 20, 165);
    lv_obj_set_style_bg_color(div1, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_bg_opa(div1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div1, 0, 0);
    lv_obj_clear_flag(div1, LV_OBJ_FLAG_SCROLLABLE);

    // ECU type label
    lv_obj_t *ecu_label = lv_label_create(status_panel);
    lv_label_set_text(ecu_label, "Tra_stLnchCtlActv");
    lv_obj_set_style_text_color(ecu_label, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(ecu_label, &lv_font_montserrat_12, 0);
    lv_obj_align(ecu_label, LV_ALIGN_BOTTOM_MID, 0, -15);

    // =======================================================================
    // DIAGNOSTIC ROWS (y: 320..920) — 5 rows × 120px each
    // =======================================================================
    int row_y_start = 320;
    int row_height = 110;
    int row_gap = 10;

    for (int i = 0; i < 5; i++) {
        int row_y = row_y_start + i * (row_height + row_gap);

        cond_row_panels[i] = create_panel(ui_Screen12, 10, row_y, 700, row_height);

        // Left: Status icon (check / cross)
        cond_icon_labels[i] = lv_label_create(cond_row_panels[i]);
        lv_label_set_text(cond_icon_labels[i], LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(cond_icon_labels[i], lv_color_hex(CLR_RED), 0);
        lv_obj_set_style_text_font(cond_icon_labels[i], &lv_font_montserrat_24, 0);
        lv_obj_align(cond_icon_labels[i], LV_ALIGN_LEFT_MID, 20, 0);

        // Center: Condition name
        cond_name_labels[i] = lv_label_create(cond_row_panels[i]);
        lv_label_set_text(cond_name_labels[i], cond_names[i]);
        lv_obj_set_style_text_color(cond_name_labels[i], lv_color_hex(CLR_TEXT_WHITE), 0);
        lv_obj_set_style_text_font(cond_name_labels[i], &montserrat_20_en_ru, 0);
        lv_obj_align(cond_name_labels[i], LV_ALIGN_TOP_LEFT, 65, 15);

        // Center: Description
        cond_desc_labels[i] = lv_label_create(cond_row_panels[i]);
        lv_label_set_text(cond_desc_labels[i], cond_descs[i]);
        lv_obj_set_style_text_color(cond_desc_labels[i], lv_color_hex(CLR_TEXT_DIM), 0);
        lv_obj_set_style_text_font(cond_desc_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_align(cond_desc_labels[i], LV_ALIGN_TOP_LEFT, 65, 50);

        // Right: Live value
        cond_value_labels[i] = lv_label_create(cond_row_panels[i]);
        lv_label_set_text(cond_value_labels[i], "---");
        lv_obj_set_style_text_color(cond_value_labels[i], lv_color_hex(CLR_TEXT_WHITE), 0);
        lv_obj_set_style_text_font(cond_value_labels[i], &montserrat_20_en_ru, 0);
        lv_obj_align(cond_value_labels[i], LV_ALIGN_RIGHT_MID, -20, 0);

        // Right: small status label
        lv_obj_t *status_hint = lv_label_create(cond_row_panels[i]);
        lv_label_set_text(status_hint, "NOT MET");
        lv_obj_set_style_text_color(status_hint, lv_color_hex(CLR_RED), 0);
        lv_obj_set_style_text_font(status_hint, &lv_font_montserrat_12, 0);
        lv_obj_align(status_hint, LV_ALIGN_BOTTOM_LEFT, 65, -12);
    }

    // =======================================================================
    // BOTTOM TELEMETRY PANEL (y: 930..1100)
    // =======================================================================
    lv_obj_t *telem_panel = create_panel(ui_Screen12, 10, 930, 700, 170);

    // RPM gauge
    lv_obj_t *rpm_title = lv_label_create(telem_panel);
    lv_label_set_text(rpm_title, "ENGINE RPM");
    lv_obj_set_style_text_color(rpm_title, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_font(rpm_title, &lv_font_montserrat_14, 0);
    lv_obj_align(rpm_title, LV_ALIGN_TOP_LEFT, 20, 15);

    rpm_value_label = lv_label_create(telem_panel);
    lv_label_set_text(rpm_value_label, "0");
    lv_obj_set_style_text_color(rpm_value_label, lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(rpm_value_label, &montserrat_20_en_ru, 0);
    lv_obj_align(rpm_value_label, LV_ALIGN_TOP_RIGHT, -20, 10);

    rpm_bar = lv_bar_create(telem_panel);
    lv_obj_set_size(rpm_bar, 660, 16);
    lv_obj_align(rpm_bar, LV_ALIGN_TOP_MID, 0, 50);
    lv_bar_set_range(rpm_bar, 0, 8000);
    lv_bar_set_value(rpm_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(rpm_bar, lv_color_hex(0x1F293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rpm_bar, lv_color_hex(CLR_CYAN), LV_PART_INDICATOR);

    // MAP gauge
    lv_obj_t *map_title = lv_label_create(telem_panel);
    lv_label_set_text(map_title, "MAP (kPa)");
    lv_obj_set_style_text_color(map_title, lv_color_hex(CLR_GREEN), 0);
    lv_obj_set_style_text_font(map_title, &lv_font_montserrat_14, 0);
    lv_obj_align(map_title, LV_ALIGN_TOP_LEFT, 20, 80);

    map_value_label = lv_label_create(telem_panel);
    lv_label_set_text(map_value_label, "0");
    lv_obj_set_style_text_color(map_value_label, lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(map_value_label, &montserrat_20_en_ru, 0);
    lv_obj_align(map_value_label, LV_ALIGN_TOP_RIGHT, -20, 75);

    map_bar = lv_bar_create(telem_panel);
    lv_obj_set_size(map_bar, 660, 16);
    lv_obj_align(map_bar, LV_ALIGN_TOP_MID, 0, 115);
    lv_bar_set_range(map_bar, 0, 300);
    lv_bar_set_value(map_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(map_bar, lv_color_hex(0x1F293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(map_bar, lv_color_hex(CLR_GREEN), LV_PART_INDICATOR);

    // =======================================================================
    // NAVIGATION BUTTONS
    // =======================================================================
    ui_create_standard_navigation_buttons(ui_Screen12);

    ESP_LOGI(TAG, "Screen 12 (VAG Launch Control Diagnostics) successfully initialized.");
}

// ============================================================================
// SCREEN DESTROY
// ============================================================================

void ui_Screen12_screen_destroy(void) {
    ESP_LOGI(TAG, "Destroying ui_Screen12...");

    status_label = NULL;
    status_sub_label = NULL;
    rpm_value_label = NULL;
    rpm_bar = NULL;
    map_value_label = NULL;
    map_bar = NULL;

    memset(cond_dots, 0, sizeof(cond_dots));
    memset(cond_row_panels, 0, sizeof(cond_row_panels));
    memset(cond_name_labels, 0, sizeof(cond_name_labels));
    memset(cond_desc_labels, 0, sizeof(cond_desc_labels));
    memset(cond_value_labels, 0, sizeof(cond_value_labels));
    memset(cond_icon_labels, 0, sizeof(cond_icon_labels));

    if (ui_Screen12) {
        lv_obj_del(ui_Screen12);
        ui_Screen12 = NULL;
    }
}

// ============================================================================
// SCREEN UPDATE (called every frame from update_all_gauges)
// ============================================================================

void ui_Screen12_update(void) {
    if (!ui_Screen12) return;

    ecu_data_t data;
    if (demo_mode_get_enabled()) {
        ecu_data_simulate(&data);
    } else {
        ecu_data_get_copy(&data);
    }

    // Evaluate 5 conditions
    bool conditions[5];
    conditions[0] = (data.gear_lever_val == 12);           // Sport
    conditions[1] = (data.vehicle_speed < 1.0f);           // Stationary
    conditions[2] = (data.pedal_position > 80.0f);         // Throttle floored
    conditions[3] = (data.brake_status == 3);              // Brake pressed
    conditions[4] = (data.tcu_torque_intervention);        // TCU torque intervention

    int met_count = 0;
    for (int i = 0; i < 5; i++) {
        if (conditions[i]) met_count++;
    }

    // Driver preparation: Sport gear, speed is 0, brake is pressed
    bool driver_prep = conditions[0] && conditions[1] && conditions[3];

    // --- Main status label ---
    if (status_label) {
        if (data.tcu_launch_ready) {
            lv_label_set_text(status_label, "LAUNCH READY START");
            lv_obj_set_style_text_color(status_label, lv_color_hex(CLR_GREEN), 0);
        } else if (driver_prep) {
            if (data.pedal_position > 80.0f) {
                lv_label_set_text(status_label, "LAUNCH NOT READY");
                lv_obj_set_style_text_color(status_label, lv_color_hex(CLR_RED), 0);
            } else {
                lv_label_set_text(status_label, "PRELAUNCH (HOLD BRAKE)");
                lv_obj_set_style_text_color(status_label, lv_color_hex(CLR_AMBER), 0);
            }
        } else {
            lv_label_set_text(status_label, "STANDBY");
            lv_obj_set_style_text_color(status_label, lv_color_hex(CLR_TEXT_DIM), 0);
        }
    }

    if (status_sub_label) {
        lv_label_set_text_fmt(status_sub_label, "%d / 5 CONDITIONS MET", met_count);
        if (data.tcu_launch_ready) {
            lv_obj_set_style_text_color(status_sub_label, lv_color_hex(CLR_GREEN), 0);
        } else if (driver_prep) {
            if (data.pedal_position > 80.0f) {
                lv_obj_set_style_text_color(status_sub_label, lv_color_hex(CLR_RED), 0);
            } else {
                lv_obj_set_style_text_color(status_sub_label, lv_color_hex(CLR_AMBER), 0);
            }
        } else {
            lv_obj_set_style_text_color(status_sub_label, lv_color_hex(CLR_TEXT_DIM), 0);
        }
    }

    // --- LED dots ---
    for (int i = 0; i < 5; i++) {
        if (cond_dots[i]) {
            if (conditions[i]) {
                lv_obj_set_style_bg_color(cond_dots[i], lv_color_hex(CLR_GREEN), 0);
                lv_obj_set_style_border_color(cond_dots[i], lv_color_hex(CLR_GREEN), 0);
            } else {
                lv_obj_set_style_bg_color(cond_dots[i], lv_color_hex(0x2A2A2A), 0);
                lv_obj_set_style_border_color(cond_dots[i], lv_color_hex(CLR_BORDER), 0);
            }
        }
    }

    // --- Glow bar ---
    lv_obj_t *status_panel = lv_obj_get_child(ui_Screen12, 1);
    if (status_panel) {
        lv_obj_t *glow = lv_obj_get_child(status_panel, 7);
        if (glow) {
            lv_bar_set_value(glow, met_count, LV_ANIM_OFF);
            if (data.tcu_launch_ready) {
                lv_obj_set_style_bg_color(glow, lv_color_hex(CLR_GREEN), LV_PART_INDICATOR);
            } else if (driver_prep) {
                if (data.pedal_position > 80.0f) {
                    lv_obj_set_style_bg_color(glow, lv_color_hex(CLR_RED), LV_PART_INDICATOR);
                } else {
                    lv_obj_set_style_bg_color(glow, lv_color_hex(CLR_AMBER), LV_PART_INDICATOR);
                }
            } else {
                lv_obj_set_style_bg_color(glow, lv_color_hex(CLR_TEXT_DIM), LV_PART_INDICATOR);
            }
        }
    }

    // --- Condition rows ---
    // Build live value strings
    char value_strs[5][32];
    // 1) Gear lever
    const char *gear_name;
    switch (data.gear_lever_val) {
        case 5:
        case 14: gear_name = "P"; break;
        case 6:
        case 15: gear_name = "R"; break;
        case 7:
        case 13: gear_name = "N"; break;
        case 8:
        case 11: gear_name = "D"; break;
        case 12: gear_name = "S"; break;
        default: gear_name = "?"; break;
    }
    snprintf(value_strs[0], sizeof(value_strs[0]), "%s (%d)", gear_name, data.gear_lever_val);

    // 2) Vehicle speed
    snprintf(value_strs[1], sizeof(value_strs[1]), "%.0f km/h", data.vehicle_speed);

    // 3) Pedal position
    snprintf(value_strs[2], sizeof(value_strs[2]), "%.0f%%", data.pedal_position);

    // 4) Brake status
    snprintf(value_strs[3], sizeof(value_strs[3]), "%s (%d)",
             data.brake_status == 3 ? "ON" : "OFF", data.brake_status);

    // 5) TCU torque intervention
    snprintf(value_strs[4], sizeof(value_strs[4]), "%s",
             data.tcu_torque_intervention ? "ACTIVE" : "INACTIVE");

    for (int i = 0; i < 5; i++) {
        // Value text
        if (cond_value_labels[i]) {
            lv_label_set_text(cond_value_labels[i], value_strs[i]);
            lv_obj_set_style_text_color(cond_value_labels[i],
                conditions[i] ? lv_color_hex(CLR_GREEN) : lv_color_hex(CLR_TEXT_WHITE), 0);
        }

        // Icon (check / cross)
        if (cond_icon_labels[i]) {
            lv_label_set_text(cond_icon_labels[i],
                conditions[i] ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(cond_icon_labels[i],
                conditions[i] ? lv_color_hex(CLR_GREEN) : lv_color_hex(CLR_RED), 0);
        }

        // Row background tint
        if (cond_row_panels[i]) {
            lv_obj_set_style_bg_color(cond_row_panels[i],
                conditions[i] ? lv_color_hex(CLR_ROW_OK) : lv_color_hex(CLR_PANEL), 0);
            lv_obj_set_style_border_color(cond_row_panels[i],
                conditions[i] ? lv_color_hex(CLR_GREEN) : lv_color_hex(CLR_BORDER), 0);
        }

        // Status hint (last child of the row panel, index 4)
        if (cond_row_panels[i]) {
            lv_obj_t *hint = lv_obj_get_child(cond_row_panels[i], 4);
            if (hint) {
                lv_label_set_text(hint, conditions[i] ? "MET" : "NOT MET");
                lv_obj_set_style_text_color(hint,
                    conditions[i] ? lv_color_hex(CLR_GREEN) : lv_color_hex(CLR_RED), 0);
            }
        }
    }

    // --- Bottom telemetry ---
    if (rpm_value_label) {
        lv_label_set_text_fmt(rpm_value_label, "%.0f", data.engine_rpm);
    }
    if (rpm_bar) {
        int rpm_val = (int)data.engine_rpm;
        if (rpm_val > 8000) rpm_val = 8000;
        lv_bar_set_value(rpm_bar, rpm_val, LV_ANIM_OFF);
        // Color RPM bar based on launch state
        if (met_count == 5) {
            lv_obj_set_style_bg_color(rpm_bar, lv_color_hex(CLR_GREEN), LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_bg_color(rpm_bar, lv_color_hex(CLR_CYAN), LV_PART_INDICATOR);
        }
    }

    if (map_value_label) {
        lv_label_set_text_fmt(map_value_label, "%.0f", data.map_kpa);
    }
    if (map_bar) {
        int map_val = (int)data.map_kpa;
        if (map_val > 300) map_val = 300;
        lv_bar_set_value(map_bar, map_val, LV_ANIM_OFF);
    }
}
