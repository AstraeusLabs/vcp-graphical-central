/*
 * Copyright (c) 2024 Demant A/S
 * SPDX-License-Identifier: Apache-2.0
 */

/* Header for BLE management */

#ifndef __BLE_H
#define __BLE_H

#define MAX_DEVICE_NAME_LEN     32

#define BLE_CONN_CNT            CONFIG_BT_TARGET_DEVICE_NUMBER

#define VCP_MAX_VOCS_INST       CONFIG_BT_VCP_VOL_CTLR_MAX_VOCS_INST
#define VCP_MAX_AICS_INST       CONFIG_BT_VCP_VOL_CTLR_MAX_AICS_INST

#define VOLUME_MAX              255
#define VOLUME_MIN              0
#define VOCS_OFFSET_MAX         255
#define VOCS_OFFSET_MIN         -255
#define AICS_GAIN_MAX           127
#define AICS_GAIN_MIN           -128


typedef enum
{
    scan_timeout = -1,
    scan_unavailable,
    scan_available,
    scan_done,
} scan_status_t;

typedef enum
{
    conn_disconnected = 0,
    conn_connected,
} conn_status_t;

typedef enum
{
    vcp_discover,
    vcp_vcs_vol_state,
    vcp_vocs_state,
    vcp_aics_state,
} vcp_type_t;

typedef struct
{
    uint8_t conn_idx;
    int err;
    uint8_t vocs_count;
    uint8_t aics_count;
} vcp_discover_t;

typedef struct
{
    uint8_t conn_idx;
    int err;
    uint8_t volume;
    uint8_t mute;
} vcp_vol_state_t;

typedef struct
{
    uint8_t conn_idx;
    uint8_t inst_idx;
    int err;
    int16_t offset;
} vcp_vocs_state_t;

typedef struct
{
    uint8_t conn_idx;
    uint8_t inst_idx;
    int err;
    int8_t gain;
    uint8_t mute;
    uint8_t mode;
} vcp_aics_state_t;

enum
{
    conn_unknown = -1,
    conn_tgt = 0,
    conn_rshi = 0,
    conn_lshi = 1,
    conn_undefined,
};


typedef void (scan_status_callback_t) (scan_status_t scan_st, const char *dev_name);
typedef void (conn_status_callback_t) (uint8_t conn_idx, conn_status_t conn_st);
typedef void (vcp_status_callback_t) (vcp_type_t cb_type, void *vcp_user_data);


int ble_bt_init(void);
int ble_stop_scan(void);
int ble_start_scan(void);
int ble_start_scan_force(void);
int ble_connect(uint8_t conn_idx);
int ble_disconnect(uint8_t conn_idx);
int ble_vcp_discover(uint8_t conn_idx);
int ble_update_volume(uint8_t conn_idx, uint8_t volume);
int ble_update_volume_mute(uint8_t conn_idx, uint8_t mute);
int ble_update_vocs_offset(uint8_t conn_idx, uint8_t inst_idx, int16_t offset);
int ble_update_aics_gain(uint8_t conn_idx, uint8_t inst_idx, int8_t gain);
int ble_update_aics_mute(uint8_t conn_idx, uint8_t inst_idx, uint8_t mute);

void ble_scan_status_cb_register(scan_status_callback_t *scan_status_cb);
void ble_conn_status_cb_register(conn_status_callback_t *conn_status_cb);
void ble_vcp_status_cb_register(vcp_status_callback_t *vcp_status_cb);

#endif /* __BLE_H */
