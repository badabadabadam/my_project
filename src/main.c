/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
#include <app_version.h>

#include "bt_main.h"
#include "config_svc.h"


LOG_MODULE_REGISTER(main);

#define FSR_TAG 		0x01
#define CONTROLLER_TAG 	0x02 

enum debug_flag {
	FLAG_FSR = 0,
	FLAG_CONTROLLER,
	FLAG_NUM,
};

static ATOMIC_DEFINE(flags, FLAG_NUM);
#ifdef CONFIG_HAS_BLE_FSR
K_MSGQ_DEFINE(fsr_msgq, sizeof(struct fsr_data), 10, 1);
#endif
#ifdef CONFIG_HAS_BLE_CONTROLLER
K_MSGQ_DEFINE(controller_msgq, sizeof(struct controller_data), 10, 1);
#endif
struct k_poll_event events[] = {
#ifdef CONFIG_HAS_BLE_FSR
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                                    K_POLL_MODE_NOTIFY_ONLY,
                                    &fsr_msgq, FSR_TAG),
#endif
#ifdef CONFIG_HAS_BLE_CONTROLLER
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                                    K_POLL_MODE_NOTIFY_ONLY,
                                    &controller_msgq, CONTROLLER_TAG),
#endif
};

int main(void)
{
	LOG_INF("Application Version: %s", APP_VERSION_EXTENDED_STRING);

	while (1) {
		int err = k_poll(events, ARRAY_SIZE(events), K_FOREVER);
		if (err) {
			LOG_ERR("k_poll failed: %d", err);
			continue;
		}

		for (int i = 0; i < ARRAY_SIZE(events); i++) {
#ifdef CONFIG_HAS_BLE_FSR	
			if (events[i].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE && events[i].tag == FSR_TAG) {
				events[i].state = K_POLL_STATE_NOT_READY;
				struct fsr_data data;
				err = k_msgq_get(events[i].msgq, &data, K_NO_WAIT);
				if (err) {
					LOG_ERR("k_msgq_get failed: %d", err);
					continue;
				}
				if (atomic_test_bit(flags, FLAG_FSR)) {
					LOG_INF("FSR data: %u %u %u %u", data.value[0], data.value[1], data.value[2], data.value[3]);
				}
			}		
#endif
#ifdef CONFIG_HAS_BLE_CONTROLLER
			if (events[i].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE && 
				events[i].tag == CONTROLLER_TAG) {
				struct controller_data data;
				events[i].state = K_POLL_STATE_NOT_READY;
				err = k_msgq_get(events[i].msgq, &data, K_NO_WAIT);
				if (err) {
					LOG_ERR("k_msgq_get failed: %d", err);
					continue;
				}
				if (atomic_test_bit(flags, FLAG_CONTROLLER)) {
					LOG_INF("Controller data: %u", data.value);
				}
			}	
#endif
		}
	}

	return 0;
}

static int cmd_monitor_fsr_start(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (atomic_test_and_set_bit(flags, FLAG_FSR)) {
		shell_print(sh, "fsr monitoring already started\n");
		return 0;
	}
	shell_print(sh, "fsr started\n");
	return 0;
}

static int cmd_monitor_fsr_stop(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (atomic_test_and_clear_bit(flags, FLAG_FSR)) {
		shell_print(sh, "fsr monitoring not started\n");
		return 0;
	}
	shell_print(sh, "fsr stopped\n");

	return 0;
}

static int cmd_monitor_controller_start(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (atomic_test_and_set_bit(flags, FLAG_CONTROLLER)) {
		shell_print(sh, "controller monitoring already started\n");
		return 0;
	}
	shell_print(sh, "controller started\n");
	return 0;
}

static int cmd_monitor_controller_stop(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (atomic_test_and_clear_bit(flags, FLAG_CONTROLLER)) {
		shell_print(sh, "controller monitoring not started\n");
		return 0;
	}
	shell_print(sh, "controller stopped\n");
	return 0;
}


#define START_HELP ("<cmd>\n\nStart monitoring")
#define STOP_HELP  ("<cmd>\n\nStop monitoring")

SHELL_STATIC_SUBCMD_SET_CREATE(fsr_subcmd,
	/* Alphabetically sorted to ensure correct Tab autocompletion. */
	SHELL_CMD_ARG(start, NULL, START_HELP, cmd_monitor_fsr_start, 1, 0),
	SHELL_CMD_ARG(stop, NULL, STOP_HELP, cmd_monitor_fsr_stop, 1, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(controller_subcmd,
	/* Alphabetically sorted to ensure correct Tab autocompletion. */
	SHELL_CMD_ARG(start, NULL, START_HELP, cmd_monitor_controller_start, 1, 0),
	SHELL_CMD_ARG(stop, NULL, STOP_HELP, cmd_monitor_controller_stop, 1, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(monitor_subcmd,
	SHELL_CMD(fsr, &fsr_subcmd, "FSR sensor data", NULL),
	SHELL_CMD(controller, &controller_subcmd, "Controller data", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(monitor, &monitor_subcmd, "Monitor commands", NULL);
