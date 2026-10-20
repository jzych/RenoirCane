#pragma once
#include "sdk.h"
#include <sys/types.h>
#include <sys/stat.h>
#define ESP_VFS_FLAG_DEFAULT 0
typedef struct {
    int flags;
    int (*open)(const char *, int, int);
    int (*close)(int);
    ssize_t (*write)(int, const void *, size_t);
    int (*fstat)(int, struct stat *);
} esp_vfs_t;
esp_err_t esp_vfs_register(const char *, const esp_vfs_t *, void *);
