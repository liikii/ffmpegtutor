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
--------------------------------
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

// 音视频数据队列
PacketQueue audioq;
int quit = 0;


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
    /*
    
    avcodec_open2()的源代码量是非常长的，但是它的调用关系非常简单——它只调用了一个关键的函数，即AVCodec的init()，后文将会对这个函数进行分析。
    我们可以简单梳理一下avcodec_open2()所做的工作，如下所列：
    （1）为各种结构体分配内存（通过各种av_malloc()实现）。
    （2）将输入的AVDictionary形式的选项设置到AVCodecContext。
    （3）其他一些零零碎碎的检查，比如说检查编解码器是否处于“实验”阶段。
    （4）如果是编码器，检查输入参数是否符合编码器的要求
    （5）调用AVCodec的init()初始化具体的解码器。
    */
    // 该函数用于初始化一个视音频编解码器的AVCodecContext。
    avcodec_open2(aCodecCtx, aCodec, NULL);

    /*
    AVFrame中存储的是经过解码后的原始数据。在解码中，AVFrame是解码器的输出；在编码中，AVFrame是编码器的输入。
    AVFrame对象必须调用av_frame_alloc()在堆上分配，注意此处指的是AVFrame对象本身，AVFrame对象必须调用av_frame_free()进行销毁。
    AVFrame中包含的数据缓冲区是
    AVFrame通常只需分配一次，然后可以多次重用，每次重用前应调用av_frame_unref()将frame复位到原始的干净可用的状态。
    */
    AVFrame *pFrame = NULL;
    // Allocate video frame
    pFrame=av_frame_alloc();

    /*
    
    */
    // 到屏幕的像素
    // A structure that contains a collection of pixels used in software blitting.
    SDL_Surface     *screen;
    // Make a screen to put our video
    // 
#ifndef __DARWIN__
    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
#else
    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);
#endif

    if(!screen) {
        fprintf(stderr, "SDL: could not set video mode - exiting\n");
        exit(1);
    }
    // Overlays are the best method available for rendering video. They are based on the YUV color formats, which come in either packed or planar formats. See the link above for detailed information on the different formats and their structures.
    // A SDL_Overlay is similar to a SDL_Surface except it stores a YUV overlay. All the fields are read only, except for pixels which should be locked before use. 
    // Create a new overlay using SDL_CreateYUVOverlay. Pass this the width, height, and format of the overlay, as well as a pointer to the SDL display it should be displayed on. The overlays are scalable, meaning the width and height of the overlay do not need to be the same as the width and height of the surface. As mentioned on the CreateYUVOverlay page, SDL is fastest at 2x scaling. An example:
    SDL_Overlay  *bmp;
    // 初始化overlay
    // Allocate a place to put our YUV image on that screen
    bmp = SDL_CreateYUVOverlay(pCodecCtx->width,
                 pCodecCtx->height,
                 SDL_YV12_OVERLAY,
                 screen);

    // 
    // sws_scale
    //  函数主要是用来做视频像素格式和分辨率的转换,
    struct SwsContext *sws_ctx = NULL;
    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(pCodecCtx->width,
               pCodecCtx->height,
               pCodecCtx->pix_fmt,
               pCodecCtx->width,
               pCodecCtx->height,
               PIX_FMT_YUV420P,
               SWS_BILINEAR,
               NULL,
               NULL,
               NULL
               );

    /* code */
    return 0;
}
