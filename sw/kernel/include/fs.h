#pragma once

int fs_mount(void);
void fs_list(void);
int fs_cat(const char *name);
int fs_write(const char *name, const char *data);
