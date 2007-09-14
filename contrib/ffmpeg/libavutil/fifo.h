/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file fifo.h
 * A very simple circular buffer FIFO implementation.
 */

#ifndef FIFO_H
#define FIFO_H

typedef struct AVFifoBuffer {
    uint8_t *buffer;
    uint8_t *rptr, *wptr, *end;
} AVFifoBuffer;

/**
 * Initializes an AVFifoBuffer.
 * @param *f AVFifoBuffer to initialize
 * @param size of FIFO
 * @return <0 for failure >=0 otherwise
 */
int av_fifo_init(AVFifoBuffer *f, int size);

/**
 * Frees an AVFifoBuffer.
 * @param *f AVFifoBuffer to free
 */
void av_fifo_free(AVFifoBuffer *f);

/**
 * Returns the amount of data in bytes in the AVFifoBuffer, that is the
 * amount of data you can read from it.
 * @param *f AVFifoBuffer to read from
 * @return size
 */
int av_fifo_size(AVFifoBuffer *f);

/**
 * Reads data from an AVFifoBuffer.
 * @param *f AVFifoBuffer to read from
 * @param *buf data destination
 * @param buf_size number of bytes to read
 */
int av_fifo_read(AVFifoBuffer *f, uint8_t *buf, int buf_size);

/**
 * Feeds data from an AVFifoBuffer to a user supplied callback.
 * @param *f AVFifoBuffer to read from
 * @param buf_size number of bytes to read
 * @param *func generic read function
 * @param *dest data destination
 */
int av_fifo_generic_read(AVFifoBuffer *f, int buf_size, void (*func)(void*, void*, int), void* dest);

/**
 * Writes data into an AVFifoBuffer.
 * @param *f AVFifoBuffer to write to
 * @param *buf data source
 * @param size data size
 */
void av_fifo_write(AVFifoBuffer *f, const uint8_t *buf, int size);

/**
 * Resizes an AVFifoBuffer.
 * @param *f AVFifoBuffer to resize
 * @param size new AVFifoBuffer size in bytes
 */
void av_fifo_realloc(AVFifoBuffer *f, unsigned int size);

/**
 * Reads and discards the specified amount of data from an AVFifoBuffer.
 * @param *f AVFifoBuffer to read from
 * @param size amount of data to read in bytes
 */
void av_fifo_drain(AVFifoBuffer *f, int size);

static inline uint8_t av_fifo_peek(AVFifoBuffer *f, int offs)
{
    uint8_t *ptr = f->rptr + offs;
    if (ptr >= f->end)
        ptr -= f->end - f->buffer;
    return *ptr;
}
#endif /* FIFO_H */
