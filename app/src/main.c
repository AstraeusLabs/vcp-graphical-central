/*
 * Copyright (c) 2024 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include "lcd.h"
#include "ble.h"


static bool target_device_detected = false;
static uint8_t vocs_count = 1, aics_count = 3;
static uint8_t aics_mute[VCP_MAX_AICS_INST] = { 0 };
static int8_t aics_gain[VCP_MAX_AICS_INST] = { 0 };
static int16_t vocs_offset[VCP_MAX_VOCS_INST] = { 0 };

static lv_obj_t *scr;
static lv_obj_t *vocs_slider[VCP_MAX_VOCS_INST];
static lv_obj_t *aics_slider[VCP_MAX_AICS_INST];
static lv_obj_t *vocs_voice_icon[VCP_MAX_VOCS_INST];
static lv_obj_t *aics_voice_icon[VCP_MAX_AICS_INST];
static lv_obj_t *msg_label;


static void vocs_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);
    
    for (uint8_t i = 0; i < vocs_count; i++) {
        if(slider == vocs_slider[i]) {
            vocs_offset[i] = value;
	        ble_update_vocs_offset(i, value);
        }
    }    
}

static void aics_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);

    for (uint8_t i = 0; i < aics_count; i++) {
        if(slider == aics_slider[i]) {
            aics_gain[i] = value;
	        ble_update_aics_gain(i, value);
        }
    }    
}

static void aics_voice_icon_event_cb(lv_event_t *e)
{
    lv_obj_t *icon = lv_event_get_target(e);

    for (uint8_t i = 0; i < aics_count; i++) {
        if(icon == aics_voice_icon[i]) {
            aics_mute[i] = !aics_mute[i];
            ble_update_aics_mute(i, aics_mute[i]);
            lcd_change_voice_icon(icon, aics_mute[i]);
        }
    }
}

static void create_sliders(void)
{
    lv_obj_t *vocs_label[VCP_MAX_VOCS_INST];
    lv_obj_t *aics_label[VCP_MAX_AICS_INST];
    lv_coord_t scr_y = LCD_Y_MIN;
    const int dist = (LCD_Y_MAX - LCD_Y_MIN) / (VCP_MAX_VOCS_INST + VCP_MAX_AICS_INST + 1); 

    lcd_clear_screen(scr);

    for (uint8_t i = 0; i < vocs_count; ++i) {
        char txt[10];
        snprintf(txt, sizeof(txt), "VOCS-%d", i);

        scr_y += dist;

        vocs_slider[i] = lcd_create_slider(scr, VOCS_OFFSET_MIN, VOCS_OFFSET_MAX, 10, scr_y, vocs_slider_event_cb);
        vocs_label[i] = lcd_create_label(scr, txt, -120, scr_y);
        vocs_voice_icon[i] = lcd_create_balance_icon(scr, 125, scr_y, NULL);
    }

    for (uint8_t i = 0; i < aics_count; ++i) {
        char txt[10];
        snprintf(txt, sizeof(txt), "AICS-%d", i);

        scr_y += dist;

        aics_slider[i] = lcd_create_slider(scr, AICS_GAIN_MIN, AICS_GAIN_MAX, 10, scr_y, aics_slider_event_cb);
        aics_label[i] = lcd_create_label(scr, txt, -120, scr_y);
        aics_voice_icon[i] = lcd_create_voice_icon(scr, 125, scr_y, aics_voice_icon_event_cb);
    }
}

static void scan_btn_event_cb(lv_event_t *e)
{
    target_device_detected = false;

    int err = ble_start_scan(scan_only);
    if (err) {
        lcd_display_message(msg_label, "Start scanning failed!");
        return;
    }        

    lcd_display_message(msg_label, "Scanning started.");
}

static void connect_btn_event_cb(lv_event_t *e)
{
    int err;

    lcd_display_message(msg_label, "Connecting...");

    if (target_device_detected) {
        err = ble_connect();
        if (err) {
            lcd_display_message(msg_label, "Connection failed!");
        }
    } else {
        err = ble_start_scan(scan_connect);
        if (err) {
            lcd_display_message(msg_label, "Start scanning failed!");
        }
    }
}

static void discover_btn_event_cb(lv_event_t *e)
{
     int err = ble_vcp_discover();
     if (err) {
        lcd_display_message(msg_label, "VCP discover failed!");
        return;
     }

    lcd_display_message(msg_label, "Start discovering VCP...");
}

static void disconnect_btn_event_cb(lv_event_t *e)
{  
    int err = ble_disconnect();
    if (err) {
        lcd_display_message(msg_label, "Failed to disconnect!");
	}
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
    disconnect_btn = lcd_create_button(scr, "Disconnect", 120, 40, -75, -75, disconnect_btn_event_cb);

    msg_label = lcd_create_label(scr, "Connected.", 0, 70);
}

static void scan_device_status(status_t dev_st, const char *dev_name)
{
    if (dev_st == status_available) {
        char txt[MAX_DEVICE_NAME_LEN + 20];    
        snprintf(txt, sizeof(txt), "Found device: %s\n", dev_name);
        lcd_display_message(msg_label, txt);

        target_device_detected = true;
    } else {
        printk("No device found!\n");
        lcd_display_message(msg_label, "No device found!");
    }
}

static void device_connection_status(status_t conn_st)
{
    if (conn_st == status_available) {
        create_buttons_after_connecting();
    } else {
        create_buttons_before_connecting();
    }
}

static void vcp_status(vcp_type_t cb_type, void *vcp_user_data)
{
    switch (cb_type) {
    case vcp_discover:
        vcp_discover_t *discover_data = (vcp_discover_t *)vcp_user_data;

        vocs_count = discover_data->vocs_count;
        aics_count = discover_data->aics_count;

        printk("VCP discovered successfully (no. of VOCS inst. = %d, no. of AICS inst. = %d\n", vocs_count, aics_count);
        create_sliders();
        break;
    case vcp_vcs_vol_state:
        vcp_volume_state_t *vcs_state = (vcp_volume_state_t *)vcp_user_data;

        if (vcs_state->err != 0) {
            printk("VOCS state get failed (%d)\n", vcs_state->err);
            return;
        }

        printk("VCS volume = %u, mute = %u\n", vcs_state->volume, vcs_state->mute);
        break;
    case vcp_vocs_state:
        vcp_vocs_state_t *vocs_state = (vcp_vocs_state_t *)vcp_user_data;

        if (vocs_state->inst_idx >= vocs_count) {
            printk("VOCS inst. index (%d) is not valid!\n", vocs_state->inst_idx);
            return;
        }

        if (vocs_state->err != 0) {
            printk("VOCS state get failed (%d) for inst. index %d\n", vocs_state->err, vocs_state->inst_idx);
            return;
        }

        printk("VOCS-%d offset = %d\n", vocs_state->inst_idx, vocs_state->offset);
        lv_slider_set_value(vocs_slider[vocs_state->inst_idx], vocs_state->offset, LV_ANIM_OFF);
        break;
    case vcp_aics_state:
        vcp_aics_state_t *aics_state = (vcp_aics_state_t *)vcp_user_data;

        if (aics_state->inst_idx >= aics_count) {
            printk("AICS inst. index (%d) is not valid!\n", aics_state->inst_idx);
            return;
        }

        if (aics_state->err != 0) {
            printk("AICS state get failed (%d) for inst. index %d\n", aics_state->err, aics_state->inst_idx);
            return;
        }

        printk("AICS-%d gain = %d, mute = %u, mode = %u\n", aics_state->inst_idx, aics_state->gain, aics_state->mute, aics_state->mode);
        lv_slider_set_value(aics_slider[aics_state->inst_idx], aics_state->gain, LV_ANIM_OFF);
        lcd_change_voice_icon(aics_voice_icon[aics_state->inst_idx], aics_state->mute);
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

    create_buttons_before_connecting();

	while (1) {
        lv_task_handler();
        k_sleep(K_MSEC(50));
	}
}
