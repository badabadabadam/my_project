#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(loadcell, LOG_LEVEL_INF);

#define STACKSIZE 1024
#define PRIORITY 7

static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), loadcell);

K_SEM_DEFINE(read_sem, 0, 1);


static void loadcell_thread(void)
{
	int err;
	uint16_t sample;
	struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };

	/* Configure channels individually prior to sampling. */
    if (!adc_is_ready_dt(&adc_channel)) {
        LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
        return;
    }

    err = adc_channel_setup_dt(&adc_channel);
    if (err < 0) {
        LOG_ERR("Could not setup channel (%d)", err);
        return;
    }

	while (1) {
        k_sem_take(&read_sem, K_MSEC(1000));
		LOG_DBG("ADC reading:");
        adc_sequence_init_dt(&adc_channel, &sequence);
        err = adc_read_dt(&adc_channel, &sequence);
        if (err < 0) {
            LOG_ERR("Could not read (%d)", err);
            continue;
        }
        LOG_DBG("sample: %d", sample);
	}
}

K_THREAD_DEFINE(loadcell_id, STACKSIZE, loadcell_thread, NULL, NULL, NULL, PRIORITY, 0, 0);

