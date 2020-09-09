/*
gcc -o haha3 haha3.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
`sdl-config --cflags --libs`


*/
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

// 确保main不被覆盖
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
    // 确保有个参数
    if(argc < 2) {
        fprintf(stderr, "Usage: test <file>\n");
        exit(1);
    }

    // Register all formats and codecs
    // 注册 解码器 
    av_register_all();

    // 对SDL做初始化  SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER 初始化标志位. 
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }

    // 可能就是存了一点视频文件的一个基础信息, 以供解码用. ffprobe -v quiet -print_format json -show_format -show_streams
    AVFormatContext *pFormatCtx = NULL;
    // Open video file  主要是初始华pFormatCtx
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0){
        return -1; // Couldn't open file
    }


    /* code */
    return 0;
}
