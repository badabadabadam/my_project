
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
#include "config_svc.h"

LOG_MODULE_REGISTER(bt_main, LOG_LEVEL_INF);

#define STACKSIZE 1024
#define PRIORITY 7

#define MFG_DATA_LEN 2
#define MFG_FLAG_PAIRING 0x8000

#define SCAN_PARAM BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_PASSIVE, \
									BT_LE_SCAN_OPT_NONE, \
									BT_GAP_SCAN_FAST_INTERVAL, \
									BT_GAP_SCAN_FAST_WINDOW)
#define SCAN_DELAY K_MSEC(500)


enum state_flag {
	FLAG_SCAN,
	FLAG_SLEEP_BEFORE_SCAN,
	FLAG_PAIR,
	FLAG_PAIRING_COMPLETE,
	FLAG_NUM,
};

static ATOMIC_DEFINE(flag, FLAG_NUM);
K_SEM_DEFINE(bt_sem, 0, 1);

static struct gatt_client *clients[] = {
#ifdef CONFIG_HAS_BLE_FSR
	&fsr_srvc,	
#endif
#ifdef CONFIG_HAS_BLE_CONTROLLER
	&controller_client,
#endif	
	// NULL
};

static bool is_pairing = false;
static bt_addr_le_t bond_addrs[CONFIG_BT_MAX_PAIRED] = {0};
struct k_work_delayable pairing_timeout_work;
#define PAIRING_TIMEOUT K_SECONDS(CONFIG_PAIRING_TIMEOUT)

static void copy_bonded_addr(const struct bt_bond_info *info, void *data)
{
	int *count = (int *)data;
	if (*count >= CONFIG_BT_MAX_PAIRED) {
		LOG_WRN("Maximum bonded addresses reached (%d)", CONFIG_BT_MAX_PAIRED);
		return;
	}
	bt_addr_le_copy(&bond_addrs[*count], &info->addr);
	(*count)++;
}

static void load_bonded_addresses(void)
{
	int bonded_count = 0;
	bt_foreach_bond(BT_ID_DEFAULT, copy_bonded_addr, &bonded_count);
	LOG_INF("Loaded %u bonded addresses", bonded_count);
}

static bool find_bonded_addr(const bt_addr_le_t *addr)
{
	// Check if the address is in the bonded addresses list
	for (int i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
		if (bt_addr_le_cmp(addr, &bond_addrs[i]) == 0) {
			return true;
		}
	}
	return false;
}

static void pairing_timeout(struct k_work *work)
{
	LOG_INF("Pairing timeout, stopping scan");
	atomic_set_bit(flag, FLAG_PAIRING_COMPLETE);
	k_sem_give(&bt_sem);
}


static void start_scan(void);
static void stop_scan(void);

static bool scan_required(void)
{
	// Check if any client is disconnected
	for (int i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i]->conn == NULL) {
			return true;
		}
	}
	return false;
}

static struct gatt_client * get_client_by_conn(struct bt_conn *conn)
{
	// Find the service by connection
	for (int i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i]->conn == conn) {
			return clients[i];
		}
	}
	return NULL;
}

static void disconnect_all(void)
{
	LOG_INF("Disconnecting all clients");
	for (int i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i]->conn) {
			bt_conn_disconnect(clients[i]->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			clients[i]->disconnected_cb();
			bt_conn_unref(clients[i]->conn);
			clients[i]->conn = NULL;
		}
	}
}

static bool eir_found(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;
    struct bt_uuid_128 uuid128;
	static uint16_t mfg_data = 0x0; // Manufacturer data placeholder

	// LOG_INF("[AD]: %u data_len %u", data->type, data->data_len);

	switch (data->type) {
	case BT_DATA_MANUFACTURER_DATA:
		if (data->data_len == 2) {
			mfg_data = sys_get_be16(data->data);
			// LOG_INF("Manufacturer data found: %04x", mfg_data);
		} else {
			mfg_data = 0x0; // Reset if data length is not 2
		}
		break;	
	case BT_DATA_UUID128_ALL:
        if (!bt_uuid_create(&uuid128.uuid, data->data, data->data_len)) {
            LOG_ERR("Unable to create UUID from data");
            return false;
        }

		for (int i = 0; i < ARRAY_SIZE(clients); i++) {
			if (bt_uuid_cmp(&uuid128.uuid, &clients[i]->uuid->uuid) == 0 && clients[i]->conn == NULL) {

				if (is_pairing) {
					if (mfg_data & MFG_FLAG_PAIRING) {
						LOG_INF("Pairing flag found in manufacturer data, pairing mode active");
						bt_unpair(BT_ID_DEFAULT, addr);
					} else {
						LOG_INF("Pairing mode active, but no pairing flag in manufacturer data");
						return false;
					}
				} else  {
					if (find_bonded_addr(addr) && !(mfg_data & MFG_FLAG_PAIRING)) {
						LOG_INF("Found bonded address, proceeding with connection");
					} else {
						LOG_INF("Not in pairing mode and no bonded address found");
						return false;
					}
				}

				LOG_INF("Found service %s", clients[i]->name);
				stop_scan();
				if (bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, &clients[i]->conn_param, &clients[i]->conn)) {
					char addr_str[BT_ADDR_LE_STR_LEN];
					bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
					LOG_INF("Create conn to %s failed", addr_str);
					atomic_set_bit(flag, FLAG_SCAN);
					k_sem_give(&bt_sem);
				}
				return false;
			}
		}
    }

	return true;
}


static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	if (!scan_required()) {
		stop_scan();
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_DBG("Device found: %s (RSSI %d)", addr_str, rssi);
	bt_data_parse(ad, eir_found, (void *)addr);
}


static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct gatt_client *client = get_client_by_conn(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_INF("Failed to connect to %s %u %s", addr, err, bt_hci_err_to_str(err));
		if (client) {
			client->conn = NULL;
		}
		bt_conn_unref(conn);
		atomic_set_bit(flag, FLAG_SCAN);
		k_sem_give(&bt_sem);
		return;
	}

	if (client == NULL) {
		LOG_WRN("Service not found for connection");
		return;
	}

	client->connected_cb();
	LOG_INF("Connected from %s security %d", addr, bt_conn_get_security(conn));

	atomic_set_bit(flag, FLAG_SLEEP_BEFORE_SCAN);
	k_sem_give(&bt_sem);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct gatt_client *client = get_client_by_conn(conn);
	if (!client) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));
	client->disconnected_cb();
	bt_conn_unref(client->conn);
	client->conn = NULL;

	atomic_set_bit(flag, FLAG_SCAN);
	k_sem_give(&bt_sem);
}

static bool le_param_req(struct bt_conn *conn,struct bt_le_conn_param *param)
{
	LOG_INF("LE param request: interval_min %d interval_max %d latency %d timeout %d", param->interval_min, 
		param->interval_max, param->latency, param->timeout);
	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
	LOG_INF("LE param updated: interval %d latency %d timeout %d", interval, latency, timeout);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Security changed: %s, level %d, err %d", addr, level, err);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_req = le_param_req,
    .le_param_updated = le_param_updated,
	.security_changed = security_changed,
};

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated,
};

void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));
	LOG_INF("Pairing completed: %s, bonded %s", addr_str, bonded ? "yes" : "no");
}

void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_INF("Pairing failed: %s", bt_security_err_to_str(reason));
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
	bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
}


static struct bt_conn_auth_info_cb bt_conn_auth_info = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void start_scan(void)
{
	int err;

	err = bt_le_scan_start(SCAN_PARAM, device_found);
	if (err && err != -EALREADY) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	} else {
		LOG_INF("Scanning successfully started\n");
	}
}

static void stop_scan(void)
{
	int err;

	err = bt_le_scan_stop();
	if (err) {
		LOG_ERR("Failed to stop scanning (err %d)", err);
		return;
	}
	LOG_INF("Scanning successfully stopped\n");
}


static void bt_thread(void)
{

    k_work_init_delayable(&pairing_timeout_work, pairing_timeout);

	if (dev_settings_load()) {
		LOG_ERR("Failed to initialize settings");
		return;
	}

	k_sem_take(&bt_sem, K_FOREVER); // Wait for the semaphore to start Bluetooth initialization

	if (bt_enable(NULL)) {
		LOG_ERR("Failed to enable Bluetooth");
		return;
	}
 
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	LOG_INF("Bluetooth initialized");

	load_bonded_addresses();

    bt_conn_auth_info_cb_register(&bt_conn_auth_info);
	bt_gatt_cb_register(&gatt_callbacks);

	bt_set_bondable(false);

	start_scan();
	init_config_svc();

	while (1) {
        k_sem_take(&bt_sem, K_FOREVER);
		if (atomic_test_and_clear_bit(flag, FLAG_SCAN)) {
			if (scan_required()) {
				start_scan();
			} else if (is_pairing) {
				k_work_cancel_delayable(&pairing_timeout_work);
				atomic_set_bit(flag, FLAG_PAIRING_COMPLETE);
				k_sem_give(&bt_sem);
			}
		} 
		if (atomic_test_and_clear_bit(flag, FLAG_SLEEP_BEFORE_SCAN)) {
			k_sleep(SCAN_DELAY);
			atomic_set_bit(flag, FLAG_SCAN);
			k_sem_give(&bt_sem);
		}
		if (atomic_test_and_clear_bit(flag, FLAG_PAIR)) {
			disconnect_all();
			bt_set_bondable(true);
			is_pairing = true;
			k_work_schedule(&pairing_timeout_work, PAIRING_TIMEOUT);
			atomic_set_bit(flag, FLAG_SLEEP_BEFORE_SCAN);
			k_sem_give(&bt_sem);
		}
		if (atomic_test_and_clear_bit(flag, FLAG_PAIRING_COMPLETE)) {
			LOG_INF("Pairing completed");
			bt_set_bondable(false);
			is_pairing = false;
			settings_runtime_set("event/pairing_complete", NULL, 0);
			atomic_set_bit(flag, FLAG_SCAN);
			k_sem_give(&bt_sem);
		}
	}
}

K_THREAD_DEFINE(bt_id, STACKSIZE, bt_thread, NULL, NULL, NULL, PRIORITY, 0, 0);



int btsrv_handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	// int err;

	name_len = settings_name_next(name, &next);
	if (!next) {
		if (!strncmp(name, "start", name_len)) {
			LOG_INF("<btsrv/start>");
			k_sem_give(&bt_sem);
			return 0;
		}
		if (!strncmp(name, "stop", name_len)) {
			LOG_INF("<btsrv/stop>");
			bt_disable();
			return 0;
		}
		if (!strncmp(name, "pair", name_len)) {
			LOG_INF("<btsrv/pair>");
			atomic_set_bit(flag, FLAG_PAIR);
			k_sem_give(&bt_sem);
			return 0;
		}
	}
	return -ENOENT;
}
/* static subtree handler */
SETTINGS_STATIC_HANDLER_DEFINE(btsrv, "btsrv", NULL, btsrv_handle_set, NULL, NULL);

