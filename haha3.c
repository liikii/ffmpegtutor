/*
gcc -o haha3 haha3.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
`sdl-config --cflags --libs`


*/
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

// 看不
#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000


typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  // 数据包数量
  int nb_packets;
  // 数据大小 
  int size;
  // 两个锁  主要是防止不同线程读写冲突. 
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;



int main(int argc, char const *argv[])
{
    /* code */
    return 0;
}
