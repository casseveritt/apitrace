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


#include "glproc.hpp"
#include "play.hpp"
#include "glplay.hpp"


using namespace glplay;


typedef std::map<unsigned long long, glws::Drawable *> DrawableMap;
typedef std::map<unsigned long long, Context *> ContextMap;
static DrawableMap drawable_map;
static DrawableMap pbuffer_map;
static ContextMap context_map;


static glws::Drawable *
getDrawable(unsigned long long hdc) {
    if (hdc == 0) {
        return NULL;
    }

    DrawableMap::const_iterator it;
    it = drawable_map.find(hdc);
    if (it == drawable_map.end()) {
        return (drawable_map[hdc] = glplay::createDrawable());
    }

    return it->second;
}

static void play_wglCreateContext(trace::Call &call) {
    unsigned long long orig_context = call.ret->toUIntPtr();
    Context *context = glplay::createContext();
    context_map[orig_context] = context;
}

static void play_wglDeleteContext(trace::Call &call) {
    unsigned long long hglrc = call.arg(0).toUIntPtr();

    ContextMap::iterator it;
    it = context_map.find(hglrc);
    if (it == context_map.end()) {
        return;
    }

    delete it->second;
    
    context_map.erase(it);
}

static void play_wglMakeCurrent(trace::Call &call) {
    bool ret = call.ret->toBool();

    glws::Drawable *new_drawable = NULL;
    Context *new_context = NULL;
    if (ret) {
        unsigned long long hglrc = call.arg(1).toUIntPtr();
        if (hglrc) {
            new_drawable = getDrawable(call.arg(0).toUIntPtr());
            new_context = context_map[hglrc];
        }
    }

    glplay::makeCurrent(call, new_drawable, new_context);
}

static void play_wglCopyContext(trace::Call &call) {
}

static void play_wglChoosePixelFormat(trace::Call &call) {
}

static void play_wglDescribePixelFormat(trace::Call &call) {
}

static void play_wglSetPixelFormat(trace::Call &call) {
}

static void play_wglSwapBuffers(trace::Call &call) {
    glws::Drawable *drawable = getDrawable(call.arg(0).toUIntPtr());

    frame_complete(call);
    if (play::doubleBuffer) {
        if (drawable) {
            drawable->swapBuffers();
        } else {
            glplay::Context *currentContext = glplay::getCurrentContext();
            if (currentContext) {
                currentContext->drawable->swapBuffers();
            }
        }
    } else {
        glFlush();
    }
}

static void play_wglShareLists(trace::Call &call) {
    unsigned long long hglrc1 = call.arg(0).toUIntPtr();
    unsigned long long hglrc2 = call.arg(1).toUIntPtr();

    Context *share_context = context_map[hglrc1];
    Context *old_context = context_map[hglrc2];

    Context *new_context = glplay::createContext(share_context);
    if (new_context) {
        glplay::Context *currentContext = glplay::getCurrentContext();
        if (currentContext == old_context) {
            glplay::makeCurrent(call, currentContext->drawable, new_context);
        }

        context_map[hglrc2] = new_context;
        
        delete old_context;
    }
}

static void play_wglCreateLayerContext(trace::Call &call) {
    play_wglCreateContext(call);
}

static void play_wglDescribeLayerPlane(trace::Call &call) {
}

static void play_wglSetLayerPaletteEntries(trace::Call &call) {
}

static void play_wglRealizeLayerPalette(trace::Call &call) {
}

static void play_wglSwapLayerBuffers(trace::Call &call) {
    play_wglSwapBuffers(call);
}

static void play_wglUseFontBitmapsA(trace::Call &call) {
}

static void play_wglUseFontBitmapsW(trace::Call &call) {
}

static void play_wglSwapMultipleBuffers(trace::Call &call) {
}

static void play_wglUseFontOutlinesA(trace::Call &call) {
}

static void play_wglUseFontOutlinesW(trace::Call &call) {
}

static void play_wglCreateBufferRegionARB(trace::Call &call) {
}

static void play_wglDeleteBufferRegionARB(trace::Call &call) {
}

static void play_wglSaveBufferRegionARB(trace::Call &call) {
}

static void play_wglRestoreBufferRegionARB(trace::Call &call) {
}

static void play_wglChoosePixelFormatARB(trace::Call &call) {
}

static void play_wglMakeContextCurrentARB(trace::Call &call) {
}

static void play_wglCreatePbufferARB(trace::Call &call) {
    int iWidth = call.arg(2).toUInt();
    int iHeight = call.arg(3).toUInt();

    unsigned long long orig_pbuffer = call.ret->toUIntPtr();
    glws::Drawable *drawable = glplay::createPbuffer(iWidth, iHeight);

    pbuffer_map[orig_pbuffer] = drawable;
}

static void play_wglGetPbufferDCARB(trace::Call &call) {
    glws::Drawable *pbuffer = pbuffer_map[call.arg(0).toUIntPtr()];

    unsigned long long orig_hdc = call.ret->toUIntPtr();

    drawable_map[orig_hdc] = pbuffer;
}

static void play_wglReleasePbufferDCARB(trace::Call &call) {
}

static void play_wglDestroyPbufferARB(trace::Call &call) {
}

static void play_wglQueryPbufferARB(trace::Call &call) {
}

static void play_wglBindTexImageARB(trace::Call &call) {
}

static void play_wglReleaseTexImageARB(trace::Call &call) {
}

static void play_wglSetPbufferAttribARB(trace::Call &call) {
}

static void play_wglCreateContextAttribsARB(trace::Call &call) {
    unsigned long long orig_context = call.ret->toUIntPtr();
    Context *share_context = NULL;

    if (call.arg(1).toPointer()) {
        share_context = context_map[call.arg(1).toUIntPtr()];
    }

    Context *context = glplay::createContext(share_context);
    context_map[orig_context] = context;
}

static void play_wglMakeContextCurrentEXT(trace::Call &call) {
}

static void play_wglChoosePixelFormatEXT(trace::Call &call) {
}

static void play_wglSwapIntervalEXT(trace::Call &call) {
}

static void play_wglAllocateMemoryNV(trace::Call &call) {
}

static void play_wglFreeMemoryNV(trace::Call &call) {
}

static void play_glAddSwapHintRectWIN(trace::Call &call) {
}

static void play_wglGetProcAddress(trace::Call &call) {
}

const play::Entry glplay::wgl_callbacks[] = {
    {"glAddSwapHintRectWIN", &play_glAddSwapHintRectWIN},
    {"wglAllocateMemoryNV", &play_wglAllocateMemoryNV},
    {"wglBindTexImageARB", &play_wglBindTexImageARB},
    {"wglChoosePixelFormat", &play_wglChoosePixelFormat},
    {"wglChoosePixelFormatARB", &play_wglChoosePixelFormatARB},
    {"wglChoosePixelFormatEXT", &play_wglChoosePixelFormatEXT},
    {"wglCopyContext", &play_wglCopyContext},
    {"wglCreateBufferRegionARB", &play_wglCreateBufferRegionARB},
    {"wglCreateContext", &play_wglCreateContext},
    {"wglCreateContextAttribsARB", &play_wglCreateContextAttribsARB},
    {"wglCreateLayerContext", &play_wglCreateLayerContext},
    {"wglCreatePbufferARB", &play_wglCreatePbufferARB},
    {"wglDeleteBufferRegionARB", &play_wglDeleteBufferRegionARB},
    {"wglDeleteContext", &play_wglDeleteContext},
    {"wglDescribeLayerPlane", &play_wglDescribeLayerPlane},
    {"wglDescribePixelFormat", &play_wglDescribePixelFormat},
    {"wglDestroyPbufferARB", &play_wglDestroyPbufferARB},
    {"wglFreeMemoryNV", &play_wglFreeMemoryNV},
    {"wglGetCurrentContext", &play::ignore},
    {"wglGetCurrentDC", &play::ignore},
    {"wglGetCurrentReadDCARB", &play::ignore},
    {"wglGetCurrentReadDCEXT", &play::ignore},
    {"wglGetDefaultProcAddress", &play::ignore},
    {"wglGetExtensionsStringARB", &play::ignore},
    {"wglGetExtensionsStringEXT", &play::ignore},
    {"wglGetLayerPaletteEntries", &play::ignore},
    {"wglGetPbufferDCARB", &play_wglGetPbufferDCARB},
    {"wglGetPixelFormat", &play::ignore},
    {"wglGetPixelFormatAttribfvARB", &play::ignore},
    {"wglGetPixelFormatAttribfvEXT", &play::ignore},
    {"wglGetPixelFormatAttribivARB", &play::ignore},
    {"wglGetPixelFormatAttribivEXT", &play::ignore},
    {"wglGetProcAddress", &play_wglGetProcAddress},
    {"wglGetSwapIntervalEXT", &play::ignore},
    {"wglMakeContextCurrentARB", &play_wglMakeContextCurrentARB},
    {"wglMakeContextCurrentEXT", &play_wglMakeContextCurrentEXT},
    {"wglMakeCurrent", &play_wglMakeCurrent},
    {"wglQueryPbufferARB", &play_wglQueryPbufferARB},
    {"wglRealizeLayerPalette", &play_wglRealizeLayerPalette},
    {"wglReleasePbufferDCARB", &play_wglReleasePbufferDCARB},
    {"wglReleaseTexImageARB", &play_wglReleaseTexImageARB},
    {"wglRestoreBufferRegionARB", &play_wglRestoreBufferRegionARB},
    {"wglSaveBufferRegionARB", &play_wglSaveBufferRegionARB},
    {"wglSetLayerPaletteEntries", &play_wglSetLayerPaletteEntries},
    {"wglSetPbufferAttribARB", &play_wglSetPbufferAttribARB},
    {"wglSetPixelFormat", &play_wglSetPixelFormat},
    {"wglShareLists", &play_wglShareLists},
    {"wglSwapBuffers", &play_wglSwapBuffers},
    {"wglSwapIntervalEXT", &play_wglSwapIntervalEXT},
    {"wglSwapLayerBuffers", &play_wglSwapLayerBuffers},
    {"wglSwapMultipleBuffers", &play_wglSwapMultipleBuffers},
    {"wglUseFontBitmapsA", &play_wglUseFontBitmapsA},
    {"wglUseFontBitmapsW", &play_wglUseFontBitmapsW},
    {"wglUseFontOutlinesA", &play_wglUseFontOutlinesA},
    {"wglUseFontOutlinesW", &play_wglUseFontOutlinesW},
    {NULL, NULL}
};

