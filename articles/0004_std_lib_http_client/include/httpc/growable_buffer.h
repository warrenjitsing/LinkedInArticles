#pragma once

typedef struct {
    char* data;
    size_t len;
    size_t capacity;
} GrowableBuffer;
