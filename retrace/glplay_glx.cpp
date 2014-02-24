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

#if !defined(HAVE_X11)

#define GLX_PBUFFER_HEIGHT 0x8040
#define GLX_PBUFFER_WIDTH 0x8041

#define GLX_CONTEXT_MAJOR_VERSION_ARB           0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB           0x2092
#define GLX_CONTEXT_FLAGS_ARB                   0x2094
#define GLX_CONTEXT_PROFILE_MASK_ARB            0x9126

#define GLX_CONTEXT_DEBUG_BIT_ARB               0x0001
#define GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB  0x0002

#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB        0x00000001
#define GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

#endif /* !HAVE_X11 */


using namespace glplay;


typedef std::map<unsigned long, glws::Drawable *> DrawableMap;
typedef std::map<unsigned long long, Context *> ContextMap;
static DrawableMap drawable_map;
static ContextMap context_map;


static glws::Drawable *
getDrawable(unsigned long drawable_id) {
    if (drawable_id == 0) {
        return NULL;
    }

    DrawableMap::const_iterator it;
    it = drawable_map.find(drawable_id);
    if (it == drawable_map.end()) {
        return (drawable_map[drawable_id] = glplay::createDrawable());
    }

    return it->second;
}

static Context *
getContext(unsigned long long context_ptr) {
    if (context_ptr == 0) {
        return NULL;
    }

    ContextMap::const_iterator it;
    it = context_map.find(context_ptr);
    if (it == context_map.end()) {
        return (context_map[context_ptr] = glplay::createContext());
    }

    return it->second;
}

static void play_glXCreateContext(trace::Call &call) {
    unsigned long long orig_context = call.ret->toUIntPtr();
    Context *share_context = getContext(call.arg(2).toUIntPtr());

    Context *context = glplay::createContext(share_context);
    context_map[orig_context] = context;
}

static void play_glXCreateContextAttribsARB(trace::Call &call) {
    unsigned long long orig_context = call.ret->toUIntPtr();
    Context *share_context = getContext(call.arg(2).toUIntPtr());

    unsigned major = 1;
    unsigned minor = 0;
    bool core = false;

    const trace::Array * attribs = call.arg(4).toArray();
    if (attribs) {
        size_t i = 0;
        while (i < attribs->values.size()) {
            int param = attribs->values[i++]->toSInt();
            if (param == 0) {
                break;
            }
            int value = attribs->values[i++]->toSInt();

            switch (param) {
            case GLX_CONTEXT_MAJOR_VERSION_ARB:
                major = value;
                break;
            case GLX_CONTEXT_MINOR_VERSION_ARB:
                minor = value;
                break;
            case GLX_CONTEXT_FLAGS_ARB:
                break;
            case GLX_CONTEXT_PROFILE_MASK_ARB:
                if (value & GLX_CONTEXT_CORE_PROFILE_BIT_ARB) {
                    core = true;
                }
                break;
            default:
                break;
            }
        }
    }

    glws::Profile profile = glws::PROFILE_COMPAT;
    if (major >= 3) {
        profile = (glws::Profile)((core ? 0x100 : 0) | (major << 4) | minor);
    }

    Context *context = glplay::createContext(share_context, profile);
    context_map[orig_context] = context;
}

static void play_glXMakeCurrent(trace::Call &call) {
    glws::Drawable *new_drawable = getDrawable(call.arg(1).toUInt());
    Context *new_context = getContext(call.arg(2).toUIntPtr());

    glplay::makeCurrent(call, new_drawable, new_context);
}


static void play_glXDestroyContext(trace::Call &call) {
    ContextMap::iterator it;
    it = context_map.find(call.arg(1).toUIntPtr());
    if (it == context_map.end()) {
        return;
    }

    delete it->second;

    context_map.erase(it);
}

static void play_glXCopySubBufferMESA(trace::Call &call) {
    glws::Drawable *drawable = getDrawable(call.arg(1).toUInt());
    int x = call.arg(2).toSInt();
    int y = call.arg(3).toSInt();
    int width = call.arg(4).toSInt();
    int height = call.arg(5).toSInt();

    drawable->copySubBuffer(x, y, width, height);
}

static void play_glXSwapBuffers(trace::Call &call) {
    glws::Drawable *drawable = getDrawable(call.arg(1).toUInt());

    frame_complete(call);
    if (play::doubleBuffer) {
        if (drawable) {
            drawable->swapBuffers();
        }
    } else {
        glFlush();
    }
}

static void play_glXCreateNewContext(trace::Call &call) {
    unsigned long long orig_context = call.ret->toUIntPtr();
    Context *share_context = getContext(call.arg(3).toUIntPtr());

    Context *context = glplay::createContext(share_context);
    context_map[orig_context] = context;
}

static void play_glXCreatePbuffer(trace::Call &call) {
    const trace::Value *attrib_list = call.arg(2).toArray();
    int width = glplay::parseAttrib(attrib_list, GLX_PBUFFER_WIDTH, 0);
    int height = glplay::parseAttrib(attrib_list, GLX_PBUFFER_HEIGHT, 0);

    unsigned long long orig_drawable = call.ret->toUInt();

    glws::Drawable *drawable = glplay::createPbuffer(width, height);
    
    drawable_map[orig_drawable] = drawable;
}

static void play_glXDestroyPbuffer(trace::Call &call) {
    glws::Drawable *drawable = getDrawable(call.arg(1).toUInt());

    if (!drawable) {
        return;
    }

    delete drawable;
}

static void play_glXMakeContextCurrent(trace::Call &call) {
    glws::Drawable *new_drawable = getDrawable(call.arg(1).toUInt());
    Context *new_context = getContext(call.arg(3).toUIntPtr());

    glplay::makeCurrent(call, new_drawable, new_context);
}

const play::Entry glplay::glx_callbacks[] = {
    //{"glXBindChannelToWindowSGIX", &play_glXBindChannelToWindowSGIX},
    //{"glXBindSwapBarrierNV", &play_glXBindSwapBarrierNV},
    //{"glXBindSwapBarrierSGIX", &play_glXBindSwapBarrierSGIX},
    {"glXBindTexImageEXT", &play::ignore},
    //{"glXChannelRectSGIX", &play_glXChannelRectSGIX},
    //{"glXChannelRectSyncSGIX", &play_glXChannelRectSyncSGIX},
    {"glXChooseFBConfig", &play::ignore},
    {"glXChooseFBConfigSGIX", &play::ignore},
    {"glXChooseVisual", &play::ignore},
    //{"glXCopyContext", &play_glXCopyContext},
    //{"glXCopyImageSubDataNV", &play_glXCopyImageSubDataNV},
    {"glXCopySubBufferMESA", &play_glXCopySubBufferMESA},
    {"glXCreateContextAttribsARB", &play_glXCreateContextAttribsARB},
    {"glXCreateContext", &play_glXCreateContext},
    //{"glXCreateContextWithConfigSGIX", &play_glXCreateContextWithConfigSGIX},
    //{"glXCreateGLXPbufferSGIX", &play_glXCreateGLXPbufferSGIX},
    //{"glXCreateGLXPixmap", &play_glXCreateGLXPixmap},
    //{"glXCreateGLXPixmapWithConfigSGIX", &play_glXCreateGLXPixmapWithConfigSGIX},
    {"glXCreateNewContext", &play_glXCreateNewContext},
    {"glXCreatePbuffer", &play_glXCreatePbuffer},
    {"glXCreatePixmap", &play::ignore},
    //{"glXCreateWindow", &play_glXCreateWindow},
    //{"glXCushionSGI", &play_glXCushionSGI},
    {"glXDestroyContext", &play_glXDestroyContext},
    //{"glXDestroyGLXPbufferSGIX", &play_glXDestroyGLXPbufferSGIX},
    //{"glXDestroyGLXPixmap", &play_glXDestroyGLXPixmap},
    {"glXDestroyPbuffer", &play_glXDestroyPbuffer},
    {"glXDestroyPixmap", &play::ignore},
    //{"glXDestroyWindow", &play_glXDestroyWindow},
    //{"glXFreeContextEXT", &play_glXFreeContextEXT},
    {"glXGetAGPOffsetMESA", &play::ignore},
    {"glXGetClientString", &play::ignore},
    {"glXGetConfig", &play::ignore},
    {"glXGetContextIDEXT", &play::ignore},
    {"glXGetCurrentContext", &play::ignore},
    {"glXGetCurrentDisplayEXT", &play::ignore},
    {"glXGetCurrentDisplay", &play::ignore},
    {"glXGetCurrentDrawable", &play::ignore},
    {"glXGetCurrentReadDrawable", &play::ignore},
    {"glXGetCurrentReadDrawableSGI", &play::ignore},
    {"glXGetFBConfigAttrib", &play::ignore},
    {"glXGetFBConfigAttribSGIX", &play::ignore},
    {"glXGetFBConfigFromVisualSGIX", &play::ignore},
    {"glXGetFBConfigs", &play::ignore},
    {"glXGetMscRateOML", &play::ignore},
    {"glXGetProcAddressARB", &play::ignore},
    {"glXGetProcAddress", &play::ignore},
    {"glXGetSelectedEvent", &play::ignore},
    {"glXGetSelectedEventSGIX", &play::ignore},
    {"glXGetSwapIntervalMESA", &play::ignore},
    {"glXGetSyncValuesOML", &play::ignore},
    {"glXGetVideoSyncSGI", &play::ignore},
    {"glXGetVisualFromFBConfig", &play::ignore},
    {"glXGetVisualFromFBConfigSGIX", &play::ignore},
    //{"glXImportContextEXT", &play_glXImportContextEXT},
    {"glXIsDirect", &play::ignore},
    //{"glXJoinSwapGroupNV", &play_glXJoinSwapGroupNV},
    //{"glXJoinSwapGroupSGIX", &play_glXJoinSwapGroupSGIX},
    {"glXMakeContextCurrent", &play_glXMakeContextCurrent},
    //{"glXMakeCurrentReadSGI", &play_glXMakeCurrentReadSGI},
    {"glXMakeCurrent", &play_glXMakeCurrent},
    {"glXQueryChannelDeltasSGIX", &play::ignore},
    {"glXQueryChannelRectSGIX", &play::ignore},
    {"glXQueryContextInfoEXT", &play::ignore},
    {"glXQueryContext", &play::ignore},
    {"glXQueryDrawable", &play::ignore},
    {"glXQueryExtension", &play::ignore},
    {"glXQueryExtensionsString", &play::ignore},
    {"glXQueryFrameCountNV", &play::ignore},
    {"glXQueryGLXPbufferSGIX", &play::ignore},
    {"glXQueryMaxSwapBarriersSGIX", &play::ignore},
    {"glXQueryMaxSwapGroupsNV", &play::ignore},
    {"glXQueryServerString", &play::ignore},
    {"glXQuerySwapGroupNV", &play::ignore},
    {"glXQueryVersion", &play::ignore},
    //{"glXReleaseBuffersMESA", &play_glXReleaseBuffersMESA},
    {"glXReleaseTexImageEXT", &play::ignore},
    //{"glXResetFrameCountNV", &play_glXResetFrameCountNV},
    //{"glXSelectEvent", &play_glXSelectEvent},
    //{"glXSelectEventSGIX", &play_glXSelectEventSGIX},
    //{"glXSet3DfxModeMESA", &play_glXSet3DfxModeMESA},
    //{"glXSwapBuffersMscOML", &play_glXSwapBuffersMscOML},
    {"glXSwapBuffers", &play_glXSwapBuffers},
    {"glXSwapIntervalEXT", &play::ignore},
    {"glXSwapIntervalSGI", &play::ignore},
    //{"glXUseXFont", &play_glXUseXFont},
    {"glXWaitForMscOML", &play::ignore},
    {"glXWaitForSbcOML", &play::ignore},
    {"glXWaitGL", &play::ignore},
    {"glXWaitVideoSyncSGI", &play::ignore},
    {"glXWaitX", &play::ignore},
    {NULL, NULL},
};

