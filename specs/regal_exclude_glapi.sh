#!/bin/sh
grep -v -f regal_exclude_glapi.txt glapi-full.py > glapi.py

