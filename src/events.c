#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(event, LOG_LEVEL_INF);

#define STACKSIZE 1024
#define PRIORITY 7

#define REBOOT_DELAY K_SECONDS(3)

enum {
	FLAG_ONOFF,
	FLAG_PAIR,
	FLAG_FSR_CONNECTIED,
	FLAG_FSR_DISCONNECTED,
	FLAG_CONTROLLER_CONNECTED,
	FLAG_CONTROLLER_DISCONNECTED,
	FLAG_PAIRING_COMPLETE,
	FLAG_NUM,
};
static ATOMIC_DEFINE(flag, FLAG_NUM);

struct {
	bool onoff;
	bool fsr_connected;
	bool controller_connected;
	bool shutdown;
	bool pairing;
} state = {
	.onoff = false,
	.fsr_connected = false,
	.controller_connected = false,
};

static const struct gpio_dt_spec enable_system =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), enable_system_gpios);
static const struct gpio_dt_spec enable_motor =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), enable_motor_gpios);

K_SEM_DEFINE(event_sem, 0, 1);

struct k_work_delayable reboot_work;


static void reboot_handler(struct k_work *work)
{
	LOG_INF("Rebooting system...");
	sys_reboot(SYS_REBOOT_COLD);
}


static void event_handler_thread(void)
{
    k_work_init_delayable(&reboot_work, reboot_handler);

	if (!gpio_is_ready_dt(&enable_system)) {
		LOG_ERR("Error: device %s not ready", enable_system.port->name);
		return;
	}
	if (!gpio_is_ready_dt(&enable_motor)) {
		LOG_ERR("Error: device %s not ready", enable_motor.port->name);
		return;
	}
	gpio_pin_configure_dt(&enable_system, GPIO_OUTPUT_LOW);
	gpio_pin_configure_dt(&enable_motor, GPIO_OUTPUT_LOW);

	while (1) {
        k_sem_take(&event_sem, K_FOREVER);
        if (atomic_test_and_clear_bit(flag, FLAG_ONOFF)) {
			state.onoff = !state.onoff;
            if (gpio_pin_get_dt(&enable_system)) {
                LOG_INF("System is ON, toggling to OFF");
                gpio_pin_set_dt(&enable_system, 0);
                gpio_pin_set_dt(&enable_motor, 0);
				settings_runtime_set("led/poweroff", NULL, 0);
				settings_runtime_set("btsrv/stop", NULL, 0);
				settings_runtime_set("can/stop", NULL, 0);
				state.shutdown = true;
				k_work_schedule(&reboot_work, REBOOT_DELAY);
            } else {
                LOG_INF("System is OFF, toggling to ON");
				if (state.shutdown) {
					LOG_INF("System is shutting down, cannot power on");
					continue;
				}
                gpio_pin_set_dt(&enable_system, 1);
                gpio_pin_set_dt(&enable_motor, 1);
				settings_runtime_set("led/poweron", NULL, 0);
				settings_runtime_set("btsrv/start", NULL, 0);
				settings_runtime_set("can/start", NULL, 0);
            }
        } else if (atomic_test_and_clear_bit(flag, FLAG_PAIR)) {
			LOG_INF("Pairing mode activated");
			if (state.shutdown) {
				continue;
			}
			state.pairing = true;
			settings_runtime_set("btsrv/pair", NULL, 0);
			settings_runtime_set("led/pairing", NULL, 0);
        } else if (atomic_test_and_clear_bit(flag, FLAG_PAIRING_COMPLETE)) {
			LOG_INF("Pairing completed");
			if (state.shutdown) {
				continue;
			}
			state.pairing = false;
			if (state.controller_connected && state.fsr_connected) {
				settings_runtime_set("led/ready", NULL, 0);
			} else {
				settings_runtime_set("led/standby", NULL, 0);
			}
        } else if (atomic_test_and_clear_bit(flag, FLAG_FSR_CONNECTIED)) {
			state.fsr_connected = true;
			LOG_INF("FSR connected");
			if (state.shutdown) {
				continue;
			}
			if (!state.pairing && state.controller_connected) {
				settings_runtime_set("led/ready", NULL, 0);
			}
		} else if (atomic_test_and_clear_bit(flag, FLAG_FSR_DISCONNECTED)) {
			state.fsr_connected = false;
			LOG_INF("FSR disconnected");
			if (state.shutdown) {
				continue;
			}
			if (!state.pairing) {
				settings_runtime_set("led/standby", NULL, 0);
			}
		} else if (atomic_test_and_clear_bit(flag, FLAG_CONTROLLER_CONNECTED)) {
			state.controller_connected = true;
			LOG_INF("Controller connected");
			if (state.shutdown) {
				continue;
			}
			if (!state.pairing && state.fsr_connected) {
				settings_runtime_set("led/ready", NULL, 0);
			}
		} else if (atomic_test_and_clear_bit(flag, FLAG_CONTROLLER_DISCONNECTED)) {
			state.controller_connected = false;
			LOG_INF("Controller disconnected");
			if (state.shutdown) {
				continue;
			}
			if (!state.pairing) {
				settings_runtime_set("led/standby", NULL, 0);
			}
		} else {
			LOG_ERR("Unknown event flag");
		}

	}
}

K_THREAD_DEFINE(event_handler_id, STACKSIZE, event_handler_thread, NULL, NULL, NULL, PRIORITY, 0, 0);




int event_handle_get(const char *name, char *val, int val_len_max)
{
	return -ENOENT;
}

int event_handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	// int err;

	name_len = settings_name_next(name, &next);
	if (!next) {
		if (!strncmp(name, "button_a", name_len)) {
			LOG_INF("<event/button_a>");
			return 0;
		}
		if (!strncmp(name, "button_b", name_len)) {
			LOG_INF("<event/button_b>");
			return 0;
		}
		if (!strncmp(name, "button_ab", name_len)) {
			LOG_INF("<event/button_ab>");
            atomic_set_bit(flag, FLAG_ONOFF);
            k_sem_give(&event_sem);
			return 0;
		}
		if (!strncmp(name, "button_a_long", name_len)) {
			LOG_INF("<event/button_a_long>");
			if (!state.onoff) {
				LOG_ERR("Cannot pair while system is OFF");
				return -EPERM;
			}
			atomic_set_bit(flag, FLAG_PAIR);
            k_sem_give(&event_sem);
			return 0;
		}
		if (!strncmp(name, "button_b_long", name_len)) {
			LOG_INF("<event/button_b_long>");
			if (!state.onoff) {
				LOG_ERR("Cannot pair while system is OFF");
				return -EPERM;
			}
			atomic_set_bit(flag, FLAG_PAIR);
            k_sem_give(&event_sem);
			return 0;
		}
		if (!strncmp(name, "fsr_connection", name_len)) {
			bool val;
			read_cb(cb_arg, &val, sizeof(val));
			LOG_INF("<event/fsr_connection> %s", val ? "true" : "false");
			if (val) {
				atomic_set_bit(flag, FLAG_FSR_CONNECTIED);
			} else {
				atomic_set_bit(flag, FLAG_FSR_DISCONNECTED);
			}
			k_sem_give(&event_sem);
			return 0;
		}
		if (!strncmp(name, "controller_connection", name_len)) {
			bool val;
			read_cb(cb_arg, &val, sizeof(val));
			LOG_INF("<event/controller_connection> %s", val ? "true" : "false");
			if (val) {
				atomic_set_bit(flag, FLAG_CONTROLLER_CONNECTED);
			} else {
				atomic_set_bit(flag, FLAG_CONTROLLER_DISCONNECTED);
			}	
			k_sem_give(&event_sem);
			return 0;
		}
		if (!strncmp(name, "pairing_complete", name_len)) {
			LOG_INF("<event/pairing_complete>");
			if (!state.onoff) {
				LOG_ERR("Cannot complete pairing while system is OFF");
				return -EPERM;
			}
			atomic_set_bit(flag, FLAG_PAIRING_COMPLETE);
			k_sem_give(&event_sem);
			return 0;
		}		
	}
	return -ENOENT;
}
/* static subtree handler */
SETTINGS_STATIC_HANDLER_DEFINE(event, "event", event_handle_get, event_handle_set, NULL, NULL);

