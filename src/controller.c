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
#include <stdio.h>
#include <string.h>
#include "errno.h"
#include "bt_main.h"

LOG_MODULE_REGISTER(controller, LOG_LEVEL_INF);

#define STACKSIZE 1024
#define PRIORITY 7

// #define VALUE_HANDLE 0x0017
// #define VALUE_CCC_HANDLE 0x0018

#define VALUE_HANDLE 0x0012
#define VALUE_CCC_HANDLE 0x0013


enum state_flag {
#ifndef VALUE_HANDLE    
	FLAG_DISCOVER,
#endif    
	FLAG_SUBSCRIBE,
	FLAG_NUM,
};
static ATOMIC_DEFINE(flag, FLAG_NUM);
K_SEM_DEFINE(cntl_sem, 0, 1);

#define CONNECTION_INTERVAL_MIN 16//16 //8
#define CONNECTION_INTERVAL_MAX 16//16 //8
#define CONNECTION_LATENCY      2
#define CONNECTION_SUPERVISION_TIMEOUT 80


// UUIDs for the controller service and notification
// a8a618ba-16bc-11f0-9cd2-0242ac120002
// a8a61aa4-16bc-11f0-9cd2-0242ac120002
// a8a61b62-16bc-11f0-9cd2-0242ac120002
// a8a61c16-16bc-11f0-9cd2-0242ac120002
// a8a61e46-16bc-11f0-9cd2-0242ac120002

static const struct bt_uuid_128 controller_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xa8a618ba, 0x16bc, 0x11f0, 0x9cd2, 0x0242ac120002));	


#ifndef VALUE_HANDLE
static const struct bt_uuid_128 controller_notification_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xa8a61aa4, 0x16bc, 0x11f0, 0x9cd2, 0x0242ac120002));	

static uint8_t discover_func(struct bt_conn *conn, 
    const struct bt_gatt_attr *attr, 
    struct bt_gatt_discover_params *params);

static struct bt_gatt_discover_params discover_params = {
    .uuid = &controller_notification_uuid.uuid,
    .func = discover_func,
    .start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
    .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
    .type = BT_GATT_DISCOVER_CHARACTERISTIC,
};
#endif

static uint8_t notify_func(struct bt_conn *conn,
    struct bt_gatt_subscribe_params *params,
    const void *data, uint16_t length);

static struct bt_gatt_subscribe_params subscribe_params = {
#ifdef VALUE_HANDLE
    .disc_params = NULL,
    .ccc_handle = VALUE_CCC_HANDLE,
#else
    .disc_params = &discover_params,
    .ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE,
#endif     
    .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
    .value = BT_GATT_CCC_INDICATE,
#ifdef VALUE_HANDLE 
    .value_handle = VALUE_HANDLE,
#else
    .value_handle = 0,
#endif
    .notify = notify_func,
};



static uint8_t notify_func(struct bt_conn *conn,
    struct bt_gatt_subscribe_params *params,
    const void *data, uint16_t length)
{
    if (!data) {
        LOG_INF("[UNSUBSCRIBED]: 0x%04x", params->value_handle);
        atomic_set_bit(flag, FLAG_SUBSCRIBE);
        k_sem_give(&cntl_sem);
        return BT_GATT_ITER_STOP;
    }
    if (length == sizeof(struct controller_data)) {
        struct controller_data cont;
        cont.value = sys_get_be16((uint8_t*) data);
        cont.mode = sys_get_be16((uint8_t*) data + sizeof(uint16_t));
        if (k_msgq_put(&controller_msgq, &cont, K_NO_WAIT) != 0) {
            LOG_ERR("controller msgq full");
        }
        if (cont.value) {
            printf("[INDICATION]: Go mode[%d]\n", cont.mode);
        } else {
            printf("[INDICATION]: Stop\n");
        }

        // LOG_DBG("[NOTIFICATION] 0x%04x", sys_get_be16(data));
    } else {
        LOG_DBG("[NOTIFICATION] data %p length %u", data, length);
    }
    return BT_GATT_ITER_CONTINUE;
}

#ifndef VALUE_HANDLE
static uint8_t discover_func(struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    struct bt_gatt_discover_params *params)
{
    int err;

    LOG_INF("Discovering...");

    if (!attr) {
        LOG_INF("Discover complete");
        return BT_GATT_ITER_STOP;
    }

    if (bt_uuid_cmp(discover_params.uuid, &controller_notification_uuid.uuid) == 0) {
        LOG_INF("Service found");
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);
        atomic_set_bit(flag, FLAG_SUBSCRIBE);
        k_sem_give(&cntl_sem);
    } 

    return BT_GATT_ITER_STOP;
}
#endif

static void connected () 
{
    LOG_INF("Connected");
#ifdef VALUE_HANDLE   
    atomic_set_bit(flag, FLAG_SUBSCRIBE);
    k_sem_give(&cntl_sem);
#else
    // if (subscribe_params.value_handle) {
    //     atomic_set_bit(flag, FLAG_SUBSCRIBE);
    //     k_sem_give(&cntl_sem);
    // } else {

        atomic_set_bit(flag, FLAG_DISCOVER);
        k_sem_give(&cntl_sem);
    // }
#endif
    bool val = true;
    settings_runtime_set("event/controller_connection", &val, sizeof(val));
}

static void disconnected () 
{
    LOG_INF("Disconnected");
    bool val = false;
    settings_runtime_set("event/controller_connection", &val, sizeof(val));
}

struct gatt_client controller_client = {
    .name = "Controller",
    .conn = NULL,
    .conn_param = {
        .interval_min = CONNECTION_INTERVAL_MIN,
        .interval_max = CONNECTION_INTERVAL_MAX,
        .latency = CONNECTION_LATENCY,
        .timeout = CONNECTION_SUPERVISION_TIMEOUT,
    },
    .uuid = &controller_svc_uuid,
    .connected_cb = connected,
    .disconnected_cb = disconnected,
};


static void controller_thread(void)
{
    int err;

	while (1) {
        k_sem_take(&cntl_sem, K_FOREVER);
        if (atomic_test_and_clear_bit(flag, FLAG_SUBSCRIBE)) {
            k_sleep(K_MSEC(500));
            if (controller_client.conn == NULL) {
                LOG_ERR("No connection");
                continue;
            }
            err = bt_gatt_subscribe(controller_client.conn, &subscribe_params);
            if (err && err != -EALREADY) {
                LOG_INF("Subscribe failed (err %d)", err);
            } else {
                LOG_INF("[SUBSCRIBED]: handle 0x%04x ccc_handle 0x%04x", 
                    subscribe_params.value_handle, subscribe_params.ccc_handle);
            }
		}
#ifndef VALUE_HANDLE   
        else if (atomic_test_and_clear_bit(flag, FLAG_DISCOVER)) {
            k_sleep(K_MSEC(500));
            discover_params.uuid = &controller_notification_uuid.uuid,
            discover_params.func = discover_func,
            discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
            discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
            discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC,
            err = bt_gatt_discover(controller_client.conn, &discover_params);
            if (err) {
                LOG_ERR("Discover failed (err %d)", err);
            }
        } 
#endif


	}
}

K_THREAD_DEFINE(controller_id, STACKSIZE, controller_thread, NULL, NULL, NULL, PRIORITY, 0, 0);

