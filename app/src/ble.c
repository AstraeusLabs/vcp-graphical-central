/*
 * Copyright (c) 2024 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/bluetooth/audio/aics.h>
#include <zephyr/bluetooth/audio/vocs.h>

#include "ble.h"


#define TGT_DEV_NAME        CONFIG_BT_TARGET_DEVICE_NAME
#define RSHI_DEV_NAME       CONFIG_BT_TARGET_RSHI_DEVICE_NAME
#define LSHI_DEV_NAME       CONFIG_BT_TARGET_LSHI_DEVICE_NAME

#if (BLE_CONN_CNT == 2)
#define INIT_DEV_NAME { \
    RSHI_DEV_NAME, \
    LSHI_DEV_NAME \
}
#else
#define INIT_DEV_NAME { \
    TGT_DEV_NAME \
}
#endif


static struct bt_conn *ble_conn[BLE_CONN_CNT];
static struct bt_vcp_vol_ctlr *vcp_vol_ctlr[BLE_CONN_CNT];
static struct bt_vcp_included vcp_included[BLE_CONN_CNT];

static bool ble_dev_found[BLE_CONN_CNT] = { false };
static bt_addr_le_t pd_addr[BLE_CONN_CNT];
const char *dev_name[BLE_CONN_CNT] = INIT_DEV_NAME;
static bool scan_started = false;

static scan_status_callback_t *user_scan_status_cb = NULL;
static conn_status_callback_t *user_conn_status_cb = NULL;
static vcp_status_callback_t *user_vcp_status_cb = NULL;


static bool scan_data_cb(struct bt_data *data, void *user_data)
{
    char *name = user_data;
    uint8_t len;

    switch (data->type) {
    case BT_DATA_NAME_SHORTENED:
    case BT_DATA_NAME_COMPLETE:
        len = MIN(data->data_len, MAX_DEVICE_NAME_LEN - 1);
        memcpy(name, data->data, len);
        name[len] = '\0';
        return false;
    default:
        return true;
    }
}

static void scan_recv_cb(const struct bt_le_scan_recv_info *info,
                         struct net_buf_simple *ad)
{
    char name[MAX_DEVICE_NAME_LEN];

    bt_data_parse(ad, scan_data_cb, name);

    for (int i = 0; i < BLE_CONN_CNT; i++) {
        if (!ble_dev_found[i] && !strcmp(dev_name[i], name)) {
            char le_addr[BT_ADDR_LE_STR_LEN];

            if (info->addr == NULL) {
                return;
            }

            ble_dev_found[i] = true;
            memcpy(&pd_addr[i], info->addr, sizeof(pd_addr[i]));

            bt_addr_le_to_str(&pd_addr[i], le_addr, sizeof(le_addr));
            printk("Found device with name %s and address %s\n",
                   name, le_addr);

            if (user_scan_status_cb) {
                user_scan_status_cb(scan_available, name);
            }
        }
    }

    for (int i = 0; i < BLE_CONN_CNT; i++) {
        if(!ble_dev_found[i]) {
            return;
        }
    }

    int err = bt_le_scan_stop();
    if (err) {
        printk("Failed to stop scan: %d\n", err);
    }

    scan_started = false;
    printk("Scan stopped.\n");

    if (user_scan_status_cb) {
        user_scan_status_cb(scan_done, NULL);
    }

}

static void scan_timeout_cb(void)
{
    printk("Scan timeout!\n");

    scan_started = false;

    if (user_scan_status_cb) {
        user_scan_status_cb(scan_timeout ,NULL);
    }
}

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv_cb,
    .timeout = scan_timeout_cb,
};

int ble_start_scan(void)
{
    const uint16_t scan_timeout_seconds = SCAN_TIMEOUT_SEC;
    const struct bt_le_scan_param param = {
        .type       = BT_LE_SCAN_TYPE_ACTIVE,
        .options    = BT_LE_SCAN_OPT_NONE,
        .interval   = BT_GAP_SCAN_FAST_INTERVAL,
        .window     = BT_GAP_SCAN_FAST_WINDOW,
        .timeout    = scan_timeout_seconds * 100,
    };

    if (!scan_started) {
        for (int i = 0; i < BLE_CONN_CNT; i++) {
            ble_dev_found[i] = false;
        }

        int err = bt_le_scan_start(&param, NULL);
        if (err) {
            printk("Starting scanning failed (err %d)\n", err);
            return -1;
        }

        scan_started = true;
        printk("Scanning started.\n");
    } else {
        printk("Scanning is already started!\n");
        return 1;
    }

    return 0;
}

int ble_start_scan_force(void)
{
    if (scan_started) {
        int err = bt_le_scan_stop();
        if (err) {
            printk("Failed to stop scan: %d\n", err);
            return -1;
        }

        scan_started = false;
    }

    return ble_start_scan();
}

static int connect_to_device(uint8_t conn_idx)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(&pd_addr[conn_idx], addr_str, sizeof(addr_str));
	printk("Connecting to connection %d (name: %s, addr: %s)...\n",
           conn_idx, dev_name[conn_idx], addr_str);

    int err = bt_conn_le_create(&pd_addr[conn_idx],
                                BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT,
                                &ble_conn[conn_idx]);
    if (err) {
        printk("Connection failed (err %d)\n", err);
        return -1;
    }

    return 0;
}

int ble_connect(uint8_t conn_idx)
{
    if (scan_started) {
        int err = bt_le_scan_stop();
        if (err) {
            printk("Failed to stop scan: %d\n", err);
            return -1;
        }

        scan_started = false;
    }

    return connect_to_device(conn_idx);
}

int ble_disconnect(uint8_t conn_idx)
{
	if (ble_conn[conn_idx] == NULL) {
		printk("Connection %d: no connection available!\n", conn_idx);
        return 1;
	}

    int err = bt_conn_disconnect(ble_conn[conn_idx],
                                 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
        printk("Connection %d: failed to disconnect (err %d)\n",
               conn_idx, err);
        return -1;
    }

    return 0;
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err,
                            uint8_t vocs_count, uint8_t aics_count)
{
    int conn_idx = -1;
    int disc_err = 0;

    for (int i = 0; i < BLE_CONN_CNT; i++) {
        if (vol_ctlr == vcp_vol_ctlr[i]) {
            conn_idx = i;
        }
    }

    if (conn_idx == -1) {
        return;
    }

    if (err) {
        disc_err = -1;
        printk("Connection %d; VCP discover failed (%d)\n",
               conn_idx, err);
    } else {
        int res = bt_vcp_vol_ctlr_included_get(vol_ctlr,
                                               &vcp_included[conn_idx]);
        if (res) {
            disc_err = -2;
            printk("Connecion %d: could not get VCP context!\n",
                   conn_idx);
        }
    }

    if (user_vcp_status_cb) {
        vcp_discover_t discover;
        discover.conn_idx = conn_idx;
        discover.err = disc_err;
        discover.vocs_count = vcp_included[conn_idx].vocs_cnt;
        discover.aics_count = vcp_included[conn_idx].aics_cnt;

        user_vcp_status_cb(vcp_discover, &discover);
    }
}

static void vcp_volume_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr,
                                int err, uint8_t volume, uint8_t mute)
{
    int conn_idx = -1;

    for (int i = 0; i < BLE_CONN_CNT; i++) {
        if (vol_ctlr == vcp_vol_ctlr[i]) {
            conn_idx = i;
        }
    }

    if (conn_idx == -1) {
        return;
    }

    if (user_vcp_status_cb) {
        vcp_vol_state_t state;
        state.conn_idx = conn_idx;
        state.err = err;
        state.volume = volume;
        state.mute = mute;

        user_vcp_status_cb(vcp_vcs_vol_state, &state);
    }
}

static void vcp_vocs_state_cb(struct bt_vocs *inst, int err,
                              int16_t offset)
{
    for (int i = 0; i < BLE_CONN_CNT; i++) {
        for (int j = 0; j < vcp_included[i].vocs_cnt; ++j) {
            if (vcp_included[i].vocs[j] == inst) {
                if (user_vcp_status_cb) {
                    vcp_vocs_state_t state;
                    state.conn_idx = i;
                    state.inst_idx = j;
                    state.err = err;
                    state.offset = offset;

                    user_vcp_status_cb(vcp_vocs_state, &state);
                }
            }
        }
    }
}

static void vcp_aics_state_cb(struct bt_aics *inst, int err, int8_t gain,
                              uint8_t mute, uint8_t mode)
{
    for (int i = 0; i < BLE_CONN_CNT; i++) {
        for (uint8_t j = 0; j < vcp_included[i].aics_cnt; ++j) {
            if (vcp_included[i].aics[j] == inst) {
                if (user_vcp_status_cb) {
                    vcp_aics_state_t state;
                    state.conn_idx = i;
                    state.inst_idx = j;
                    state.err = err;
                    state.gain = gain;
                    state.mute = mute;
                    state.mode = mode;

                    user_vcp_status_cb(vcp_aics_state, &state);
                }
            }
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

int ble_vcp_discover(uint8_t conn_idx)
{
    if (ble_conn[conn_idx] == NULL) {
        printk("Connection %d: not connected!\n", conn_idx);
        return -2;
    }

    int err = bt_vcp_vol_ctlr_discover(ble_conn[conn_idx],
                                       &vcp_vol_ctlr[conn_idx]);
    if (err != 0) {
        printk("Connection %d: VCP discovering failed: %d\n",
               conn_idx, err);
        return -3;
    }

	return 0;
}

int ble_update_vocs_offset(uint8_t conn_idx, uint8_t inst_idx,
                           int16_t offset)
{
    int result;

    if(inst_idx > vcp_included[conn_idx].vocs_cnt) {
        printk("Connection %d: VOCS inst. index is not valid: %d\n",
               conn_idx, inst_idx);
        return -1;
    }

    result = bt_vocs_state_set(vcp_included[conn_idx].vocs[inst_idx],
                               offset);
    if (result != 0) {
        printk("Connection %d: VOCS offset set failed: %d\n",
               conn_idx, result);
        return -1;
    }

    return 0;
}

int ble_update_aics_gain(uint8_t conn_idx, uint8_t inst_idx,
                         int8_t gain)
{
    int result;

    if(inst_idx > vcp_included[conn_idx].aics_cnt) {
        printk("Connection %d: AICS inst. index is not valid: %d\n",
               conn_idx, inst_idx);
        return -1;
    }

    result = bt_aics_gain_set(vcp_included[conn_idx].aics[inst_idx],
                              gain);
    if (result != 0) {
        printk("Connection %d: AICS gain set failed: %d\n",
               conn_idx, result);
        return -1;
    }

    return 0;
}

int ble_update_aics_mute(uint8_t conn_idx, uint8_t inst_idx,
                         uint8_t mute)
{
    int result;

    if(inst_idx > vcp_included[conn_idx].aics_cnt) {
        printk("Connection %d: AICS inst. index is not valid: %d\n",
               conn_idx, inst_idx);
        return -1;
    }

    if (mute) {
        result = bt_aics_mute(vcp_included[conn_idx].aics[inst_idx]);

    } else {
        result = bt_aics_unmute(vcp_included[conn_idx].aics[inst_idx]);
    }

    if (result != 0) {
        printk("Connection %d: AICS mute set failed: %d\n",
               conn_idx, result);
        return -1;
    }

    return 0;
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    int conn_idx = -1;

    for (int i = 0; i < BLE_CONN_CNT; i++) {
        if (conn == ble_conn[i]) {
            conn_idx = i;
        }
    }

    if (conn_idx == -1) {
        return;
    }

    if (conn_err) {
        printk("Connection failed (conn=%d, err=%u)\n",
               conn_idx, conn_err);
        bt_conn_unref(conn);
        return;
    }

    printk("Connection %d: connected.\n", conn_idx);

    if (user_conn_status_cb) {
        user_conn_status_cb(conn_idx, conn_connected);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    int conn_idx = -1;

    for (int i = 0; i < BLE_CONN_CNT; i++) {
        if (conn == ble_conn[i]) {
            conn_idx = i;
        }
    }

    if (conn_idx == -1) {
        return;
    }

    printk("Connection %d: disconnected (reason %u)\n",
           conn_idx,reason);
    bt_conn_unref(ble_conn[conn_idx]);

    if (user_conn_status_cb) {
        user_conn_status_cb(conn_idx, conn_disconnected);
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

int ble_bt_init(void)
{
    int err;

    err = bt_enable(NULL);
    if (err) {
        printk("BT enable failed! (err %d)\n", err);
        return -1;
    }

    bt_le_scan_cb_register(&scan_callbacks);
    bt_conn_cb_register(&conn_callbacks);

    err = bt_vcp_vol_ctlr_cb_register(&vcp_cbs);
    if (err) {
        printk("CB register failed: %d\n", err);
        return -2;
    }

    return 0;
}

void ble_scan_status_cb_register(scan_status_callback_t *scan_status_cb)
{
    user_scan_status_cb = scan_status_cb;
}

void ble_conn_status_cb_register(conn_status_callback_t *conn_status_cb)
{
    user_conn_status_cb = conn_status_cb;
}

void ble_vcp_status_cb_register(vcp_status_callback_t *vcp_status_cb)
{
    user_vcp_status_cb = vcp_status_cb;
}
