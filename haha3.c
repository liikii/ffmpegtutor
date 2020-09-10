/*


gcc -o haha3 haha3.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
`sdl-config --cflags --libs`

scp haha3.c liikii@192.168.1.104:/home/liikii/tmp3/
--------------------------------
音视频流程
1.av_register_all ffmpeg 注册
2. avformat_open_input  打开文件
3. avformat_find_stream_info 查找流中的数据通道
4. avcodec_free_decoder  根据通道id查找对应的编码
5. avcodec_alloc_context3 根据编码填充默认值
6. avcodec_open2 根据编码初始化内容
7. av_read_frame 获取流中每帧的数据流
8. avcodec_send_packet
9. avcodec_receive_frame
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
        exit(2);
    }

    // https://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    // 可能就是存了一点视频文件的一个基础信息, 以供解码用. ffprobe -v quiet -print_format json -show_format -show_streams
    AVFormatContext *pFormatCtx = NULL;
    // Open video file  主要是初始华pFormatCtx
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0){
        return 3; // Couldn't open file
    }

    // av_dump_format(pFormatCtx, 0, argv[1], 0);
    
    // * Read packets of a media file to get stream information. This
     // * is useful for file formats with no headers such as MPEG. This
     // * function also computes the real framerate in case of MPEG-2 repeat
     // * frame mode.
    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        return 4; // Couldn't find stream information
    }
    
    // 打印一个视频基本信息.
    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, argv[1], 0);
    printf("\n");


    //找到音视频流的位置. 
    // Find the first video stream
    videoStream=-1;
    audioStream=-1;
    for(i=0; i<pFormatCtx->nb_streams; i++) {
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream < 0) {
            videoStream=i;
        }
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream < 0) {
            audioStream=i;
        }
    }
    if(videoStream==-1){
        return -1; // Didn't find a video stream
    }
    if(audioStream==-1){
        return -1;
    }

    AVCodecContext *aCodecCtxOrig = NULL;
    AVCodec         *aCodec = NULL;

    // 编解码环境
    aCodecCtxOrig=pFormatCtx->streams[audioStream]->codec;
    // 解码器
    aCodec = avcodec_find_decoder(aCodecCtxOrig->codec_id);
    if(!aCodec) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    // 
    AVCodecContext *aCodecCtx = NULL;
    // Copy context
    // Allocate an AVCodecContext and set its fields to default values.
    aCodecCtx = avcodec_alloc_context3(aCodec);
    // 新建一个上下文, 具体原因不明. 
    // The resulting destination codec context will be unopened, i.e. you are required to call avcodec_open2() 
    // before you can use this AVCodecContext to decode/encode video/audio data.
    if(avcodec_copy_context(aCodecCtx, aCodecCtxOrig) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        return -1; // Error copying codec context
    }

    // SDL参数
    SDL_AudioSpec   wanted_spec, spec;
    // Set audio settings from codec info
    // int sample_rate; //采样率（仅音频）。
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    //声道数
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;


    /*
    打开音频设备
    opening the audio device. 
    int SDL_OpenAudio(SDL_AudioSpec* desired,
                  SDL_AudioSpec* obtained)

    This function opens the audio device with the desired parameters, and returns 0 if successful, placing the actual hardware parameters in the structure pointed to by obtained.
    If obtained is NULL, the audio data passed to the callback function will be guaranteed to be in the requested format, and will be automatically converted to the actual hardware audio format if necessary. If obtained is NULL, desired will have fields modified.
    desired：期望的参数。
    obtained：实际音频设备的参数，一般情况下设置为NULL即可。
    */
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        return -1;
    }

    avcodec_open2(aCodecCtx, aCodec, NULL);

    /* code */
    return 0;
}
