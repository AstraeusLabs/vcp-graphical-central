/*
 * Copyright (c) 2024 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "lcd.h"
#include "ble.h"


static bool target_device_connected[BLE_CONN_CNT];
static bool target_device_vcp_discovered[BLE_CONN_CNT];
static bool connect_all_targets;
static bool vcp_discover_all_targets;
static bool all_devices_detected;
static uint8_t aics_inst_cnt[VCP_MAX_AICS_INST];
static uint8_t vocs_inst_cnt[VCP_MAX_VOCS_INST];
static uint8_t aics_mute[VCP_MAX_AICS_INST];
static int8_t aics_gain[VCP_MAX_AICS_INST];
static int16_t vocs_offset[VCP_MAX_VOCS_INST];
static uint8_t vcs_volume = 0;
static uint8_t vcs_mute = 0;

static lv_obj_t *scr;
static lv_obj_t *vcs_volume_slider;
static lv_obj_t *vcs_voice_icon;
static lv_obj_t *vocs_slider[VCP_MAX_VOCS_INST];
static lv_obj_t *vocs_voice_icon[VCP_MAX_VOCS_INST];
static lv_obj_t *aics_slider[VCP_MAX_AICS_INST];
static lv_obj_t *aics_voice_icon[VCP_MAX_AICS_INST];
static lv_obj_t *msg_label;

#if (BLE_CONN_CNT == 2)
static bool vocs_offset_changed;
static bool aics_gain_changed;
static bool aics_mute_changed;
static bool vcs_volume_changed;
static bool vcs_mute_changed;
#endif


static void vcs_volume_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);

    vcs_volume = value;
    ble_update_volume(conn_tgt, value);

#if (BLE_CONN_CNT == 2)
    vcs_volume_changed = true;
#endif
}

static void vocs_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);

    for (uint8_t i = 0; i < VCP_MAX_VOCS_INST; i++) {
        if(slider == vocs_slider[i]) {
            vocs_offset[i] = value;
            ble_update_vocs_offset(conn_tgt, i, value);

#if (BLE_CONN_CNT == 2)
            vocs_offset_changed = true;
#endif
        }
    }
}

static void aics_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);

    for (uint8_t i = 0; i < VCP_MAX_AICS_INST; i++) {
        if(slider == aics_slider[i]) {
            aics_gain[i] = value;
            ble_update_aics_gain(conn_tgt, i, value);

#if (BLE_CONN_CNT == 2)
            aics_gain_changed = true;
#endif
       }
    }
}

static void vcs_voice_icon_event_cb(lv_event_t *e)
{
    lv_obj_t *icon = lv_event_get_target(e);

    vcs_mute = !vcs_mute;
    ble_update_volume_mute(conn_tgt, vcs_mute);
    lcd_change_voice_icon(icon, vcs_mute);

#if (BLE_CONN_CNT == 2)
    vcs_mute_changed = true;
#endif
}

static void aics_voice_icon_event_cb(lv_event_t *e)
{
    lv_obj_t *icon = lv_event_get_target(e);

    for (uint8_t i = 0; i < VCP_MAX_AICS_INST; i++) {
        if(icon == aics_voice_icon[i]) {
            aics_mute[i] = !aics_mute[i];
            ble_update_aics_mute(conn_tgt, i, aics_mute[i]);
            lcd_change_voice_icon(icon, aics_mute[i]);

#if (BLE_CONN_CNT == 2)
            aics_mute_changed = true;
#endif
        }
    }
}

static void create_sliders(void)
{
    char txt[10];
    lv_obj_t *vcs_volume_label;
    lv_obj_t *vocs_label[VCP_MAX_VOCS_INST];
    lv_obj_t *aics_label[VCP_MAX_AICS_INST];
    lv_coord_t scr_y = LCD_Y_MIN;
    const int dist = (LCD_Y_MAX - LCD_Y_MIN) / (VCP_MAX_VOCS_INST + VCP_MAX_AICS_INST + 2);

    lcd_clear_screen(scr);

    snprintf(txt, sizeof(txt), "Volume");
    scr_y += dist;

    vcs_volume_slider = lcd_create_slider(scr, VOLUME_MIN, VOLUME_MAX, 10, scr_y,
                                          vcs_volume_slider_event_cb);

    vcs_volume_label = lcd_create_label(scr, txt, -120, scr_y);
    vcs_voice_icon = lcd_create_voice_icon(scr, 125, scr_y, vcs_voice_icon_event_cb);

    for (uint8_t i = 0; i < VCP_MAX_VOCS_INST; ++i) {
#if (VCP_MAX_VOCS_INST == 1)
        snprintf(txt, sizeof(txt), "Balance");
#else
        snprintf(txt, sizeof(txt), "VOCS-%d", i);
#endif
        scr_y += dist;

        vocs_slider[i] = lcd_create_slider(scr, VOCS_OFFSET_MIN, VOCS_OFFSET_MAX, 10, scr_y,
                                           vocs_slider_event_cb);

        vocs_label[i] = lcd_create_label(scr, txt, -120, scr_y);
        vocs_voice_icon[i] = lcd_create_balance_icon(scr, 125, scr_y, NULL);
    }

    for (uint8_t i = 0; i < VCP_MAX_AICS_INST; ++i) {
#if (VCP_MAX_VOCS_INST == 1)
        snprintf(txt, sizeof(txt), "AICS");
#else
        snprintf(txt, sizeof(txt), "AICS-%d", i);
#endif
        scr_y += dist;

        aics_slider[i] = lcd_create_slider(scr, AICS_GAIN_MIN, AICS_GAIN_MAX, 10, scr_y,
                                           aics_slider_event_cb);

        aics_label[i] = lcd_create_label(scr, txt, -120, scr_y);
        aics_voice_icon[i] = lcd_create_voice_icon(scr, 125, scr_y, aics_voice_icon_event_cb);
    }
}

static int connect_first_disconnected_devic(uint8_t start_conn_idx)
{
    int err;
    uint8_t conn_idx = start_conn_idx;

    do {
        err = ble_connect(conn_idx);
        if (err < 0) {
            lcd_display_message(msg_label, "Connection failed!");
            return -1;
        }
    } while ((err > 0) && (++conn_idx < BLE_CONN_CNT));

    return 0;
}

static void scan_btn_event_cb(lv_event_t *e)
{
    connect_all_targets = false;
    all_devices_detected = false;

    int err = ble_start_scan();
    if (err < 0) {
        lcd_display_message(msg_label, "Start scanning failed!");
        return;
    }

    lcd_display_message(msg_label, "Scanning started.");
}

static void connect_btn_event_cb(lv_event_t *e)
{
    connect_all_targets = true;
    lcd_display_message(msg_label, "Connecting...");

    if (all_devices_detected) {
        uint8_t first_conn_idx = 0;
        int err = connect_first_disconnected_devic(first_conn_idx);
        if (err) {
            lcd_display_message(msg_label, "Connection failed!");
        }
    } else {
        int err = ble_start_scan_force();
        if (err) {
            lcd_display_message(msg_label, "Start scanning failed!");
        }
    }
}

static void discover_btn_event_cb(lv_event_t *e)
{
    for (uint8_t i = 0; i < BLE_CONN_CNT; i++) {
        target_device_vcp_discovered[i] = false;
    }

    vcp_discover_all_targets = true;
    uint8_t first_conn_idx = 0;
    int err = ble_vcp_discover(first_conn_idx);
    if (err) {
        char txt[50];
        snprintf(txt, sizeof(txt), "Connection %d: VCP discover failed!", first_conn_idx);
        lcd_display_message(msg_label, txt);
        return;
    }

    lcd_display_message(msg_label, "Start discovering VCP...");
}

static void disconnect_btn_event_cb(lv_event_t *e)
{
    for (uint8_t i = 0; i < BLE_CONN_CNT; i++) {
        if (target_device_connected[i]) {
            int err = ble_disconnect(i);
            if (err) {
                char txt[50];
                snprintf(txt, sizeof(txt), "Connection %d: failed to disconnect!", i);
                lcd_display_message(msg_label, txt);
            }
        }
    }

    connect_all_targets = false;
    vcp_discover_all_targets = false;
}

static void create_buttons_before_connecting(void)
{
    lv_obj_t *connect_btn, *scan_btn;

    lcd_clear_screen(scr);

    connect_btn = lcd_create_button(scr, "Connect", 100, 50, -60, -20, connect_btn_event_cb);
    scan_btn = lcd_create_button(scr, "Scan", 100, 50, 60, -20, scan_btn_event_cb);

    msg_label = lcd_create_label(scr, "Not connected.", 0, 50);
}

static void create_buttons_after_connecting(void)
{
    lv_obj_t *discover_btn, *disconnect_btn;

    lcd_clear_screen(scr);

    discover_btn = lcd_create_button(scr, "VCP Discover", 160, 50, 0, 0, discover_btn_event_cb);
    disconnect_btn = lcd_create_button(scr, "Disconnect", 120, 40, -75, -75,
                                       disconnect_btn_event_cb);

    msg_label = lcd_create_label(scr, "Connected.", 0, 70);
}

static void create_buttons(conn_status_t all_conn)
{
    static bool buttons_before_connecting_created = false;
    static bool buttons_after_connecting_created = false;

    if (all_conn != conn_connected) {
        if (!buttons_before_connecting_created) {
            create_buttons_before_connecting();
            buttons_before_connecting_created = true;
            buttons_after_connecting_created = false;
        }
    } else {
        if(!buttons_after_connecting_created) {
            create_buttons_after_connecting();
            buttons_before_connecting_created = false;
            buttons_after_connecting_created = true;
        }
    }
}

static void scan_device_status(scan_status_t scan_st,
                               const char *dev_name)
{
    switch (scan_st) {
    case scan_available:
        char txt[MAX_DEVICE_NAME_LEN + 20];
        snprintf(txt, sizeof(txt), "Found device: %s", dev_name);
        lcd_display_message(msg_label, txt);
        break;
    case scan_done:
        all_devices_detected = true;
        printk("All devices found.\n");
        lcd_display_message(msg_label, "All devices found.");

        if (connect_all_targets) {
            lcd_display_message(msg_label, "Connecting...");

            uint8_t first_conn_idx = 0;
            int err = connect_first_disconnected_devic(first_conn_idx);
            if (err) {
                lcd_display_message(msg_label, "Connection failed!");
            }
        }
        break;
    case scan_timeout:
        printk("Some devices not found!\n");
        lcd_display_message(msg_label, "Scan timeout!\nSome devices not found!");
        break;
    default:
        printk("Unknown scan status!\n");
        break;
    }
}

static void device_connection_status(uint8_t conn_idx, conn_status_t conn_st)
{
    if (conn_idx >= BLE_CONN_CNT) {
        printk("Connection index is not valid!\n");
        return;
    }

    switch (conn_st) {
    case conn_connected:
        target_device_connected[conn_idx] = true;
        printk("Device %d connected successfully.\n", conn_idx);

        if (connect_all_targets) {
            uint8_t next_conn = conn_idx + 1;
            if (next_conn < BLE_CONN_CNT) {
                if(!target_device_connected[next_conn]) {
                    int err = connect_first_disconnected_devic(next_conn);
                    if (err) {
                        char txt[50];
                        snprintf(txt, sizeof(txt), "Connection %d: failed!", next_conn);
                        lcd_display_message(msg_label, txt);
                        return;
                    }
                }
            }
        }
        break;
    case conn_disconnected:
        target_device_connected[conn_idx] = false;
        connect_all_targets = false;
        all_devices_detected = false;

        printk("Device %d disconnected successfully.\n", conn_idx);
        create_buttons(conn_disconnected);
        break;
    default:
        printk("Connection status value is not valid!\n");
        return;
    }

    for (uint8_t i = 0; i < BLE_CONN_CNT; i++) {
        if (!target_device_connected[i]) {
            return;
        }
    }

    connect_all_targets = false;
    printk("All devices connected successfully.\n");
    create_buttons(conn_connected);
}

static void vcp_status(vcp_type_t cb_type, void *vcp_user_data)
{
#if (BLE_CONN_CNT == 2)
    uint8_t next_conn_idx;
#endif

    switch (cb_type) {
    case vcp_discover:
        vcp_discover_t *disc_data = (vcp_discover_t *)vcp_user_data;

        if (disc_data->conn_idx >= BLE_CONN_CNT) {
            printk("Connection index is not valid!\n");
            return;
        }

        if (disc_data->err != 0) {
            printk("Connection %d: VCP discover get failed (%d)!\n",
                   disc_data->conn_idx, disc_data->err);
            return;
        }

        vocs_inst_cnt[disc_data->conn_idx] = disc_data->vocs_count;
        aics_inst_cnt[disc_data->conn_idx] = disc_data->aics_count;

        target_device_vcp_discovered[disc_data->conn_idx] = true;
        printk("Connection %d: VCP discovered successfully\n", disc_data->conn_idx);

        if (vcp_discover_all_targets) {
            uint8_t next_conn = disc_data->conn_idx + 1;
            if (next_conn < BLE_CONN_CNT) {
                if(!target_device_vcp_discovered[next_conn]) {
                    int err = ble_vcp_discover(next_conn);
                    if (err) {
                        char txt[50];
                        snprintf(txt, sizeof(txt), "Connection %d: VCP discover failed!",
                                 next_conn);
                        lcd_display_message(msg_label, txt);
                        return;
                    }
                }
            }
        }

        for (uint8_t i = 0; i < BLE_CONN_CNT; i++) {
            if (!target_device_vcp_discovered[i]) {
                return;
            }
        }

        printk("VCP discovered for all devices successfully.\n");
        create_sliders();
        break;
    case vcp_vcs_vol_state:
        vcp_vol_state_t *vcs_state = (vcp_vol_state_t *)vcp_user_data;

        if (vcs_state->conn_idx >= BLE_CONN_CNT) {
            printk("Connection index is not valid!\n");
            return;
        }

        if (vcs_state->err != 0) {
            printk("VOCS state get failed (%d)\n", vcs_state->err);
            return;
        }

        printk("Connection %d: VCS volume = %u, mute = %u\n",
               vcs_state->conn_idx, vcs_state->volume, vcs_state->mute);

#if (BLE_CONN_CNT == 2)
        next_conn_idx = (vcs_state->conn_idx != conn_rshi) ? conn_rshi : conn_lshi;

        if (vcs_volume_changed || (vcs_volume != vcs_state->volume)) {
            ble_update_volume(next_conn_idx, vcs_state->volume);
            vcs_volume_changed = false;
        }

        if (vcs_mute_changed || (vcs_mute != vcs_state->mute)) {
            ble_update_volume_mute(next_conn_idx, vcs_state->mute);
            vcs_mute_changed = false;
        }
#endif
        vcs_volume = vcs_state->volume;
        vcs_mute = vcs_state->mute;

        lv_slider_set_value(vcs_volume_slider, vcs_volume, LV_ANIM_OFF);
        lcd_change_voice_icon(vcs_voice_icon, vcs_mute);
        break;
    case vcp_vocs_state:
        vcp_vocs_state_t *vocs_state = (vcp_vocs_state_t *)vcp_user_data;

        if (vocs_state->conn_idx >= BLE_CONN_CNT) {
            printk("Connection index is not valid!\n");
            return;
        }

        if (vocs_state->inst_idx >= vocs_inst_cnt[vocs_state->conn_idx]) {
            printk("VOCS inst. index is not valid!\n");
            return;
        }

        if (vocs_state->err != 0) {
            printk("VOCS state get failed (%d) for inst. index %d\n",
                   vocs_state->err, vocs_state->inst_idx);
            return;
        }

        printk("Connection %d: VOCS-%d offset = %d\n",
               vocs_state->conn_idx, vocs_state->inst_idx, vocs_state->offset);

#if (BLE_CONN_CNT == 1)
        vocs_offset[vocs_state->inst_idx] = vocs_state->offset;
#elif (BLE_CONN_CNT == 2)
        int16_t new_offset = (vocs_state->conn_idx != conn_rshi) ?
                             -(vocs_state->offset) : vocs_state->offset;

        next_conn_idx = (vocs_state->conn_idx != conn_rshi) ? conn_rshi : conn_lshi;

        if (vocs_offset_changed || (vocs_offset[vocs_state->inst_idx] != new_offset)) {
            ble_update_vocs_offset(next_conn_idx, vocs_state->inst_idx, -(vocs_state->offset));
            vocs_offset_changed = false;
        }

        vocs_offset[vocs_state->inst_idx] = new_offset;
#endif
        lv_slider_set_value(vocs_slider[vocs_state->inst_idx],
                            vocs_offset[vocs_state->inst_idx],
                            LV_ANIM_OFF);
        break;
    case vcp_aics_state:
        vcp_aics_state_t *aics_state = (vcp_aics_state_t *)vcp_user_data;

        if (aics_state->conn_idx >= BLE_CONN_CNT) {
            printk("Connection index is not valid!\n");
            return;
        }

        if (aics_state->inst_idx >= aics_inst_cnt[aics_state->conn_idx]) {
            printk("AICS inst. index is not valid!\n");
            return;
        }

        if (aics_state->err != 0) {
            printk("AICS state get failed (%d) for inst. index %d\n",
                   aics_state->err, aics_state->inst_idx);
            return;
        }

        printk("Connection %d: AICS-%d gain = %d, mute = %u, mode = %u\n",
               aics_state->conn_idx, aics_state->inst_idx,
               aics_state->gain, aics_state->mute, aics_state->mode);

#if (BLE_CONN_CNT == 2)
        next_conn_idx = (aics_state->conn_idx != conn_rshi) ? conn_rshi : conn_lshi;

        if (aics_gain_changed || (aics_gain[aics_state->inst_idx] != aics_state->gain)) {
            ble_update_aics_gain(next_conn_idx, aics_state->inst_idx, aics_state->gain);
            aics_gain_changed = false;
        }

        if (aics_mute_changed || (aics_mute[aics_state->inst_idx] != aics_state->mute)) {
            ble_update_aics_mute(next_conn_idx, aics_state->inst_idx, aics_state->mute);
            aics_mute_changed = false;
        }
#endif
        aics_gain[aics_state->inst_idx] = aics_state->gain;
        aics_mute[aics_state->inst_idx] = aics_state->mute;

        lv_slider_set_value(aics_slider[aics_state->inst_idx],
                            aics_gain[aics_state->inst_idx],
                            LV_ANIM_OFF);
        lcd_change_voice_icon(aics_voice_icon[aics_state->inst_idx],
                              aics_mute[aics_state->inst_idx]);
        break;
    default:
        printk("VCP status: undefined parameter!\n");
        break;
    }
}

static int bt_init(void)
{
    int err;

    err = ble_bt_init();
    if (!err) {
        ble_scan_status_cb_register(&scan_device_status);
        ble_conn_status_cb_register(&device_connection_status);
        ble_vcp_status_cb_register(&vcp_status);
    }

    return err;
}


int main(void)
{
    int err;

    err = bt_init();
    if(err) {
        printk("BT init failed!\n");
        return 0;
    }
    printk("BT initialized.\n");

    scr = lv_scr_act();

    err = lcd_init();
    if (err) {
        printk("Device not ready!\n");
        return 0;
    }
    printk("Display initialized.\n");

    create_buttons(conn_disconnected);

    while (1) {
        lv_task_handler();
        k_sleep(K_MSEC(50));
    }
}
