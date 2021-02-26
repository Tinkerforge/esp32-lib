/* esp32-lib
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

template <typename T, size_t SIZE>
class TF_Ringbuffer {
public:
    TF_Ringbuffer() : start(0), end(0), buffer{0} {}

    void clear() {
        start = 0;
        end = 0;
    }

    size_t size() {
        return SIZE - 1;
    }

    size_t used() {
        if(end < start) {
            return SIZE + end - start;
        }

        return end - start;
    };

    size_t free() {
        return size() - used();
    }

    void push(T val) {
        buffer[end] = val;
        end++;
        if(end >= sizeof(buffer) / sizeof(buffer[0])) {
            end = 0;
        }

        // This is true if we've just overwritten the oldest item
        if(end == start) {
            ++start;
            if(start >= sizeof(buffer) / sizeof(buffer[0])) {
                start = 0;
            }
        }
    }
    bool pop(T* val) {
        //Silence Wmaybe-uninitialized in the _read_[type] functions.
        *val = 0;
        if(used() == 0) {
            return false;
        }

        *val = buffer[start];
        start++;
        if(start >= sizeof(buffer) / sizeof(buffer[0])) {
            start = 0;
        }

        return true;
    }

    bool peek(T* val) {
        if(used() == 0) {
            return false;
        }

        *val = buffer[start];
        return true;
    }

    bool peek_offset(T* val, size_t offset) {
        if(used() <= offset) {
            return false;
        }

        if (start + offset >= sizeof(buffer) / sizeof(buffer[0])) {
            *val = buffer[start + offset - sizeof(buffer) / sizeof(buffer[0])];
        } else {
            *val = buffer[start + offset];
        }

        return true;
    }

    //index of first valid elemnt
	size_t start;
    //index of first invalid element
	size_t end;
	T buffer[SIZE];
};
