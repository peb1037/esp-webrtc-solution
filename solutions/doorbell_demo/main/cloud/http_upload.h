#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int http_upload_put_binary(const char *url, const char *content_type, const uint8_t *data, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif
