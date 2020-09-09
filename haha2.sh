#!/bin/bash
gcc -o haha2 haha2.c -Wall -lavformat -lavcodec -lswresample -lswscale -lavutil -lm `sdl-config --cflags --libs`
