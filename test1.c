#include <SDL.h>
#include <SDL_thread.h>
#include <stdio.h>

//  gcc -o haha3 haha3.c -lz -lm  `sdl-config --cflags --libs`


int main(int argc, char *arg[]){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }
    printf("%s\n", "中华");
    return 0;
}

