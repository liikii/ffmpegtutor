#!/bin/bash
gcc -o test1 test1.c -Wall -lavformat -lavcodec -lswresample -lswscale -lavutil -lm `sdl-config --cflags --libs`
