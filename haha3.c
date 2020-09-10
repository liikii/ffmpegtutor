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

/*
Queues
There! Now we're ready to start pulling audio information from the stream. 
But what do we do with that information? We are going to be continuously getting packets 
from the movie file, but at the same time SDL is going to call the callback function! 
The solution is going to be to create some kind of global structure that we can stuff audio packets 
in so our audio_callback has something to get audio data from! So what we're going to do is to create a queue of packets. 
ffmpeg even comes with a structure to help us with this: AVPacketList, which is just a linked list for packets. Here's our queue structure:
*/
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


// 音频数据队列
PacketQueue audioq;
// 控制窗口标志
int quit = 0;


// 初始化 PacketQueue
void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}


// packetqueue 里是  avpacketlist, packetlist是avpacket包与下一包地址. 
// 放音频包
// SDL_LockMutex() locks the mutex in the queue so we can add something to it, 
// and then SDL_CondSignal() sends a signal to our get function (if it is waiting) 
// through our condition variable to tell it that there is data and it can proceed, 
// then unlocks the mutex to let it go.
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    // 
    AVPacketList *pkt1;
    // 拷贝包. 
    if(av_dup_packet(pkt) < 0) {
        return -1;
    }
    /*
    typedef struct AVPacketList  
    {  
    AVPacket pkt;  
    struct AVPacketList *next; // 用于把各个 AVPacketList 串联起来。  
    } AVPacketList;  
    */
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1){
        return -1;
    }
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt){
        // queue 为空
        q->first_pkt = pkt1;
    }else{
        新包进来把原最后包下一个指到新包上. 
        q->last_pkt->next = pkt1;
    }
    // 追回为尾包.
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    // 发加数据信号给要的人. 

    SDL_CondSignal(q->cond);
    // 释放锁
    SDL_UnlockMutex(q->mutex);
    return 0;
}

// 取音频包
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block){
    // PacketQueue *q 全局队列. 
    AVPacketList *pkt1;
    int ret;
    SDL_LockMutex(q->mutex);

    for(;;) {
        if(quit) {
            ret = -1;
            break;
        }
        // 取第一个. 
        pkt1 = q->first_pkt;
        if (pkt1) {
            // 把第一个从队列踢出  下一个设为第一个. 
            q->first_pkt = pkt1->next;
            // 如果没有后续
            if (!q->first_pkt){
                q->last_pkt = NULL;
            }
            // 包数减一. 
            q->nb_packets--;
            // 大小减去取的包大小. 
            q->size -= pkt1->pkt.size;
            // 赋值
            *pkt = pkt1->pkt;
            // 释放空间
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            //  SDL_CondWait() makes the function block (i.e. pause until we get data) if we tell it to.
            // all CondWait does is wait for a signal from SDL_CondSignal() (or SDL_CondBroadcast()) and 
            // then continue. However, it looks as though we've trapped it within our mutex — 
            // if we hold the lock, our put function can't put anything in the queue! However, what SDL_CondWait() also does 
            // for us is to unlock the mutex we give it and then attempt to lock it again once we get the signal.
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

/*
 Then, once we have a packet to work with, we call avcodec_decode_audio4(), which acts a lot like its sister function, avcodec_decode_video(), except in this case, a packet might have more than one frame, so you may have to call it several times to get all the data out of the packet. Once we have the frame, we simply copy it to our audio buffer, making sure the data_size is smaller than our audio buffer. Also, remember the cast to audio_buf, because SDL gives an 8 bit int buffer, and ffmpeg gives us data in a 16 bit int buffer. You should also notice the difference between len1 and data_size. len1 is how much of the packet we've used, and data_size is the amount of raw data returned.
When we've got some data, we immediately return to see if we still need to get more data from the queue, or if we are done. If we still had more of the packet to process, we save it for later. If we finish up a packet, we finally get to free that packet.
So that's it! We've got audio being carried from the main read loop to the queue, which is then read by the audio_callback function, which hands that data to SDL, which SDL beams to your sound card. Go ahead and compile:
*/
// 解音频包
int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {
    // 包
    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1, data_size = 0;

    for(;;) {
        // 有数据
        while(audio_pkt_size > 0) {
            int got_frame = 0;
            len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
            if(len1 < 0) {
                // 解码出错
                /* if error, skip frame */
                audio_pkt_size = 0;
                break;
            }
            // 没出错 继续
            // 数据大小
            audio_pkt_data += len1;
            // 数据大小
            audio_pkt_size -= len1;


            data_size = 0;
            if(got_frame) {
                // av_sample_get_buffer_size来计算音频占用的字节数。
                data_size = av_samples_get_buffer_size(NULL, 
                       aCodecCtx->channels,
                       frame.nb_samples,
                       aCodecCtx->sample_fmt,
                       1);
                assert(data_size <= buf_size);
                memcpy(audio_buf, frame.data[0], data_size);
            }
            // 没取frame
            if(data_size <= 0) {
                /* No data yet, get more frames */
                continue;
            }
            /* We have data, return it and come back for more later */
            return data_size;
        }
        if(pkt.data){
            av_free_packet(&pkt);
        }

        if(quit) {
            return -1;
        }

        // 取数据包
        if(packet_queue_get(&audioq, &pkt, 1) < 0) {
            return -1;
        }
        // 设数据大小
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}


//  音频播放段 回调用. 
// 把len长的数据写到stream里, 以供播放. 
void audio_callback(void *userdata, Uint8 *stream, int len) {
    // 解码上下文
    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;

    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    // 读了多少数据到buff. 
    static unsigned int audio_buf_size = 0;
    // 写到那了. 
    static unsigned int audio_buf_index = 0;

    // 准备写数据
    // This is basically a simple loop that will pull in data from another function we will write, 
    // audio_decode_frame(), store the result in an intermediary buffer, 
    // attempt to write len bytes to stream, and get more data if we don't have enough yet,
     // or save it for later if we have some left over. 
    // The size of audio_buf is 1.5 times the size of the largest audio frame that 
    // ffmpeg will give us, which gives us a nice cushion.
    while(len > 0) {
        // buffer 已用完. 读点数据
        if(audio_buf_index >= audio_buf_size) {
            
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
            if(audio_size < 0) {
                // 没有读到数据, 就播放无音. 
                /* If error, output silence */
                audio_buf_size = 1024; // arbitrary?
                memset(audio_buf, 0, audio_buf_size);
            } else {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        // 有多少数据
        len1 = audio_buf_size - audio_buf_index;
        if(len1 > len){
            // 数据过多, 只写要的数量. 
            len1 = len;
        }
        // memcpy(d, s+11, 6);// 从第 11 个字符(r)开始复制，连续复制 6 个字符(runoob)
        // 或者 memcpy(d, s+11*sizeof(char), 6*sizeof(char));
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        // 还剩多少待写, 如果是0 小于0 退出了. 完成了. 
        len -= len1;
        // stream 指针前移
        stream += len1;
        // buffer 索引前移
        audio_buf_index += len1;
    }
}



int main(int argc, char const *argv[])
{   
    // 确保有个文件传过来
    if(argc < 2) {
        fprintf(stderr, "Usage: test <file>\n");
        exit(1);
    }

    // Register all formats and codecs
    // 注册 解码器, 类似总体的一个初始化
    av_register_all();

    // 对SDL做初始化  SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER 初始化标志位. 
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(2);
    }

    // 解码上下文. 
    // https://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    // 可能就是存了一点视频文件的一个基础信息, 以供解码用. ffprobe -v quiet -print_format json -show_format -show_streams
    AVFormatContext *pFormatCtx = NULL;

    // Open video file  主要是初始化文件上下文
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0){
        return 3; // Couldn't open file
    }

    // 补充文件信息
    // av_dump_format(pFormatCtx, 0, argv[1], 0);
    // * Read packets of a media file to get stream information. This
     // * is useful for file formats with no headers such as MPEG. This
     // * function also computes the real framerate in case of MPEG-2 repeat
     // * frame mode.
    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        return 4; // Couldn't find stream information
    }
    
    // 打印文件基本信息.
    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, argv[1], 0);
    printf("\n");


    //找到音视频流的索引. 
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

    // 解码上下文
    AVCodecContext *aCodecCtxOrig = NULL;
    // 解码器
    AVCodec         *aCodec = NULL;

    // 编解码环境初始化
    aCodecCtxOrig=pFormatCtx->streams[audioStream]->codec;
    // 解码器  音频解码用
    aCodec = avcodec_find_decoder(aCodecCtxOrig->codec_id);
    if(!aCodec) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    // 新建解音频上下文 
    AVCodecContext *aCodecCtx = NULL;
    // Copy context
    // Allocate an AVCodecContext and set its fields to default values.
    // 音频解码上下文初始化
    aCodecCtx = avcodec_alloc_context3(aCodec);
    // 新建一个解音频上下文
    // The resulting destination codec context will be unopened, i.e. you are required to call avcodec_open2() 
    // before you can use this AVCodecContext to decode/encode video/audio data.
    if(avcodec_copy_context(aCodecCtx, aCodecCtxOrig) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        return -1; // Error copying codec context
    }

    // SDL参数 音频播放参数
    SDL_AudioSpec   wanted_spec, spec;
    // Set audio settings from codec info
    // int sample_rate; //采样率（仅音频）。
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    //声道数
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    // 音频回调函数 另一线程调用. 
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
    音频播放
    */
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        return -1;
    }
    /*
    初化化
    avcodec_open2()的源代码量是非常长的，但是它的调用关系非常简单——它只调用了一个关键的函数，即AVCodec的init()，后文将会对这个函数进行分析。
    我们可以简单梳理一下avcodec_open2()所做的工作，如下所列：
    （1）为各种结构体分配内存（通过各种av_malloc()实现）。
    （2）将输入的AVDictionary形式的选项设置到AVCodecContext。
    （3）其他一些零零碎碎的检查，比如说检查编解码器是否处于“实验”阶段。
    （4）如果是编码器，检查输入参数是否符合编码器的要求
    （5）调用AVCodec的init()初始化具体的解码器。
    */
    // 该函数用于初始化一个视音频编解码器的AVCodecContext。
    //
    avcodec_open2(aCodecCtx, aCodec, NULL);
    
    // 初始化音频数据队列
    // audio_st = pFormatCtx->streams[index]
    packet_queue_init(&audioq);

    // 开始播放
    // SDL_PauseAudio(1);  // audio callback is stopped when this returns.
    // SDL_Delay(5000);  // audio device plays silence for 5 seconds
    // SDL_PauseAudio(0);  // audio callback starts running again.
    SDL_PauseAudio(0);

    // ---------------------------------------------------------------------------------------------------
    // ---------------------------------------------------------------------------------------------------
    // 准备解 视频.
    AVCodecContext *pCodecCtxOrig = NULL;
    // Get a pointer to the codec context for the video stream
    pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;

    AVCodec *pCodec = NULL;
    // 视频解码器
    // Find the decoder for the video stream
    pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
    if(pCodec==NULL) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1; // Codec not found
    }

    AVCodecContext *pCodecCtx = NULL;
    // 初始化视频解码上下文
    // Copy context
    pCodecCtx = avcodec_alloc_context3(pCodec);
    // 再次初始化, 可能是添加一些参数到
    if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        return -1; // Error copying codec context
    }

      // Open codec
    if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
        return -1; // Could not open codec
    }

    // 像素转换
    // sws_scale
    //  函数主要是用来做音视频像素格式和分辨率的转换,
    struct SwsContext *sws_ctx = NULL;
    // 初始化像素转换上下文
    // initialize SWS context for software scaling
    //sws: software scaling;; 
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

    // 视频窗口面
    // 到屏幕的像素
    // A structure that contains a collection of pixels used in software blitting.
    SDL_Surface     *screen;
    // 初始化窗口
    // Make a screen to put our video
#ifndef __DARWIN__
    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
#else
    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);
#endif

    if(!screen) {
        fprintf(stderr, "SDL: could not set video mode - exiting\n");
        exit(1);
    }

    // 显示用 类screen
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
    
    
     /*
    AVFrame中存储的是经过解码后的原始数据。在解码中，AVFrame是解码器的输出；在编码中，AVFrame是编码器的输入。
    AVFrame对象必须调用av_frame_alloc()在堆上分配，注意此处指的是AVFrame对象本身，AVFrame对象必须调用av_frame_free()进行销毁。
    AVFrame中包含的数据缓冲区是
    AVFrame通常只需分配一次，然后可以多次重用，每次重用前应调用av_frame_unref()将frame复位到原始的干净可用的状态。
    */
    // 画面帧
    AVFrame *pFrame = NULL;
    // Allocate video frame
    // 初始化画面帧
    pFrame=av_frame_alloc();

    AVPacket  packet;
    int       frameFinished;
    // 读文件包
    while(av_read_frame(pFormatCtx, &packet)>=0) {
        // Is this a packet from the video stream?
        // 是视频流的话
        if(packet.stream_index==videoStream) {
            // 解包
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            // 解没解完
            // Did we get a video frame?
            if(frameFinished) {
                // 如果解完
                // 锁住screen. 可能是防止读写冲突
                SDL_LockYUVOverlay(bmp);

                // bmp数据指到pict上. 
                AVPicture pict;
                pict.data[0] = bmp->pixels[0];
                pict.data[1] = bmp->pixels[2];
                pict.data[2] = bmp->pixels[1];
                // // bmp数据指到pict上. 
                pict.linesize[0] = bmp->pitches[0];
                pict.linesize[1] = bmp->pitches[2];
                pict.linesize[2] = bmp->pitches[1];

                // 格式转化. ()
                // Convert the image into YUV format that SDL uses  
                sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
                pFrame->linesize, 0, pCodecCtx->height,
                pict.data, pict.linesize);

                // 放开读写保护. 
                SDL_UnlockYUVOverlay(bmp);
                // 显示位置
                rect.x = 0;
                rect.y = 0;
                rect.w = pCodecCtx->width;
                rect.h = pCodecCtx->height;
                // 显示图
                SDL_DisplayYUVOverlay(bmp, &rect);
                av_free_packet(&packet);
            }
        } else if(packet.stream_index==audioStream) { // 是音频流的话
            // 音频到音频队列
            packet_queue_put(&audioq, &packet);
        } else {  // 其它流  具体我不清楚
            // Free the packet that was allocated by av_read_frame
            av_free_packet(&packet);
        }

        
        SDL_PollEvent(&event);
            switch(event.type) {
            case SDL_QUIT:
                quit = 1;
                SDL_Quit();
                exit(0);
                break;
            default:
                break;
        }

    }

    // 释放内存
    // Free the YUV frame
    av_frame_free(&pFrame);
    // Close the codecs
    avcodec_close(pCodecCtxOrig);
    avcodec_close(pCodecCtx);
    avcodec_close(aCodecCtxOrig);
    avcodec_close(aCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    return 0;
}
