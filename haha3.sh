#!/bin/bash
gcc -o haha3 haha3.c -Wall -lavformat -lavcodec -lswresample -lswscale -lavutil -lm `sdl-config --cflags --libs`
