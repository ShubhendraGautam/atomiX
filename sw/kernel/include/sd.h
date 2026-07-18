#pragma once

#include <stdint.h>

int sd_init(void);
int sd_read_block(uint32_t block, uint8_t *data);
int sd_write_block(uint32_t block, const uint8_t *data);
