
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/bluetooth/bluetooth.h>


LOG_MODULE_REGISTER(dev_settings, LOG_LEVEL_INF);

static bt_addr_t local_addr = {
	.val = {0, 0, 0, 0, 0, 0}
};

uint8_t *local_dev_addr() 
{
	return local_addr.val;
}

static int direct_loader(const char *name, size_t len, settings_read_cb read_cb,
	void *cb_arg, void *param)
{
    ARG_UNUSED(name);
    ARG_UNUSED(len);
	read_cb(cb_arg, local_addr.val, sizeof(bt_addr_t));
	return 0;
}

int dev_settings_load(void)
{
    if (settings_subsys_init()) {
        LOG_ERR("Failed to initialize settings subsystem");
        return -1;
    }

    if (settings_load_subtree_direct("device/bdaddr", direct_loader, NULL)) {
        LOG_ERR("Failed to load local address");
        return -1;
    }

    if (bt_addr_cmp(&local_addr, BT_ADDR_ANY) == 0) {
        LOG_INF("No address found, generating random address");
        for (int i = 0; i < sizeof(bt_addr_t); i++) {
            local_addr.val[i] = sys_rand8_get();
        }
        settings_save_one("device/bdaddr", local_addr.val, sizeof(bt_addr_t));
    }

    LOG_INF("Local address: %02x:%02x:%02x:%02x:%02x:%02x",
        local_addr.val[0], local_addr.val[1], local_addr.val[2],
        local_addr.val[3], local_addr.val[4], local_addr.val[5]);

    return 0;
}


