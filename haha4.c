/*
scp haha4.c haha4.sh liikii@192.168.1.104:/home/liikii/tmp3/
*/
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>
// 数学库
#include <math.h>


// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1


typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;


typedef struct VideoPicture {
  SDL_Overlay *bmp;
  int width, height; /* source height & width */
  int allocated;
} VideoPicture;


typedef struct VideoState {
  AVFormatContext *pFormatCtx;
  int             videoStream, audioStream;
  AVStream        *audio_st;
  AVCodecContext  *audio_ctx;
  PacketQueue     audioq;
  uint8_t         audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVFrame         audio_frame;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  AVStream        *video_st;
  AVCodecContext  *video_ctx;
  PacketQueue     videoq;
  struct SwsContext *sws_ctx;

  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  SDL_mutex       *pictq_mutex;
  SDL_cond        *pictq_cond;
  
  SDL_Thread      *parse_tid;
  SDL_Thread      *video_tid;

  char            filename[1024];
  int             quit;
} VideoState;



/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;


void packet_queue_init(PacketQueue *q) {
    //  初始化.  
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}



int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    AVPacketList *pkt1;
    // 复制原因不明. 
    if(av_dup_packet(pkt) < 0) {
        return -1;
    }
    // 
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1){
        return -1;
    }
    pkt1->pkt = *pkt;
    // 因为是新的. next为null
    pkt1->next = NULL;

    // 
    SDL_LockMutex(q->mutex);

    if (!q->last_pkt){
        // 第一. 
        q->first_pkt = pkt1;
    }else{
        q->last_pkt->next = pkt1;
    }
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;

    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
    return 0;
}


static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    // 取出一个给avpacket
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for(;;) {

        if(global_video_state->quit) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            // 最后一个
            if (!q->first_pkt){
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            // 赋值
            *pkt = pkt1->pkt;
            // 原空间释放. 
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            // 非阻塞
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}


int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {
    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1, data_size = 0;
        for(;;) {
            while(audio_pkt_size > 0) {
                int got_frame = 0;
                // 读收据. 
                len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
                if(len1 < 0) {
                    /* if error, skip frame */
                    audio_pkt_size = 0;
                    break;
                }
                audio_pkt_data += len1;
                audio_pkt_size -= len1;
                data_size = 0;
                if(got_frame) {
                    data_size = av_samples_get_buffer_size(NULL, 
                        aCodecCtx->channels,
                        frame.nb_samples,
                        aCodecCtx->sample_fmt,
                        1);
                    assert(data_size <= buf_size);
                    memcpy(audio_buf, frame.data[0], data_size);
                }
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

            if(packet_queue_get(&audioq, &pkt, 1) < 0) {
                return -1;
            }
            audio_pkt_data = pkt.data;
            audio_pkt_size = pkt.size;
    }
}


void audio_callback(void *userdata, Uint8 *stream, int len) {
    VideoState *is = (VideoState *)userdata;
    int len1, audio_size;

    while(len > 0) {
        if(is->audio_buf_index >= is->audio_buf_size) {
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf));
            if(audio_size < 0) {
                /* If error, output silence */
                is->audio_buf_size = 1024;
                memset(is->audio_buf, 0, is->audio_buf_size);
            } else {
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if(len1 > len){
            len1 = len;
        }
        memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}




int main(int argc, char const *argv[])
{
    /* code */
    printf("%s\n", "hello ");
    return 0;
}
