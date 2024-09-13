/*
 * Copyright (c) 2024 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __LCD_H
#define __LCD_H

#define LCD_X_MAX   160
#define LCD_X_MIN   -160
#define LCD_Y_MAX   120
#define LCD_Y_MIN   -120


int lcd_init(void);

lv_obj_t *lcd_create_slider(lv_obj_t *parent,
                            int16_t min_value,int16_t max_value,
                            lv_coord_t x, lv_coord_t y,
                            lv_event_cb_t cb);

lv_obj_t *lcd_create_button(lv_obj_t *parent, const char *text,
                            int32_t w, int32_t h,
                            lv_coord_t x, lv_coord_t y,
                            lv_event_cb_t cb);

lv_obj_t *lcd_create_label(lv_obj_t *parent, const char *text,
                            lv_coord_t x, lv_coord_t y);

lv_obj_t *lcd_create_voice_icon(lv_obj_t *parent,
                                lv_coord_t x, lv_coord_t y,
                                lv_event_cb_t cb);

lv_obj_t *lcd_create_balance_icon(lv_obj_t *parent,
                                  lv_coord_t x, lv_coord_t y,
                                  lv_event_cb_t cb);

void lcd_clear_screen(lv_obj_t *parent);
void lcd_display_message(lv_obj_t *lbl, const char *msg);
void lcd_change_voice_icon(lv_obj_t *icon, uint8_t mute);

#endif /* __LCD_H */
