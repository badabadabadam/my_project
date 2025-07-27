#ifndef _CONFIG_SERVICE_H_
#define _CONFIG_SERVICE_H_

#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>


// 32e94ac0-19c8-11f0-9cd2-0242ac120002
// 32e94da4-19c8-11f0-9cd2-0242ac120002
// 32e94ef8-19c8-11f0-9cd2-0242ac120002
// 32e94f8e-19c8-11f0-9cd2-0242ac120002
// 32e950ec-19c8-11f0-9cd2-0242ac120002
// 32e95178-19c8-11f0-9cd2-0242ac120002

#define BT_UUID_CONFIG_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x32e94ac0, 0x19c8, 0x11f0, 0x9cd2, 0x0242ac120002)


void init_config_svc(void);
// typedef void (*update_callback_t)(uint16_t *val, size_t val_len);
// void subscribed(int interval, update_callback_t callback);
// void unsubscribed();


#endif /* _CONFIG_SERVICE_H_ */