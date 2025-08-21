#include "circular_buffer.h"
#include "esp_heap_caps.h"



circular_buffer_t * circular_buffer_create(size_t size) {
    circular_buffer_t * cb = heap_caps_calloc(1, sizeof(circular_buffer_t), MALLOC_CAP_DEFAULT);
    if (!cb) return NULL;

    cb->buffer_ptr = heap_caps_calloc(size, sizeof(float), MALLOC_CAP_DEFAULT);
    if (!cb->buffer_ptr) {
        free(cb);
        return NULL;
    }

    cb->size = size;
    cb->write_idx = 0;
    cb->read_idx = 0;

    return cb;
}


void circular_buffer_push(circular_buffer_t * cb, float value) {
    cb->buffer_ptr[cb->write_idx] = value;
    cb->write_idx = (cb->write_idx + 1) % cb->size;
}



void circular_buffer_set_read_offset(circular_buffer_t * cb, size_t offset) {
    // Set the read index to the offset of the write idx
    cb->read_idx = (cb->write_idx + offset) % cb->size;
}


float circular_buffer_read_next(circular_buffer_t * cb) {
    // Read the next value from the buffer
    float value = cb->buffer_ptr[cb->read_idx];
    cb->read_idx = (cb->read_idx + 1) % cb->size;

    return value;
}

float circular_buffer_read_prev(circular_buffer_t * cb) {
    // Read the last value from the buffer
    float value = cb->buffer_ptr[cb->read_idx];
    cb->read_idx = (cb->read_idx - 1 + cb->size) % cb->size;

    return value;
}