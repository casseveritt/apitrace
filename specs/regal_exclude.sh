#!/bin/sh
grep -v -f regal_exclude_glapi.txt glapi-full.py > glapi.py
grep -v -f regal_exclude_glesapi.txt glesapi-full.py > glesapi.py
grep -v -f regal_exclude_glparams.txt glparams-full.py > glparams.py
grep -v -f regal_exclude_gltypes.txt gltypes-full.py > gltypes.py

grep -v -f regal_exclude_eglapi.txt eglapi-full.py > eglapi.py
grep -v -f regal_exclude_cglapi.txt cglapi-full.py > cglapi.py
grep -v -f regal_exclude_glxapi.txt glxapi-full.py > glxapi.py

