#!/bin/bash
gcc -o haha4 haha4.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
`sdl-config --cflags --libs`
