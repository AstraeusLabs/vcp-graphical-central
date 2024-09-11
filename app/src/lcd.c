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
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include "lcd.h"


static const lv_style_prop_t slider_props[] = {LV_STYLE_BG_COLOR, 0};
static lv_style_transition_dsc_t slider_trans_dsc;

static lv_style_t slider_style_main;
static lv_style_t slider_style_indicator;
static lv_style_t slider_style_knob;
static lv_style_t slider_style_pressed_color;

static lv_style_prop_t button_props[] = {LV_STYLE_OUTLINE_WIDTH, LV_STYLE_OUTLINE_OPA, 0};
static lv_style_transition_dsc_t button_trans_dsc;

static lv_style_t button_style;
static lv_style_t button_style_pressed;

static lv_style_t voice_icon_style;
static lv_style_t mute_icon_style;

static bool msg_label_created = false;


static void lcd_slider_style_init(void)
{
    lv_style_transition_dsc_init(&slider_trans_dsc, slider_props, lv_anim_path_linear, 300, 0, NULL);

    lv_style_init(&slider_style_main);
    lv_style_set_bg_opa(&slider_style_main, LV_OPA_COVER);
    lv_style_set_bg_color(&slider_style_main, lv_palette_main(LV_PALETTE_LIGHT_GREEN));
    lv_style_set_radius(&slider_style_main, LV_RADIUS_CIRCLE);
    lv_style_set_pad_ver(&slider_style_main, -2);

    lv_style_init(&slider_style_indicator);
    lv_style_set_bg_opa(&slider_style_indicator, LV_OPA_COVER);
    lv_style_set_bg_color(&slider_style_indicator, lv_palette_darken(LV_PALETTE_GREEN, 3));
    lv_style_set_radius(&slider_style_indicator, LV_RADIUS_CIRCLE);
    lv_style_set_transition(&slider_style_indicator, &slider_trans_dsc);

    lv_style_init(&slider_style_knob);
    lv_style_set_bg_opa(&slider_style_knob, LV_OPA_COVER);
    lv_style_set_bg_color(&slider_style_knob, lv_palette_darken(LV_PALETTE_RED, 3));
    lv_style_set_border_color(&slider_style_knob, lv_palette_darken(LV_PALETTE_RED, 5));
    lv_style_set_border_width(&slider_style_knob, 1);
    lv_style_set_radius(&slider_style_knob, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&slider_style_knob, 4);
    lv_style_set_transition(&slider_style_knob, &slider_trans_dsc);

    lv_style_init(&slider_style_pressed_color);
    lv_style_set_bg_color(&slider_style_pressed_color, lv_palette_lighten(LV_PALETTE_GREEN, 3));
}

static void lcd_button_style_init(void)
{
    lv_style_init(&button_style);
    lv_style_set_radius(&button_style, 3);

    lv_style_set_bg_opa(&button_style, LV_OPA_100);
    lv_style_set_bg_color(&button_style, lv_palette_main(LV_PALETTE_DEEP_PURPLE));
    lv_style_set_bg_grad_color(&button_style, lv_palette_darken(LV_PALETTE_DEEP_PURPLE, 1));
    lv_style_set_bg_grad_dir(&button_style, LV_GRAD_DIR_VER);

    lv_style_set_border_opa(&button_style, LV_OPA_100);
    lv_style_set_border_width(&button_style, 2);
    lv_style_set_border_color(&button_style, lv_palette_darken(LV_PALETTE_DEEP_PURPLE, 2));

    lv_style_set_outline_opa(&button_style, LV_OPA_COVER);
    lv_style_set_outline_color(&button_style, lv_palette_darken(LV_PALETTE_DEEP_PURPLE, 3));

    lv_style_set_text_color(&button_style, lv_palette_main(LV_PALETTE_AMBER));
    lv_style_set_pad_all(&button_style, 10);

    lv_style_init(&button_style_pressed);

    lv_style_set_outline_width(&button_style_pressed, 10);
    lv_style_set_outline_opa(&button_style_pressed, LV_OPA_TRANSP);

    lv_style_set_bg_color(&button_style_pressed, lv_palette_darken(LV_PALETTE_DEEP_PURPLE, 2));
    lv_style_set_bg_grad_color(&button_style_pressed, lv_palette_darken(LV_PALETTE_DEEP_PURPLE, 4));

    lv_style_transition_dsc_init(&button_trans_dsc, button_props, lv_anim_path_linear, 100, 0, NULL);
    lv_style_set_transition(&button_style_pressed, &button_trans_dsc);
}

static void lcd_common_voice_style_init(lv_style_t style)
{
    lv_style_init(&style);

    lv_style_set_bg_opa(&style, LV_OPA_TRANSP);
    lv_style_set_bg_color(&style, lv_color_white());
    lv_style_set_bg_grad_color(&style, lv_color_white());
    lv_style_set_bg_grad_dir(&style, LV_GRAD_DIR_NONE);

    lv_style_set_border_opa(&style, LV_OPA_TRANSP);
    lv_style_set_border_width(&style, 0);
    lv_style_set_border_color(&style, lv_color_white());

    lv_style_set_outline_opa(&style, LV_OPA_TRANSP);
    lv_style_set_outline_color(&style, lv_color_white());

    lv_style_set_pad_all(&style, 0);
}

static void lcd_voice_icon_style_init(void)
{
    lcd_common_voice_style_init(voice_icon_style);
    lv_style_set_text_color(&voice_icon_style, lv_palette_darken(LV_PALETTE_GREEN, 4));
}

static void lcd_mute_icon_style_init(void)
{
    lcd_common_voice_style_init(mute_icon_style);
    lv_style_set_text_color(&mute_icon_style, lv_palette_main(LV_PALETTE_RED));
}

int lcd_init(void)
{
    lv_init();

	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		return -1;
	}

    display_blanking_off(display_dev);
    lcd_slider_style_init();
    lcd_button_style_init();
    lcd_voice_icon_style_init();
    lcd_mute_icon_style_init();

    return 0;
}

void lcd_clear_screen(lv_obj_t *parent)
{
    msg_label_created = false;
    k_sleep(K_MSEC(300));
    lv_obj_clean(parent);
    k_sleep(K_MSEC(300));
}

lv_obj_t *lcd_create_slider(lv_obj_t *parent, int16_t min_value, int16_t max_value, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_remove_style_all(slider);

    lv_obj_add_style(slider, &slider_style_main, LV_PART_MAIN);
    lv_obj_add_style(slider, &slider_style_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(slider, &slider_style_pressed_color, LV_PART_INDICATOR | LV_STATE_PRESSED);
    lv_obj_add_style(slider, &slider_style_knob, LV_PART_KNOB);
    lv_obj_add_style(slider, &slider_style_pressed_color, LV_PART_KNOB | LV_STATE_PRESSED);

    lv_obj_center(slider);

    lv_obj_set_width(slider, 170);
    lv_obj_set_height(slider, 15);
    lv_obj_align(slider, LV_ALIGN_CENTER, x, y);

    lv_obj_add_event_cb(slider, cb, LV_EVENT_RELEASED, NULL);

    lv_slider_set_range(slider, min_value, max_value);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF); 

    return slider;
}

lv_obj_t *lcd_create_button(lv_obj_t *parent, const char *text,  int32_t w, int32_t h, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &button_style, LV_STATE_DEFAULT);
    lv_obj_add_style(button, &button_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(button, w, h);
    lv_obj_align(button, LV_ALIGN_CENTER, x, y);

    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return button;
}

lv_obj_t *lcd_create_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *msg_label = lv_label_create(parent);
    lv_label_set_text(msg_label, text);
    lv_obj_align(msg_label, LV_ALIGN_CENTER, x, y);

    msg_label_created = true;

    return msg_label;
}

lv_obj_t *lcd_create_voice_icon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb)
{
    lv_obj_t *icon = lv_btn_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_add_style(icon, &voice_icon_style, LV_PART_MAIN);

    lv_obj_set_size(icon, 30, 30);
    lv_obj_align(icon, LV_ALIGN_CENTER, x, y);

    lv_obj_add_event_cb(icon, cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *label = lv_label_create(icon);
    lv_label_set_text(label, LV_SYMBOL_VOLUME_MAX);
    lv_obj_center(label);

    return icon;
}

lv_obj_t *lcd_create_balance_icon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb)
{
    lv_obj_t *icon = lv_btn_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_add_style(icon, &voice_icon_style, LV_PART_MAIN);

    lv_obj_set_size(icon, 30, 30);
    lv_obj_align(icon, LV_ALIGN_CENTER, x, y);

    lv_obj_add_event_cb(icon, cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *label = lv_label_create(icon);
    lv_label_set_text(label, "L/R");
    lv_obj_center(label);

    return icon;
}

void lcd_change_voice_icon(lv_obj_t *icon, uint8_t mute)
{
    lv_obj_t *label = lv_obj_get_child(icon, 0);

    if (mute) {
        lv_obj_add_style(icon, &mute_icon_style, LV_PART_MAIN);
        lv_label_set_text(label, LV_SYMBOL_VOLUME_MID);
    } else {
        lv_obj_add_style(icon, &voice_icon_style, LV_PART_MAIN);
        lv_label_set_text(label, LV_SYMBOL_VOLUME_MAX);
    }
}

void lcd_display_message(lv_obj_t *lbl, const char *msg)
{
    if(msg_label_created) {
        lv_label_set_text(lbl, msg);
    }
}
