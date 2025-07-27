#include <zephyr/device.h>
#include <zephyr/devicetree.h>
 
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <zephyr/random/random.h>
#include <string.h>
#include "bt_main.h"

LOG_MODULE_REGISTER(fsr, LOG_LEVEL_DBG);

#define CONNECTION_INTERVAL_MIN 8
#define CONNECTION_INTERVAL_MAX 8
#define CONNECTION_LATENCY 0
#define CONNECTION_SUPERVISION_TIMEOUT 48

static const struct bt_uuid_128 fsr_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe2505f48, 0x01a0, 0x11f0, 0x9cd2, 0x0242ac120002));	
static const struct bt_uuid_128 fsr_notification_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe2506240, 0x01a0, 0x11f0, 0x9cd2, 0x0242ac120002));

extern struct gatt_client fsr_srvc;

static uint8_t discover_func(struct bt_conn *conn, 
    const struct bt_gatt_attr *attr, 
    struct bt_gatt_discover_params *params);
static uint8_t notify_func(struct bt_conn *conn,
    struct bt_gatt_subscribe_params *params,
    const void *data, uint16_t length);

static struct bt_gatt_discover_params discover_params = {
    .uuid = &fsr_notification_uuid.uuid,
    .func = discover_func,
    .start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
    .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
    .type = BT_GATT_DISCOVER_CHARACTERISTIC,
};

static struct bt_gatt_subscribe_params subscribe_params = {
    .disc_params = &discover_params,
    .ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE,
    .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
    .value = BT_GATT_CCC_NOTIFY,
    .value_handle = 0U,
    .notify = notify_func,
};

static uint8_t notify_func(struct bt_conn *conn,
    struct bt_gatt_subscribe_params *params,
    const void *data, uint16_t length)
{
    if (!data) {
        LOG_INF("[UNSUBSCRIBED]: 0x%04x", params->value_handle);
        return BT_GATT_ITER_STOP;
    }
    if (length == 8) {
        struct fsr_data fsr;
        fsr.value[0] = sys_get_be16(data);
        fsr.value[1] = sys_get_be16((uint8_t*) data + 2);
        fsr.value[2] = sys_get_be16((uint8_t*) data + 4);
        fsr.value[3] = sys_get_be16((uint8_t*) data + 6); // Not used, but can be set to 0 or any other value
        if (k_msgq_put(&fsr_msgq, &fsr, K_NO_WAIT) != 0) {
            LOG_ERR("FSR msgq full");
        }
        if (fsr.value[0] > 50 || fsr.value[1] > 50 || fsr.value[2] > 50 || fsr.value[3] > 50) {
            LOG_DBG("[FSR Pressed]: %u %u %u %u", fsr.value[0], fsr.value[1], fsr.value[2], fsr.value[3]);
        }

        // LOG_DBG("[NOTIFICATION] %u %u %u %u", sys_get_be16(data), sys_get_be16((uint8_t*) data + 2), 
        //                             sys_get_be16((uint8_t*) data + 4), sys_get_be16((uint8_t*) data + 6));
    } else {
        LOG_DBG("[NOTIFICATION] data %p length %u", data, length);
    }
    return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    struct bt_gatt_discover_params *params)
{
    int err;

	LOG_INF("Discovering...");

    if (!attr) {
        LOG_INF("Discover complete");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

	if (bt_uuid_cmp(discover_params.uuid, &fsr_notification_uuid.uuid) == 0) {
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);
        err = bt_gatt_subscribe(fsr_srvc.conn, &subscribe_params);
		if (err && err != -EALREADY) {
            LOG_INF("Subscribe failed (err %d)", err);
		} else {
			LOG_INF("[SUBSCRIBED]: 0x%04x", subscribe_params.value_handle);
        }
	} 

    return BT_GATT_ITER_STOP;
}


static void connected () 
{
    int err;

    LOG_INF("Connected");
    bool val = true;
    settings_runtime_set("event/fsr_connection", &val, sizeof(val));
	if (subscribe_params.value_handle) {
		err = bt_gatt_subscribe(fsr_srvc.conn, &subscribe_params);
		if (err && err != -EALREADY) {
			LOG_ERR("Subscribe failed (err %d)", err);
		} else {
			LOG_INF("[SUBSCRIBED]: 0x%04x", subscribe_params.value_handle);
		}
	} else {
		err = bt_gatt_discover(fsr_srvc.conn, &discover_params);
		if (err) {
			LOG_ERR("Discover failed (err %d)", err);
		}
	}
}

static void disconnected () 
{
    LOG_INF("Disconnected");
    bool val = false;
    settings_runtime_set("event/fsr_connection", &val, sizeof(val));
    // fsr_conn = NULL;
}

struct gatt_client fsr_srvc = {
    .name = "FSR",
    .conn = NULL,
    .conn_param = {
        .interval_min = CONNECTION_INTERVAL_MIN,
        .interval_max = CONNECTION_INTERVAL_MAX,
        .latency = CONNECTION_LATENCY,
        .timeout = CONNECTION_SUPERVISION_TIMEOUT,
    },
    .uuid = &fsr_svc_uuid,
    .connected_cb = connected,
    .disconnected_cb = disconnected,
};

