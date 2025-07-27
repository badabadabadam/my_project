
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(uart_imu, LOG_LEVEL_INF);

#define STACKSIZE 1024
#define PRIORITY 7

#define IMU0_NODE   DT_ALIAS(imu0)
#define IMU1_NODE   DT_ALIAS(imu1)

#if !DT_NODE_HAS_STATUS_OKAY(IMU0_NODE)
#error "Unsupported board: uart imu0 devicetree alias is not defined"
#endif
#if !DT_NODE_HAS_STATUS_OKAY(IMU1_NODE)
#error "Unsupported board: uart imu1 devicetree alias is not defined"
#endif

struct imu_dev {
	const uint8_t * const name;
	const struct device *dev;
	struct ring_buf *rx_ring_buf;
	bool rx_error;
	bool rx_overflow;
};

#define RING_BUF_SIZE 1024

RING_BUF_DECLARE(imu0_rb, RING_BUF_SIZE);
struct imu_dev imu0 = {
	.name = "imu0",
	.dev = DEVICE_DT_GET(IMU0_NODE),
	.rx_ring_buf = &imu0_rb,
	.rx_error = false,
	.rx_overflow = false,
};

RING_BUF_DECLARE(imu1_rb, RING_BUF_SIZE);
struct imu_dev imu1 = {
	.name = "imu1",
	.dev = DEVICE_DT_GET(IMU1_NODE),
	.rx_ring_buf = &imu1_rb,
	.rx_error = false,
	.rx_overflow = false,
};

static struct imu_dev *imu_list[] = {
    &imu0,
    &imu1,
};

K_SEM_DEFINE(imu_sem, 0, 1);



static void uart_cb(const struct device *dev, void *ctx)
{
	struct imu_dev *imu = (struct imu_dev *)ctx;
	int ret;
	uint8_t *buf;
	uint32_t len;

	while (uart_irq_update(imu->dev) > 0) {
		ret = uart_irq_rx_ready(imu->dev);
		if (ret < 0) {
			imu->rx_error = true;
		}
		if (ret <= 0) {
			break;
		}

		len = ring_buf_put_claim(imu->rx_ring_buf, &buf, RING_BUF_SIZE);
		if (len == 0) {
			/* no space for Rx, disable the IRQ */
			uart_irq_rx_disable(imu->dev);
			imu->rx_overflow = true;
			break;
		}

		ret = uart_fifo_read(imu->dev, buf, len);
		if (ret < 0) {
			imu->rx_error = true;
		}
		if (ret <= 0) {
			break;
		}
		len = ret;

		ret = ring_buf_put_finish(imu->rx_ring_buf, len);
		if (ret != 0) {
			imu->rx_error = true;
			break;
		}
        k_sem_give(&imu_sem);
	}
}

static void process_imu_data(struct imu_dev *imu)
{
    uint8_t *buf;
    uint32_t len;

    while (1) {
        len = ring_buf_get_claim(imu->rx_ring_buf, &buf, RING_BUF_SIZE);
        if (len == 0) {
            break;
        }

        // Process the data in buf
        LOG_INF("%s: Processed %d bytes", imu->name, len);
        // for (uint32_t i = 0; i < len; i++) {
        //     LOG_HEXDUMP_INF(&buf[i], 1, "");
        // }

        ring_buf_get_finish(imu->rx_ring_buf, len);
    }
    if (imu->rx_overflow) {
        LOG_ERR("%s: RX overflow", imu->name);
        imu->rx_overflow = false;
    }
    if (imu->rx_error) {
        LOG_ERR("%s: RX error", imu->name);
        imu->rx_error = false;
    }

}



static void uart_imu_thread(void)
{

	uart_irq_rx_disable(imu0.dev);
	uart_irq_tx_disable(imu0.dev);

	uart_irq_callback_user_data_set(imu0.dev, uart_cb, (void *)&imu0);
	uart_irq_callback_user_data_set(imu1.dev, uart_cb, (void *)&imu1);

	uart_irq_rx_enable(imu0.dev);
	uart_irq_rx_enable(imu1.dev);

	while (1) {
        k_sem_take(&imu_sem, K_FOREVER);

        for (int i = 0; i < ARRAY_SIZE(imu_list); i++) {
            struct imu_dev *imu = imu_list[i];
            // if (imu->rx_overflow || imu->rx_error) {
            //     continue;
            // }
            process_imu_data(imu);
        }

        // process_imu_data(&imu0);
        // process_imu_data(&imu1);
	}
}

K_THREAD_DEFINE(uart_imu_id, STACKSIZE, uart_imu_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
