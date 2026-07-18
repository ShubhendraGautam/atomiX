#include <stdint.h>

#include "page.h"

extern char _end[];
extern char __stack_bottom[];

/* One free-list link lives in each free physical page. This is deliberately
 * simple: it needs no backing allocator and makes the ownership of each 4 KiB
 * page explicit for the forthcoming process address spaces. */
struct free_page {
  struct free_page *next;
};

static struct free_page *free_list;
static uint32_t free_pages;

static uintptr_t page_align_up(uintptr_t address) {
  return (address + PAGE_SIZE - 1u) & ~(uintptr_t)(PAGE_SIZE - 1u);
}

void page_init(void) {
  uintptr_t page = page_align_up((uintptr_t)_end);
  const uintptr_t limit = (uintptr_t)__stack_bottom;

  free_list = 0;
  free_pages = 0;
  while (page + PAGE_SIZE <= limit) {
    page_free((void *)page);
    page += PAGE_SIZE;
  }
}

void *page_alloc(void) {
  struct free_page *page = free_list;
  if (page == 0) return 0;
  free_list = page->next;
  free_pages--;
  return page;
}

void page_free(void *page) {
  struct free_page *const node = (struct free_page *)page;
  node->next = free_list;
  free_list = node;
  free_pages++;
}

uint32_t page_free_count(void) {
  return free_pages;
}
