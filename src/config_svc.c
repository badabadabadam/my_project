#include <zephyr/device.h>
#include <zephyr/devicetree.h>
 
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
#include <string.h>
#include "config_svc.h"


LOG_MODULE_REGISTER(config_svc, LOG_LEVEL_INF);

#define STACKSIZE 1024
#define PRIORITY 7

enum controller_flag {
	FLAG_ADVERTISE,
	FLAG_NUM,
};
static ATOMIC_DEFINE(flag, FLAG_NUM);

K_SEM_DEFINE(svc_sem, 0, 1);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
			BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
			BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CONFIG_SERVICE_VAL),			
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_uuid_128 config_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_CONFIG_SERVICE_VAL);
static const struct bt_uuid_128 config_flat_walking_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x32e94da4, 0x19c8, 0x11f0, 0x9cd2, 0x0242ac120002));	
static const struct bt_uuid_128 config_stair_ascent_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x32e94ef8, 0x19c8, 0x11f0, 0x9cd2, 0x0242ac120002));	
static const struct bt_uuid_128 config_stair_descent_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x32e94f8e, 0x19c8, 0x11f0, 0x9cd2, 0x0242ac120002));	
static const struct bt_uuid_128 config_manual_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x32e950ec, 0x19c8, 0x11f0, 0x9cd2, 0x0242ac120002));	
static const struct bt_uuid_128 config_fsr_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x32e95178, 0x19c8, 0x11f0, 0x9cd2, 0x0242ac120002));	



static struct bt_conn *svc_conn = NULL;

static void connected(struct bt_conn *conn, uint8_t err)
{
    struct bt_conn_info info;

    err = bt_conn_get_info(conn, &info);
    if (err) {
        LOG_ERR("Failed to get connection info: %d", err);
        return;
    }
    if (info.role == BT_CONN_ROLE_PERIPHERAL) {
        char addr[BT_ADDR_LE_STR_LEN];
    	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        LOG_INF("Connected as peripheral to %s", addr);
        svc_conn = conn;
		// atomic_set_bit(flag, FLAG_CONNECTED);
		// k_sem_give(&svc_sem);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

    if (conn != svc_conn) {
        return;
    }
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));
    svc_conn = NULL;
    atomic_set_bit(flag, FLAG_ADVERTISE);
    k_sem_give(&svc_sem);
}


BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};


void init_config_svc(void)
{
    atomic_set_bit(flag, FLAG_ADVERTISE);
    k_sem_give(&svc_sem);
}

static void config_svc_thread(void)
{
    int err;

    
	while (1) {
        k_sem_take(&svc_sem, K_FOREVER);
        if (atomic_test_and_clear_bit(flag, FLAG_ADVERTISE)) {
			err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
			if (err) {
				LOG_ERR("Advertising failed to start (err %d)", err);
				return;
			}
			LOG_INF("Advertising started");
		}

	}
}

K_THREAD_DEFINE(config_svc_id, STACKSIZE, config_svc_thread, NULL, NULL, NULL, PRIORITY, 0, 0);


struct direct_immediate_value {
	size_t len;
	void *dest;
	size_t fetched;
};

static int direct_loader(const char *name, size_t len, settings_read_cb read_cb,
	void *cb_arg, void *param)
{
	struct direct_immediate_value *one_value = (struct direct_immediate_value *)param;
    ARG_UNUSED(len);
	const char *next;
	size_t name_len;
	int rc;

	name_len = settings_name_next(name, &next);
	if (name_len == 0) {
        if (len <= one_value->len) {
            rc = read_cb(cb_arg, one_value->dest, len);
            if (rc >= 0) {
                one_value->fetched = len;
                return 0;
            }
            return rc;
        }
        return -EINVAL;
    }
	return 0;
}

static ssize_t read_uint16(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    uint16_t value;
    struct direct_immediate_value div = {
        .len = sizeof(value),
        .dest = (void *)&value,
        .fetched = 0,
    };
    settings_load_subtree_direct(attr->user_data, direct_loader, &div);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

static ssize_t write_uint16(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    const void *buf, uint16_t len, uint16_t offset,
    uint8_t flags)
{
    uint16_t value;

   if (offset + len > sizeof(value)) {
       return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
   }

//    memcpy(value + offset, buf, len);
   settings_save_one(attr->user_data, buf, len);
   return len;
}


BT_GATT_SERVICE_DEFINE(config_service,
	BT_GATT_PRIMARY_SERVICE(&config_svc_uuid),
	BT_GATT_CHARACTERISTIC(&config_flat_walking_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                read_uint16, write_uint16, "config/flat_walking"),
	BT_GATT_CHARACTERISTIC(&config_stair_ascent_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                read_uint16, write_uint16, "config/stair_ascent"),
    BT_GATT_CHARACTERISTIC(&config_stair_descent_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                read_uint16, write_uint16, "config/stair_descent"),                
    BT_GATT_CHARACTERISTIC(&config_manual_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                read_uint16, write_uint16, "config/manual"),                
    BT_GATT_CHARACTERISTIC(&config_fsr_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                read_uint16, write_uint16, "config/fsr"),                
);
