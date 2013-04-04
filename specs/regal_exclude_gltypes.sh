#!/bin/sh
grep -v -f regal_exclude_gltypes.txt gltypes-full.py > gltypes.py

