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
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/bluetooth/audio/aics.h>
#include <zephyr/bluetooth/audio/vocs.h>

#include "lcd.h"
#include "ble.h"


static struct bt_vcp_included vcp_included;
static bool target_device_detected = false;

static int16_t vocs_offset[VCP_MAX_VOCS_INST] = {0};
static int8_t aics_gain[VCP_MAX_AICS_INST] = {0};
static uint8_t aics_mute[VCP_MAX_AICS_INST] = {0};

static lv_obj_t *scr;
static lv_obj_t *vocs_slider[VCP_MAX_VOCS_INST];
static lv_obj_t *aics_slider[VCP_MAX_AICS_INST];
static lv_obj_t *vocs_voice_icon[VCP_MAX_VOCS_INST];
static lv_obj_t *aics_voice_icon[VCP_MAX_AICS_INST];
static lv_obj_t *msg_label;
static bool msg_label_created = false;


static void display_message(const char *msg)
{
    if(msg_label_created) {
        lv_label_set_text(msg_label, msg);
    }
}

static void clear_screen(void)
{
    msg_label_created = false;
    k_sleep(K_MSEC(250));
    lv_obj_clean(scr);
    k_sleep(K_MSEC(250));
}

static void vocs_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);
    
    for (uint8_t i = 0; i < vcp_included.vocs_cnt; i++) {
        if(slider == vocs_slider[i]) {
            vocs_offset[i] = value;
            printk("Set VOCS-%d offset = %d\n", i, value);

	        int result = bt_vocs_state_set(vcp_included.vocs[i], value);
	        if (result != 0) {
		        printk("VOCS offset set failed: %d\n", result);
	        } 
        }
    }    
}

static void aics_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);

    for (uint8_t i = 0; i < vcp_included.aics_cnt; i++) {
        if(slider == aics_slider[i]) {
            aics_gain[i] = value;
            printk("Set AICS-%d gain = %d\n", i, value);

	        int result = bt_aics_gain_set(vcp_included.aics[i], value);
	        if (result != 0) {
		        printk("AICS gain set failed: %d\n", result);
	        } 
        }
    }    
}

static void aics_voice_icon_event_cb(lv_event_t *e)
{
    lv_obj_t *icon = lv_event_get_target(e);

    for (uint8_t i = 0; i < vcp_included.aics_cnt; i++) {
        if(icon == aics_voice_icon[i]) {
            int result;

            if(aics_mute[i]) {
                aics_mute[i] = 0;
	            result = bt_aics_unmute(vcp_included.aics[i]);
                lcd_change_voice_icon(icon, aics_mute[i]);
            } else {
                aics_mute[i] = 1;
	            result = bt_aics_mute(vcp_included.aics[i]);
                lcd_change_voice_icon(icon, aics_mute[i]);
            }

            printk("Set AICS-%d mute = %d\n", i,  aics_mute[i]);
	        if (result != 0) {
		        printk("AICS mute set failed: %d\n", result);
	        }
        }
    }
}

static void create_sliders(void)
{
    lv_obj_t *vocs_label[VCP_MAX_VOCS_INST];
    lv_obj_t *aics_label[VCP_MAX_AICS_INST];
    lv_coord_t scr_y = LCD_Y_MIN;
    const int dist = (LCD_Y_MAX - LCD_Y_MIN) / (VCP_MAX_VOCS_INST + VCP_MAX_AICS_INST + 1); 

    clear_screen();

    for (uint8_t i = 0; i < vcp_included.vocs_cnt; ++i) {
        char txt[10];
        snprintf(txt, sizeof(txt), "VOCS-%d", i);

        scr_y += dist;

        vocs_slider[i] = lcd_create_slider(scr, VOCS_OFFSET_MIN, VOCS_OFFSET_MAX, 10, scr_y, vocs_slider_event_cb);
        vocs_label[i] = lcd_create_label(scr, txt, -120, scr_y);
        vocs_voice_icon[i] = lcd_create_balance_icon(scr, 125, scr_y, NULL);
    }

    for (uint8_t i = 0; i < vcp_included.aics_cnt; ++i) {
        char txt[10];
        snprintf(txt, sizeof(txt), "AICS-%d", i);

        scr_y += dist;

        aics_slider[i] = lcd_create_slider(scr, AICS_GAIN_MIN, AICS_GAIN_MAX, 10, scr_y, aics_slider_event_cb);
        aics_label[i] = lcd_create_label(scr, txt, -120, scr_y);
        aics_voice_icon[i] = lcd_create_voice_icon(scr, 125, scr_y, aics_voice_icon_event_cb);
    }
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t vocs_count, uint8_t aics_count)
{
	if (err != 0) {
		printk("VCP discover failed (%d)\n", err);
        display_message("VCP discover failed!");
        return;
	}

	printk("VCP discover done with %u VOCS and %u AICS\n", vocs_count, aics_count);

	if (bt_vcp_vol_ctlr_included_get(vol_ctlr, &vcp_included)) {
		printk("Could not get VCP context!\n");
        display_message("Could not get VCP context!");
        return;
	}

    create_sliders();
}

static void vcp_volume_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t volume, uint8_t mute)
{
	if (err != 0) {
		printk("VCS state get failed (%d)\n", err);
        return;
	}

    printk("VCS volume = %u, mute = %u\n", volume, mute);
}

static void vcp_vocs_state_cb(struct bt_vocs *inst, int err, int16_t offset)
{
	if (err != 0) {
		printk("VOCS state get failed (%d) for inst %p\n", err, inst);
        return;
	}

    for (uint8_t i=0; i < vcp_included.vocs_cnt; ++i) {
        if (vcp_included.vocs[i] == inst) {
            vocs_offset[i] = offset;
            lv_slider_set_value(vocs_slider[i], offset, LV_ANIM_OFF);
            printk("VOCS-%d offset = %d\n", i, offset);
        }
    }
}

static void vcp_aics_state_cb(struct bt_aics *inst, int err, int8_t gain, uint8_t mute, uint8_t mode)
{
	if (err != 0) {
		printk("AICS state get failed (%d) for inst %p\n", err, inst);
        return;
	}

    for (uint8_t i=0; i < vcp_included.aics_cnt; ++i) {
        if (vcp_included.aics[i] == inst) {
            aics_gain[i] = gain;
            aics_mute[i] = mute;
            lv_slider_set_value(aics_slider[i], gain, LV_ANIM_OFF);
            lcd_change_voice_icon(aics_voice_icon[i], mute);
            printk("AICS-%d gain = %d, mute = %u, mode = %u\n", i, gain, mute, mode);
        }
    }
}

static struct bt_vcp_vol_ctlr_cb vcp_cbs = {
	.discover = vcp_discover_cb,
    .state = vcp_volume_state_cb,
	.vocs_cb = {
		.state = vcp_vocs_state_cb,
	},
	.aics_cb = {
		.state = vcp_aics_state_cb,
	}
};

static int vcp_discover(void)
{
	return ble_vcp_discover(&vcp_cbs);
}

static void scan_btn_event_cb(lv_event_t *e)
{
    target_device_detected = false;

    int err = ble_start_scan(scan_only);
    if (err) {
        display_message("Start scanning failed!");
        return;
    }        

    display_message("Scanning started.");       
}

static void connect_btn_event_cb(lv_event_t *e)
{
    int err;

    display_message("Connecting...");

    if (target_device_detected) {
        err = ble_connect();
        if (err) {
            display_message("Connection failed!");
        }
    } else {
        err = ble_start_scan(scan_connect);
        if (err) {
            display_message("Start scanning failed!");
        }
    }
}

static void discover_btn_event_cb(lv_event_t *e)
{
     int err = vcp_discover();
     if (err) {
        display_message("VCP discover failed!");
        return;
     }

    display_message("Start discovering VCP...");
}

static void disconnect_btn_event_cb(lv_event_t *e)
{  
    int err = ble_disconnect();
    if (err) {
        display_message("Failed to disconnect!");
	}
}

static void create_buttons_before_connecting(void)
{
    lv_obj_t *connect_btn, *scan_btn;

    clear_screen();

    connect_btn = lcd_create_button(scr, "Connect", 100, 50, -60, -20, connect_btn_event_cb);
    scan_btn = lcd_create_button(scr, "Scan", 100, 50, 60, -20, scan_btn_event_cb);

    msg_label = lcd_create_label(scr, "Not connected.", 0, 50);
    msg_label_created = true;
}

static void create_buttons_after_connecting(void)
{
    lv_obj_t *discover_btn, *disconnect_btn;

    clear_screen();

    discover_btn = lcd_create_button(scr, "VCP Discover", 160, 50, 0, 0, discover_btn_event_cb);
    disconnect_btn = lcd_create_button(scr, "Disconnect", 120, 40, -75, -75, disconnect_btn_event_cb);

    msg_label = lcd_create_label(scr, "Connected.", 0, 70);
    msg_label_created = true;
}

static void scan_device_status(status_t dev_st, const char *dev_name)
{
    if (dev_st == status_available) {
        char txt[MAX_DEVICE_NAME_LEN + 20];    
        snprintf(txt, sizeof(txt), "Found device: %s\n", dev_name);
        display_message(txt);

        target_device_detected = true;
    } else {
        printk("No device found!\n");
        display_message("No device found!");
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

static int bt_init(void)
{  
    int err;
    
    err = ble_bt_init();
    if (!err) {
        ble_scan_status_cb_register(&scan_device_status);
        ble_conn_status_cb_register(&device_connection_status);
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
