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


static struct bt_vcp_vol_ctlr *vcp_vol_ctlr;
static struct bt_conn *default_conn;
static bool scan_started = false;
static bool connect_after_detection = false;
static bt_addr_le_t pd_addr;

static scan_status_callback_t *user_scan_status_cb = NULL;
static conn_status_callback_t *user_conn_status_cb = NULL;


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

    int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &default_conn);
    if (err) {
        printk("Connection failed (err %d)\n", err);
		bt_conn_unref(default_conn);
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
	if (default_conn == NULL) {
		printk("No device connected!\n");
        return 1;
	}

    int err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
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

int ble_vcp_discover(struct bt_vcp_vol_ctlr_cb *vcp_callbacks)
{
	static bool cb_registered;
	int result;

	if (!cb_registered) {
		result = bt_vcp_vol_ctlr_cb_register(vcp_callbacks);
		if (result != 0) {
			printk("CB register failed: %d\n", result);
			return -1;
		}

		cb_registered = true;
	}

	if (default_conn == NULL) {
		printk("Not connected!\n");
		return -2;
	}

	result = bt_vcp_vol_ctlr_discover(default_conn, &vcp_vol_ctlr);
	if (result != 0) {
		printk("Fail: %d\n", result);
        return -3;
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
