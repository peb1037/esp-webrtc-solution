#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int cloud_ctrl_init(void);
void cloud_ctrl_on_network(bool connected);

#ifdef __cplusplus
}
#endif
