/**************************************************************************
 *
 * Copyright 2011 Jose Fonseca
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/


#include <assert.h>
#include <string.h>

#include <iostream>

#include "play.hpp"
#include "play_swizzle.hpp"


static void play_malloc(trace::Call &call) {
    size_t size = call.arg(0).toUInt();
    unsigned long long address = call.ret->toUIntPtr();

    if (!address) {
        return;
    }

    void *buffer = malloc(size);
    if (!buffer) {
        std::cerr << "error: failed to allocate " << size << " bytes.";
        return;
    }

    play::addRegion(address, buffer, size);
}


static void play_memcpy(trace::Call &call) {
    void * dest = play::toPointer(call.arg(0));
    void * src  = play::toPointer(call.arg(1));
    size_t n    = call.arg(2).toUInt();

    if (!dest || !src || !n) {
        return;
    }

    memcpy(dest, src, n);
}


const play::Entry play::stdc_callbacks[] = {
    {"malloc", &play_malloc},
    {"memcpy", &play_memcpy},
    {NULL, NULL}
};
