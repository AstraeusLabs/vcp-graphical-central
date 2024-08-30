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


typedef void (scan_status_callback_t) (status_t dev_st, const char *dev_name);
typedef void (conn_status_callback_t) (status_t conn_st);


bool is_substring(const char *substr, const char *str);

int ble_bt_init(void);
int ble_connect(void);
int ble_disconnect(void);
int ble_start_scan(scan_type_t sct);
int ble_vcp_discover(struct bt_vcp_vol_ctlr_cb *vcp_callbacks);
void ble_scan_status_cb_register(scan_status_callback_t *scan_status_cb);
void ble_conn_status_cb_register(conn_status_callback_t *conn_status_cb);

#endif /* __BLE_H */
