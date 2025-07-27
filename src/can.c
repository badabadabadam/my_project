#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/can.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(can, LOG_LEVEL_DBG);

#define STACKSIZE 1024
#define PRIORITY 7

#define TIMER_DELAY 	K_SECONDS(5) 
#define TIMER_INTERVAL 	K_SECONDS(5) //K_MSEC(1) 

#define TX_MSG_ID 0x100
#define RX_MSG_ID 0x200

/* Devicetree */
#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)

K_SEM_DEFINE(tx_sem, 0, 1);

K_THREAD_STACK_DEFINE(rx_thread_stack, STACKSIZE);
K_THREAD_STACK_DEFINE(tx_thread_stack, STACKSIZE);
struct k_thread rx_thread_data, tx_thread_data;

const struct device *const can_dev = DEVICE_DT_GET(CANBUS_NODE);

CAN_MSGQ_DEFINE(rx_msgq, 5);

static void tx_timer_handler(struct k_timer *timer)
{
    k_sem_give(&tx_sem);
	// LOG_DBG("TX timer expired, semaphore given");
}

K_TIMER_DEFINE(tx_timer, tx_timer_handler, NULL);

static void can_tx_callback(const struct device *dev, int error, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    if (error != 0) {
        LOG_ERR("CAN TX callback error: %d", error);
    } else {
        LOG_DBG("CAN frame sent successfully");
    }
}

static void can_tx_thread(void *unused1, void *unused2, void *unused3)
{
	struct can_frame frame = {
        .id = TX_MSG_ID,
        .dlc = 8,
        .flags = CAN_FRAME_IDE,
        .data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};
	int err;

	k_sem_take(&tx_sem, K_FOREVER);
    k_timer_start(&tx_timer, TIMER_DELAY, TIMER_INTERVAL);

    while (true) {
        k_sem_take(&tx_sem, K_FOREVER/*K_MSEC(2)*/);
		LOG_DBG("Preparing to send CAN frame with ID: 0x%08x, DLC: %d",
				frame.id, frame.dlc);
        err = can_send(can_dev, &frame, /*K_MSEC(10)*/K_FOREVER/*K_NO_WAIT*/, can_tx_callback, NULL);
		// LOG_DBG("CAN send returned with error: %d", err);
        if (err != 0) {
            LOG_ERR("failed to enqueue CAN frame (err %d)", err);
        }
	}
}

static void can_rx_thread(void *unused1, void *unused2, void *unused3)
{
	const struct can_filter filter = {
		.flags = CAN_FILTER_IDE,
		.id = RX_MSG_ID,
		.mask = CAN_EXT_ID_MASK
	};
	struct can_frame frame;
	int filter_id;

	filter_id = can_add_rx_filter_msgq(can_dev, &rx_msgq, &filter);
	LOG_INF("Filter id: %d", filter_id);

	while (true) {
		if (!k_msgq_get(&rx_msgq, &frame, K_FOREVER)) {
            LOG_DBG("Received CAN frame with ID: 0x%08x, DLC: %d",
                        frame.id, frame.dlc);
    		// LOG_HEXDUMP_DBG(frame.data, frame.dlc,
            //             "CAN frame data");
        }
	}
}

void state_change_callback(const struct device *dev, enum can_state state,
			   struct can_bus_err_cnt err_cnt, void *user_data)
{
	ARG_UNUSED(user_data);
	ARG_UNUSED(dev);

	LOG_DBG("CAN state changed: %d", state);
}


static int motor_can_init(void)
{
    int err;
    
	LOG_DBG("");

	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN device not ready");
		return -1;
	}

	err = can_set_mode(can_dev, CAN_MODE_FD);
	if (err != 0) {
		LOG_ERR("Error setting CAN FD mode (err %d)", err);
		return 0;
	}

	can_set_state_change_callback(can_dev, state_change_callback, NULL);

	err = can_start(can_dev);
	if (err != 0) {
		LOG_ERR("Error starting CAN controller (err %d)", err);
		return -1;
	}

	k_thread_create(&rx_thread_data, rx_thread_stack,
				 K_THREAD_STACK_SIZEOF(rx_thread_stack),
				 can_rx_thread, NULL, NULL, NULL,
				 PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&rx_thread_data, "can_rx");

	k_thread_create(&tx_thread_data, tx_thread_stack,
				 K_THREAD_STACK_SIZEOF(tx_thread_stack),
				 can_tx_thread, NULL, NULL, NULL,
				 PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&tx_thread_data, "can_tx");

    LOG_INF("CAN controller started successfully");
	return 0;
}

SYS_INIT(motor_can_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);



int can_handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;

	name_len = settings_name_next(name, &next);
	if (!next) {
		if (!strncmp(name, "start", name_len)) {
			LOG_INF("<can/start>");
			k_sem_give(&tx_sem);
			return 0;
		}
	}
	return -ENOENT;
}
/* static subtree handler */
SETTINGS_STATIC_HANDLER_DEFINE(can, "can", NULL, can_handle_set, NULL, NULL);