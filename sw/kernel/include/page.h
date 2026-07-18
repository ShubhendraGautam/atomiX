#pragma once

#include <stdint.h>

/* Physical pages are also kernel virtual addresses while the bootstrap uses
 * its identity-mapped Sv32 address space. */
#define PAGE_SIZE 4096u

void page_init(void);
void *page_alloc(void);
void page_free(void *page);
uint32_t page_free_count(void);
