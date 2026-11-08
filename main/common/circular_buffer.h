#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t size;
    float * buffer_ptr;
    size_t write_idx;
    size_t read_idx;
} circular_buffer_t;


circular_buffer_t * circular_buffer_create(size_t size);
void circular_buffer_push(circular_buffer_t * cb, float value);
void circular_buffer_set_read_offset(circular_buffer_t * cb, size_t offset);
float circular_buffer_read_next(circular_buffer_t * cb);
float circular_buffer_read_prev(circular_buffer_t * cb);

#endif // CIRCULAR_BUFFER_H