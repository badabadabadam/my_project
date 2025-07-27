#ifndef _BT_MAIN_H_
#define _BT_MAIN_H_

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

struct gatt_client {
    char *name;
    struct bt_conn *conn;
    struct bt_le_conn_param conn_param;
    const struct bt_uuid_128 *uuid;
    void (*connected_cb)();
    void (*disconnected_cb)();
};

extern int dev_settings_load(void);
extern void config_svc_init(void);


#ifdef CONFIG_HAS_BLE_FSR
extern struct gatt_client fsr_srvc;
struct fsr_data {
    uint16_t value[4];
};
extern struct k_msgq fsr_msgq;
#endif // CONFIG_HAS_BLE_FSR
#ifdef CONFIG_HAS_BLE_CONTROLLER
extern struct gatt_client controller_client;
struct controller_data {
    uint16_t value;
    uint16_t mode;
};
extern struct k_msgq controller_msgq;
#endif // CONFIG_HAS_BLE_CONTROLLER

#endif // _BT_MAIN_H_

