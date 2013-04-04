#!/bin/sh
grep -v -f regal_exclude_glparams.txt glparams-full.py > glparams.py

