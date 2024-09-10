/*
 * Copyright (c) 2024 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BLE_H
#define __BLE_H

#define SCAN_TIMEOUT_SEC        10
#define MAX_DEVICE_NAME_LEN     32

#define VCP_MAX_VOCS_INST       CONFIG_BT_VCP_VOL_CTLR_MAX_VOCS_INST
#define VCP_MAX_AICS_INST       CONFIG_BT_VCP_VOL_CTLR_MAX_AICS_INST

#define VOCS_OFFSET_MAX         255
#define VOCS_OFFSET_MIN         -255
#define AICS_GAIN_MAX           127
#define AICS_GAIN_MIN           -128


typedef enum
{
    scan_only = 0,
    scan_connect,
} scan_type_t;

typedef enum
{
    status_unavailable = 0,
    status_available,
} status_t;

typedef enum
{
    vcp_discover,
    vcp_vcs_vol_state,
    vcp_vocs_state,
    vcp_aics_state,
} vcp_type_t;

typedef struct
{
    int err;
    uint8_t vocs_count;
    uint8_t aics_count;
} vcp_discover_t;

typedef struct
{
    int err;
    uint8_t volume;
    uint8_t mute;
} vcp_volume_state_t;

typedef struct
{
    uint8_t inst_idx;
    int err;
    int16_t offset;
} vcp_vocs_state_t;

typedef struct
{
    uint8_t inst_idx;
    int err;
    int8_t gain;
    uint8_t mute;
    uint8_t mode;
} vcp_aics_state_t;


typedef void (scan_status_callback_t) (status_t dev_st, const char *dev_name);
typedef void (conn_status_callback_t) (status_t conn_st);
typedef void (vcp_status_callback_t) (vcp_type_t cb_type, void *vcp_user_data);


bool is_substring(const char *substr, const char *str);

int ble_bt_init(void);
int ble_connect(void);
int ble_disconnect(void);
int ble_start_scan(scan_type_t sct);
int ble_vcp_discover(void);
int ble_update_vocs_offset(uint8_t inst_idx, int16_t offset);
int ble_update_aics_gain(uint8_t inst_idx, int8_t gain);
int ble_update_aics_mute(uint8_t inst_idx, uint8_t mute);
void ble_scan_status_cb_register(scan_status_callback_t *scan_status_cb);
void ble_conn_status_cb_register(conn_status_callback_t *conn_status_cb);
void ble_vcp_status_cb_register(vcp_status_callback_t *vcp_status_cb);

#endif /* __BLE_H */
