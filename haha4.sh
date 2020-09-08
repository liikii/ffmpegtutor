#!/bin/bash
gcc -o haha4 haha4.c -Wall -lavformat -lavcodec -lswresample -lswscale -lavutil -lm `sdl-config --cflags --libs`
