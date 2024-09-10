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


#define TARGET_DEVICE_NAME      CONFIG_BT_TARGET_DEVICE_NAME


static struct bt_conn *dev_conn;

static struct bt_vcp_vol_ctlr *vcp_vol_ctlr;
static struct bt_vcp_included vcp_included;

static bool scan_started = false;
static bool connect_after_detection = false;
static bt_addr_le_t pd_addr;

static scan_status_callback_t *user_scan_status_cb = NULL;
static conn_status_callback_t *user_conn_status_cb = NULL;
static vcp_status_callback_t *user_vcp_status_cb = NULL;


bool is_substring(const char *substr, const char *str)
{
    const size_t str_len = strlen(str);
    const size_t sub_str_len = strlen(substr);

    if (sub_str_len > str_len) {
        return false;
    }

    for (size_t pos = 0; pos < str_len; pos++) {
        if (pos + sub_str_len > str_len) {
            return false;
        }

        if (strncasecmp(substr, &str[pos], sub_str_len) == 0) {
            return true;
        }
    }

    return false;
}

static int connect_to_device(const bt_addr_le_t *addr)
{
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    printk("Connecting to %s...\n", addr_str);

    int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &dev_conn);
    if (err) {
        printk("Connection failed (err %d)\n", err);
        bt_conn_unref(dev_conn);
        return -1;
    }

    printk("Connection established.\n");
    return 0;
}

int ble_connect(void)
{
    if (scan_started) {
        bt_le_scan_stop();
        scan_started = false;
    }

    return connect_to_device(&pd_addr);
}

int ble_disconnect(void)
{
    if (dev_conn == NULL) {
        printk("No device connected!\n");
        return 1;
    }

    int err = bt_conn_disconnect(dev_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
        printk("Failed to disconnect (err %d)\n", err);
        return -1;
    }

    return 0;
}


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

static void scan_recv_cb(const struct bt_le_scan_recv_info *info, struct net_buf_simple *ad)
{
    char name[MAX_DEVICE_NAME_LEN];
    
    bt_data_parse(ad, scan_data_cb, name);
    
    if (is_substring(TARGET_DEVICE_NAME, name)) {
        char le_addr[BT_ADDR_LE_STR_LEN];

        if (info->addr == NULL) {
            return;
        }

        memcpy(&pd_addr, info->addr, sizeof(pd_addr));

        bt_addr_le_to_str(&pd_addr, le_addr, sizeof(le_addr));
        printk("Found device with name %s and address %s\n", name, le_addr);

        if (user_scan_status_cb) {
            user_scan_status_cb(status_available, name);
        }

        bt_le_scan_stop();
        scan_started = false;

        if(connect_after_detection) {
            connect_to_device(&pd_addr);
        }
    }
}

static void scan_timeout_cb(void)
{
    printk("Scan timeout!\n");

    scan_started = false;

    if (user_scan_status_cb) {
        user_scan_status_cb(status_unavailable ,NULL);
    }
}

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv_cb,
    .timeout = scan_timeout_cb,
};

int ble_start_scan(scan_type_t sct)
{
    const uint16_t scan_timeout_seconds = SCAN_TIMEOUT_SEC;
    const struct bt_le_scan_param param = {
        .type       = BT_LE_SCAN_TYPE_ACTIVE,
        .options    = BT_LE_SCAN_OPT_NONE,
        .interval   = BT_GAP_SCAN_FAST_INTERVAL,
        .window     = BT_GAP_SCAN_FAST_WINDOW,
        .timeout    = scan_timeout_seconds * 100,
    };

    connect_after_detection = sct;

    if (!scan_started) {
        int err = bt_le_scan_start(&param, NULL);
        if (err) {
            printk("Starting scanning failed (err %d)\n", err);
            return -1;
        }
        
        scan_started = true;
        printk("Scanning started.\n");
    } else {
        printk("Scanning is already started!\n");       
    }

    return 0;
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t vocs_count, uint8_t aics_count)
{
    if (user_vcp_status_cb) {
        vcp_discover_t discover;
        discover.err = err;
        discover.vocs_count = vocs_count;
        discover.aics_count = aics_count;

        user_vcp_status_cb(vcp_discover, &discover);
    }

    if (err != 0) {
        printk("VCP discover failed (%d)\n", err);
        return;
    }

    printk("VCP discover done with %u VOCS and %u AICS\n", vocs_count, aics_count);

    if (bt_vcp_vol_ctlr_included_get(vol_ctlr, &vcp_included)) {
        printk("Could not get VCP context!\n");
        return;
    }
}

static void vcp_volume_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t volume, uint8_t mute)
{
    if (user_vcp_status_cb) {
        vcp_volume_state_t state;
        state.err = err;
        state.volume = volume;
        state.mute = mute;

        user_vcp_status_cb(vcp_vcs_vol_state, &state);
    }
}

static void vcp_vocs_state_cb(struct bt_vocs *inst, int err, int16_t offset)
{
    for (uint8_t i=0; i < vcp_included.vocs_cnt; ++i) {
        if (vcp_included.vocs[i] == inst) {
            if (user_vcp_status_cb) {
                vcp_vocs_state_t state;
                state.inst_idx = i;
                state.err = err;
                state.offset = offset;

                user_vcp_status_cb(vcp_vocs_state, &state);
            }
        }
    }
}

static void vcp_aics_state_cb(struct bt_aics *inst, int err, int8_t gain, uint8_t mute, uint8_t mode)
{
    for (uint8_t i=0; i < vcp_included.aics_cnt; ++i) {
        if (vcp_included.aics[i] == inst) {
            if (user_vcp_status_cb) {
                vcp_aics_state_t state;
                state.inst_idx = i;
                state.err = err;
                state.gain = gain;
                state.mute = mute;
                state.mode = mode;

                user_vcp_status_cb(vcp_aics_state, &state);
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

int ble_vcp_discover(void)
{
    static bool cb_registered;
    int result;

    if (!cb_registered) {
        result = bt_vcp_vol_ctlr_cb_register(&vcp_cbs);
        if (result != 0) {
            printk("CB register failed: %d\n", result);
            return -1;
        }

        cb_registered = true;
    }

    if (dev_conn == NULL) {
        printk("Not connected!\n");
        return -2;
    }

    result = bt_vcp_vol_ctlr_discover(dev_conn, &vcp_vol_ctlr);
    if (result != 0) {
        printk("VCP discovering failed: %d\n", result);
        return -3;
    }

    return 0;
}

int ble_update_vocs_offset(uint8_t inst_idx, int16_t offset)
{
    int result;

    if(inst_idx > vcp_included.vocs_cnt) {
        printk("VOCS inst. index is not valid: %d\n", inst_idx);
        return -1;
    }

    printk("Set VOCS-%d offset = %d\n", inst_idx, offset);

    result = bt_vocs_state_set(vcp_included.vocs[inst_idx], offset);
    if (result != 0) {
        printk("VOCS offset set failed: %d\n", result);
        return -1;
    }

    return 0;
}

int ble_update_aics_gain(uint8_t inst_idx, int8_t gain)
{
    int result;

    if(inst_idx > vcp_included.aics_cnt) {
        printk("AICS inst. index is not valid: %d\n", inst_idx);
        return -1;
    }

    printk("Set AICS-%d gain = %d\n", inst_idx, gain);

    result = bt_aics_gain_set(vcp_included.aics[inst_idx], gain);
    if (result != 0) {
        printk("AICS gain set failed: %d\n", result);
        return -1;
    }

    return 0;
}

int ble_update_aics_mute(uint8_t inst_idx, uint8_t mute)
{
    int result;

    if(inst_idx > vcp_included.aics_cnt) {
        printk("AICS inst. index is not valid: %d\n", inst_idx);
        return -1;
    }

    printk("Set AICS-%d mute = %d\n", inst_idx, mute);

    if (mute) {
        result = bt_aics_unmute(vcp_included.aics[inst_idx]);

    } else {
        result = bt_aics_unmute(vcp_included.aics[inst_idx]);
    }

    if (result != 0) {
        printk("AICS mute set failed: %d\n", result);
        return -1;
    }

    return 0;
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    if (conn_err) {
        printk("Connection failed (err %u)\n", conn_err);
        bt_conn_unref(conn);
        return;
    }
    
    printk("Connected.\n");

    if (user_conn_status_cb) {
        user_conn_status_cb(status_available);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason %u)\n", reason);
    bt_conn_unref(conn);

    if (user_conn_status_cb) {
        user_conn_status_cb(status_unavailable);
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

int ble_bt_init(void)
{  
    int err = bt_enable(NULL);
    if (err) {
        printk("BT enable failed! (err %d)\n", err);
        return -1;
    }

    bt_le_scan_cb_register(&scan_callbacks);
    bt_conn_cb_register(&conn_callbacks);

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
