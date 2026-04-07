#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*aws_iot_msg_cb_t)(const char *topic, int topic_len, const char *data, int data_len, void *ctx);

int aws_iot_start(void);
void aws_iot_stop(void);
bool aws_iot_is_connected(void);

int aws_iot_subscribe(const char *topic, int qos);
int aws_iot_publish(const char *topic, const char *payload, int payload_len, int qos, int retain);

void aws_iot_set_message_callback(aws_iot_msg_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
