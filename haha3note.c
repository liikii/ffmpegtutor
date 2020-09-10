/*

2.量化的基本概念
2.1 采样大小
一个采样用多少个bit存放，常用的是16bit(这就意味着上述的量化过程中，纵坐标的取值范围是0-65535，声音是没有负值的)。

2.2 采样率
也就是采样频率(1秒采样次数)，一般采样率有8kHz、16kHz、32kHz、44.1kHz、48kHz等，采样频率越高，声音的还原就越真实越自然，当然数据量就越大。
模拟信号中，人类听觉范围是20-20000Hz，如果按照44.1kHz的频率进行采样，对20HZ音频进行采样，一个正玄波采样2200次；对20000HZ音频进行采样，平均一个正玄波采样2.2次。

2.3 声道数
为了播放声音时能够还原真实的声场，在录制声音时在前后左右几个不同的方位同时获取声音，每个方位的声音就是一个声道。声道数是声音录制时的音源数量或回放时相应的扬声器数量，有单声道、双声道、多声道。

2.4 码率
也叫比特率，是指每秒传送的bit数。单位为 bps(Bit Per Second)，比特率越高，每秒传送数据就越多，音质就越好。


码率计算公式：
码率 = 采样率 * 采样大小 * 声道数

比如采样率44.1kHz，采样大小为16bit，双声道PCM编码的WAV文件：
码率=44.1hHz*16bit*2=1411.2kbit/s。
那么录制1分钟的音乐的大小为(1411.2 * 1000 * 60) / 8 / 1024 / 1024 = 10.09M。
链接：https://www.jianshu.com/p/f56114df9c0b

ffprobe -v quiet -print_format json -show_format -show_streams

如AAC解码后得到的是FLT（浮点型），AC3解码是FLTP（带平面）
若 sample 是 AV_SAMPLE_FMT_FLTP，則 sample 會是 float 格式，且值域为 [-1.0, 1.0]
若 sample 是 AV_SAMPLE_FMT_S16， 則 sample 會是 int16 格式，且值域为 [-32767, +32767]


AVPacket  的data 在内存中buffer有两种情况：
1)由av_malloc申请的独立的buffer(unshared buffer)；
2)是其他AVPacket或者其他reuseable 内存的一部分(shared buffer);
av_dup_packet, 通过调用 av_malloc、memcpy、memset等函数， 将shared buffer 的AVPacket duplicate(复制)到独立的buffer中。并且修改AVPacket的析构函数指针av_destruct_pkt。

av_dup_packet, 通过调用 av_malloc、memcpy、memset等函数， 将shared buffer 的AVPacket duplicate(复制)到独立的buffer中。并且修改AVPacket的析构函数指针av_destruct_pkt。

av_dup_packet源码：

00092 int av_dup_packet(AVPacket *pkt)
00093 {
00094     if (((pkt->destruct == av_destruct_packet_nofree) || (pkt->destruct == NULL)) && pkt->data) {
00095         uint8_t *data;  //定义数据栈上的数据指针
00096          We duplicate the packet and don't forget to add the padding again. 
00097         if((unsigned)pkt->size > (unsigned)pkt->size + FF_INPUT_BUFFER_PADDING_SIZE)
00098             return AVERROR(ENOMEM);
00099         data = av_malloc(pkt->size + FF_INPUT_BUFFER_PADDING_SIZE); //申请内存
00100         if (!data) {
00101             return AVERROR(ENOMEM);
00102         }
00103         memcpy(data, pkt->data, pkt->size); //把共享的内存拷过去
00104         memset(data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
00105         pkt->data = data; // 重新设置pkt->data终于有自己的独立内存了，不用共享别的AVPacket的内存
00106         pkt->destruct = av_destruct_packet; //设置析构函数指针
00107     }
00108     return 0;
00109 }

ffplay的源码地址：http://ffmpeg.org/doxygen/trunk/ffplay_8c_source.html
ffplay中有两个队列一个PacketQueue， 一个FrameQueue，下面我们对队列中AVPacket和AVFrame关系进行分析和说明。
一、AVPacket 和 AVFrame 结构含义
AVPacket
用于存储压缩的数据，分别包括有音频压缩数据，视频压缩数据和字幕压缩数据。它通常在解复用操作后存储压缩数据，然后作为输入传给解码器。或者由编码器输出然后传递给复用器。对于视频压缩数据，一个AVPacket通常包括一个视频帧。对于音频压缩数据，可能包括几个压缩的音频帧。
AVFrame
用于存储解码后的音频或者视频数据。AVFrame必须通过av_frame_alloc进行分配，通过av_frame_free释放。
两者之间的关系
av_read_frame得到压缩的数据包AVPacket，一般有三种压缩的数据包(视频、音频和字幕)，都用AVPacket表示。
然后调用avcodec_send_packet 和 avcodec_receive_frame对AVPacket进行解码得到AVFrame。
注：从 FFmpeg 3.x 开始，avcodec_decode_video2 就被废弃了，取而代之的是 avcodec_send_packet 和 avcodec_receive_frame。
二、ffplay 队列关系
ffplay中有三种PacketQueue，分别为视频包队列、音频包队列和字幕包队列。
相应地也有三种FrameQueue，视频帧队列、音频帧队列和字幕帧队列。
队列的初始化工作是在stream_open函数中进行，分别通过packet_queue_init和frame_queue_init执行初始化操作。需要注意的是，初始化中PacketQueue没有手动分配AVPacket结构，而是直接使用解码过程中的AVPacket。FrameQueue中则是通过av_frame_alloc手动分配了AVFrame结构。
在read_thread函数中，通过av_read_frame函数读取数据包，然后调用packet_queue_put将AVPacket添加到PacketQueue中。
在video_thread函数中，通过get_video_frame函数读取数据帧，然后调用queue_picture将AVFrame添加到FrameQueue中。
那么两个队列是怎么联系起来的呢？通过分析read_thread函数可以知晓：
首先，创建解复用和解码所需要的数据结构。然后，分别通过stream_component_open函数打开三种数据流。最后，通过av_read_frame将解复用后的数据包分别添加到对应的PacketQueue中。在stream_component_open函数主要负责解码工作，ffplay中为解码工作专门设置了一个数据结构Decoder，Decoder结构中有一个成员queue，这个queue就是指的输入的PacketQueue，通过decoder_init函数来指定PacketQueue。这个工作就是在stream_component_open中执行的。指定PacketQueue之后通过get_video_frame函数从PacketQueue中解码出AVFrame结构，最后通过queue_picture函数将解码得到的帧添加到FrameQueue。



typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;//队首，队尾
    int nb_packets;//队列中一共有多少个节点
    int size;//队列所有节点字节总数，用于计算cache大小
    int64_t duration;//队列所有节点的合计时长
    int abort_request;//是否要中止队列操作，用于安全快速退出播放
    int serial;//序列号，和MyAVPacketList的serial作用相同，但改变的时序稍微有点不同
    SDL_mutex *mutex;//用于维持PacketQueue的多线程安全(SDL_mutex可以按pthread_mutex_t理解）
    SDL_cond *cond;//用于读、写线程相互通知(SDL_cond可以按pthread_cond_t理解)
} PacketQueue



------------------------------------------------------------


ffplay用PacketQueue保存解封装后的数据，即保存AVPacket。

ffplay首先定义了一个结构体MyAVPacketList：

typedef struct MyAVPacketList {
    AVPacket pkt;//解封装后的数据
    struct MyAVPacketList *next;//下一个节点
    int serial;//序列号
} MyAVPacketList;
可以理解为是队列的一个节点。可以通过其next字段访问下一个节点。

所以这里我认为命名为MyAVPacketNode更为合理
serial字段主要用于标记当前节点的序列号，ffplay中多处用到serial的概念，一般用于区分是否连续数据。在后面的代码分析中我们还会看到它的作用。

接着定义另一个结构体PacketQueue：

typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;//队首，队尾
    int nb_packets;//队列中一共有多少个节点
    int size;//队列所有节点字节总数，用于计算cache大小
    int64_t duration;//队列所有节点的合计时长
    int abort_request;//是否要中止队列操作，用于安全快速退出播放
    int serial;//序列号，和MyAVPacketList的serial作用相同，但改变的时序稍微有点不同
    SDL_mutex *mutex;//用于维持PacketQueue的多线程安全(SDL_mutex可以按pthread_mutex_t理解）
    SDL_cond *cond;//用于读、写线程相互通知(SDL_cond可以按pthread_cond_t理解)
} PacketQueue;
这个结构体内定义了“队列”自身的属性。上面的注释对每个字段作了简单的介绍，接下来我们从队列的操作函数具体分析各个字段的含义。

PacketQueue操作提供以下方法：

packet_queue_init：初始化
packet_queue_destroy：销毁
packet_queue_start：启用
packet_queue_abort：中止
packet_queue_get：获取一个节点
packet_queue_put：存入一个节点
packet_queue_put_nullpacket：存入一个空节点
packet_queue_flush：清除队列内所有的节点
初始化用于初始各个字段的值，并创建mutex和cond：

static int packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}
相应的，销毁过程负责清理mutex和cond:

static void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);//先清除所有的节点
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}
启用队列：

static void packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);//这里放入了一个flush_pkt
    SDL_UnlockMutex(q->mutex);
}
flush_pkt定义是static AVPacket flush_pkt;，是一个特殊的packet，主要用来作为非连续的两端数据的“分界”标记。

中止队列：

static void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond);//释放一个条件信号
    SDL_UnlockMutex(q->mutex);
}
这里SDL_CondSignal的作用在于确保当前等待该条件的线程能被激活并继续执行退出流程。

读、写是PacketQueue的主要方法。

先看写——往队列中放入一个节点：

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;
 
    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);//主要实现在这里
    SDL_UnlockMutex(q->mutex);
 
    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);//放入失败，释放AVPacket
 
    return ret;
}
主要实现在函数packet_queue_put_private，这里需要注意的是如果放入失败，需要释放AVPacket。

static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;
 
    if (q->abort_request)//如果已中止，则放入失败
       return -1;
 
    pkt1 = av_malloc(sizeof(MyAVPacketList));//分配节点内存
    if (!pkt1)//内存不足，则放入失败
        return -1;
    pkt1->pkt = *pkt;//拷贝AVPacket(浅拷贝，AVPacket.data等内存并没有拷贝)
    pkt1->next = NULL;
    if (pkt == &flush_pkt)//如果放入的是flush_pkt，需要增加队列的序列号，以区分不连续的两段数据
        q->serial++;
    pkt1->serial = q->serial;//用队列序列号标记节点
 
    //队列操作：如果last_pkt为空，说明队列是空的，新增节点为队头；否则，队列有数据，则让原队尾的next为新增节点。 最后将队尾指向新增节点
    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
 
    //队列属性操作：增加节点数、cache大小、cache总时长
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;
 
    // XXX: should duplicate packet data in DV case 
    //发出信号，表明当前队列中有数据了，通知等待中的读线程可以取数据了
    SDL_CondSignal(q->cond);
    return 0;
}
对packet_queue_put_private笔者增加了详细注释，应该比较容易理解了。

主要完成3件事：

计算serial。serial标记了这个节点内的数据是何时的。一般情况下新增节点与上一个节点的serial是一样的，但当队列中加入一个flush_pkt后，后续节点的serial会比之前大1.
队列操作。经典的队列实现方式，不展开了。
队列属性操作。更新队列中节点的数目、占用字节数（含AVPacket.data的大小）及其时长。
再来看读——从队列中取一个节点：

// return < 0 if aborted, 0 if no packet and > 0 if packet.  
//block: 调用者是否需要在没节点可取的情况下阻塞等待
//AVPacket: 输出参数，即MyAVPacketList.pkt
//serial: 输出参数，即MyAVPacketList.serial
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
    MyAVPacketList *pkt1;
    int ret;
 
    SDL_LockMutex(q->mutex);
 
    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }
 
        //......这里是省略的代码，取一个节点，然后break
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}
函数较长，我们先省略for循环的主体部分，简单看下函数整体流程。整体流程比较清晰：加锁，进入循环，如果此时需要退出，则break，返回-1；否则，取一个节点，然后break。

这里for循环主要充当一个“壳”，以方便在一块多分支代码中可以通过break调到统一的出口。
对于加锁情况下的多分支return，这是一个不错的写法。但要小心这是一把双刃剑，没有仔细处理每个分支，容易陷入死循环。
然后看for的主体：

pkt1 = q->first_pkt;//MyAVPacketList *pkt1; 从队头拿数据
        if (pkt1) {//队列中有数据
            q->first_pkt = pkt1->next;//队头移到第二个节点
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;//节点数减1
            q->size -= pkt1->pkt.size + sizeof(*pkt1);//cache大小扣除一个节点
            q->duration -= pkt1->pkt.duration;//总时长扣除一个节点
            *pkt = pkt1->pkt;//返回AVPacket，这里发生一次AVPacket结构体拷贝，AVPacket的data只拷贝了指针
            if (serial)//如果需要输出serial，把serial输出
                *serial = pkt1->serial;
            av_free(pkt1);//释放节点内存
            ret = 1;
            break;
        } else if (!block) {//队列中没有数据，且非阻塞调用
            ret = 0;
            break;
        } else {//队列中没有数据，且阻塞调用
            SDL_CondWait(q->cond, q->mutex);//这里没有break。for循环的另一个作用是在条件变量满足后重复上述代码取出节点
        }
我们知道队列是一个先进先出的模型，所以从队头拿数据。对于没有取到数据的情况，根据block参数进行判断是否阻塞，如果阻塞，通过SDL_CondWait等待信号。

如果有取到数据，主要分3个步骤：

队列操作：转移队头、扣除大小。这里nb_packets和duration的运算较明显，size需要注意也要扣除AVPacket的size
给输出参数赋值：基本就是MyAVPacketList拍平传递给输出参数pkt和serial即可
释放节点内存：释放放入队列时申请的节点内存
最后是提供了几个"util"方法：

packet_queue_put_nullpacket放入“空包”。放入空包意味着流的结束，一般在视频读取完成的时候放入空包。该函数的实现很明了，构建一个空包，然后调用packet_queue_put:

static int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}
packet_queue_flush用于将队列中的所有节点清除。比如用于销毁队列、seek操作等。

static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;
 
    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    SDL_UnlockMutex(q->mutex);
}
函数主体的for循环是队列遍历，遍历过程释放节点和AVPacket。最后将PacketQueue的属性恢复为空队列状态。

至此，我们分析了PacketQueue的实现和主要的操作方法。

现在总结下两个关键的点：

第一，PacketQueue的内存管理：



MyAVPacketList的内存是完全由PacketQueue维护的，在put的时候malloc，在get的时候free。

AVPacket分两块，一部分是AVPacket结构体的内存，这部分从MyAVPacketList的定义可以看出是和MyAVPacketList共存亡的。另一部分是AVPacket字段指向的内存，这部分一般通过av_packet_unref函数释放。一般情况下，是在get后由调用者负责用av_packet_unref函数释放。特殊的情况是当碰到packet_queue_flush或put失败时，这时需要队列自己处理。

第二，serial的变化过程：



如上图所示，左边是队头，右边是队尾，从左往右标注了5个节点的serial，以及放入对应节点时queue的serial。

可以看到放入flush_pkt的时候后，serial增加了1.

要区分的是上图虽然看起来queue的serial和节点的serial是相等的，但这是放入时相等，在取出时是不等的。假设，现在要从队头取出一个节点，那么取出的节点是serial 1，而PacketQueue自身的queue已经增长到了2.

代码背后的设计思路：

设计一个多线程安全的队列，保存AVPacket，同时统计队列内已缓存的数据大小。（这个统计数据会用来后续设置要缓存的数据量）
引入serial的概念，区别前后数据包是否连续
设计了两类特殊的packet——flush_pkt和nullpkt，用于更细致的控制（类似用于多线程编程的事件模型——往队列中放入flush事件、放入null事件）
------------------------------------------------------------
audio_decode_frame::::
其中audio_decode_frame函数会从queue中取出packet，并对packet中的frame进行解码和resample，然后将数据放入audio_buf_tmp缓冲区中。
------------------------------------------------------------
其中audio_decode_frame函数会从queue中取出packet，并对packet中的frame进行解码和resample，然后将数据放入audio_buf_tmp缓冲区中。
------------------------------------------------------------

一、概述

最近在学习ffmpeg解码的内容，参考了官方的教程http://dranger.com/ffmpeg/tutorial03.html，结果发现这个音频解码的教程有点问题。参考了各种博客，并同时啃ffplay.c的源码，发现avcodec_decode_audio4多了一个resample（重采样）的概念。

其解码以及播放音频的思路为：



首先，ffmpeg设置本机的audio播放参数（target format），如freq(频率）为44100，format为AV_SAMPLE_FMT_S16，channels为2。这个播放参数是SDL实际播放音频时使用的参数。

但是！但是我们的audio file（如mp3文件）的audio数据很可能有其自己的audio播放参数（source format），而这些参数不同于我们实际的SDL播放参数，于是ffmpeg在其中插入resample（重采用）的过程，将source format转换成target format。

简单的说就是一个audio参数设置思路的转变：



这个思路转变最大的好处，就是本机播放的格式可以不用再迁就audio file，而是可以根据自己的需要自行设定，缺点很显然就是ffmpeg的CPU开销会增大。

 

二、代码示例（源码见“附录”）

源码在官方教程基础上把其中视频部分删除，在main函数最后加上一个无限循环，并添加resample函数，最后将resample插入到sdl的回调函数之中。

源码中关于queue的代码为官网教程原版复制，其主要作用就是让main函数和SDL audio线程互斥的push queue和get queue，以下不再赘述。

1、main函数代码结构

main函数伪代码结构如下：

复制代码
 1 SDL Initialization
 2 ffmpeg open audio file
 3 Set SDL audio parameters
 4 Set ffmpeg audio parameters(target format)
 5 while(ffmpeg_read_frame(pkt)) {
 6     packet_queue_put(pkt);
 7 }
 8 while(1) {
 9     sleep(1);
10 }
复制代码
ffmpeg从audio file中不停的读取数据，并将读出的packet放入queue中。此时我们要清楚，另外还有一个SDL audio线程在等待queue中的数据。

2、SDL audio线程

SDL audio线程主要执行一个回调函数，对应源码中的函数为audio_callback(void * userdata, Uint8 * stream, int len)。这个函数的使命就是将解码后的数据放入参数stream这个缓冲区中，以便SDL audio线程从stream缓冲区中获取数据play。这个缓冲区的大小为参数len，而userdata则是用户自定的参数。其伪代码结构如下：

复制代码
1 audio_buf_index = 0;
2 while(len > 0) {
3     audio_size = audio_decode_frame(audio_buf_tmp);
4     memcpy(stream, audio_buf_tmp, audio_size);
5     len -= audio_size;
6     stream += audio_size;
7     audio_buf_index += audio_size;
8 }
复制代码
其中audio_decode_frame函数会从queue中取出packet，并对packet中的frame进行解码和resample，然后将数据放入audio_buf_tmp缓冲区中。

3、Resample函数

Resample的过程和结构体SwrContext息息相关。使用这个结构体共需要2步。

1、先初始化SwrContex，指定target format和source format；

2、使用已初始化的SwrContext，对frame进行resample。

Resample的伪代码如下：

复制代码
 1 struct SwrContext * swr_ctx = NULL;
 2 audio_hw_params_src = audio_hw_params_tgt
 3 int resample(AVFrame * af, uint8_t * audio_buf, int * audio_buf_size)
 4 {
 5     if(audio_hw_params_src != audio_hw_params(af)) {
 6         swr_ctx = swr_alloc_set_opts(audio_hw_params_tgt, audio_hw_params(af));
 7         audio_hw_params_src = audio_hw_params(af);
 8     }
 9     in = af;
10     swr_convert(swr_ctx, out, in);
11     audio_buf = out;
12 } 
复制代码
一开始，audio_hw_parames_src（source format）被初始化为target format，在resample获得第一个frame后，会从该frame中提取source format，并将其赋值给audio_hw_params_src，同时初始化SwrContext这个结构体，指定target format和source format。然后swr_convert对输入的frame进行resample(swr_convert），然后将resample后得到的数据放进resample函数指定的缓冲区（audio_buf）中。



tbn= the time base in AVStream that has come from the container

tbc= the time base in AVCodecContext for the codec used for a particular stream

tbr= tbr is guessed from the video stream and is the value users want to see
when they look for the video frame rate, except sometimes it is twice
what one would expect because of field rate versus frame rate.

25  tbr代表帧率；

12800 tbn代表文件层（st）的时间精度，即1S=12800，和duration相关；

50   tbc代表视频层（st->codec）的时间精度，即1S=50，和strem->duration和时间戳相关。

便于理解，下图为我打印的解码后的时间戳 也就是 视频是25帧 的， 1S = 50 ;



avformat_find_stream_info前后AVFormatContext的区别
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'test.mp4':
  Metadata:
    major_brand     : isom
    minor_version   : 512
    compatible_brands: isomiso2avc1mp41
    encoder         : Lavf58.51.101
  Duration: 00:00:05.08, bitrate: N/A
    Stream #0:0(und): Video: h264 (avc1 / 0x31637661), none, 1280x720, 1356 kb/s, SAR 1:1 DAR 16:9, 25 fps, 25 tbr, 12800 tbn (default)
    Metadata:
      handler_name    : VideoHandler
    Stream #0:1(und): Audio: aac (mp4a / 0x6134706D), 48000 Hz, 2 channels, 132 kb/s (default)
    Metadata:
      handler_name    : SoundHandler
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'test.mp4':
  Metadata:
    major_brand     : isom
    minor_version   : 512
    compatible_brands: isomiso2avc1mp41
    encoder         : Lavf58.51.101
  Duration: 00:00:05.08, start: 0.000000, bitrate: 1496 kb/s
    Stream #0:0(und): Video: h264 (Main) (avc1 / 0x31637661), yuv420p, 1280x720 [SAR 1:1 DAR 16:9], 1356 kb/s, 25 fps, 25 tbr, 12800 tbn, 50 tbc (default)
    Metadata:
      handler_name    : VideoHandler
    Stream #0:1(und): Audio: aac (LC) (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 132 kb/s (default)
    Metadata:
      handler_name    : SoundHandler

没有的
start: 0.000000, bitrate: 1496 kb/s
yuv420p
stereo, fltp, 

avformat_find_stream_info
 * Read packets of a media file to get stream information. This
 * is useful for file formats with no headers such as MPEG. This
 * function also computes the real framerate in case of MPEG-2 repeat
 * frame mode.
 * The logical file position is not changed by this function;
 * examined packets may be buffered for later processing.



AVCodecContext是一个描述编解码器上下文的数据结构，包含了众多编解码器需要的参数信息，位于avcodec.h文件中。

2.常见变量及其作用

enum AVMediaType codec_type; //编解码器的类型（视频，音频...）。
const struct AVCodec  *codec; //采用的解码器AVCodec（H.264,MPEG2...）。
int64_t bit_rate;//平均比特率。
uint8_t *extradata;//针对特定编码器包含的附加信息（例如对于H.264解码器来说，存储SPS，PPS等）。
int extradata_size;
AVRational time_base;//时间的基准单位，根据该参数，可以把PTS转化为实际的时间（单位为秒s）。
编解码延迟。
int delay;//编码：从编码器输入到解码器输出的帧延迟数。解码：除了规范中规定的标准解码器外产生的帧延迟数。
int width, height;//代表宽和高（仅视频）。
int refs;//运动估计参考帧的个数（H.264的话会有多帧，MPEG2这类的一般就没有了）。
int sample_rate; //采样率（仅音频）。
int channels; //声道数（仅音频）。
enum AVSampleFormat sample_fmt;  //音频采样格式，编码：由用户设置。解码：由libavcodec设置。
int frame_size;//音频帧中每个声道的采样数。编码：由libavcodec在avcodec_open2（）中设置。 解码：可以由一些解码器设置以指示恒定的帧大小.
int frame_number;//帧计数器，由libavcodec设置。解码：从解码器返回的帧的总数。编码：到目前为止传递给编码器的帧的总数。
uint64_t channel_layout;//音频声道布局。编码：由用户设置。解码：由用户设置，可能被libavcodec覆盖。
enum AVAudioServiceType audio_service_type;//音频流传输的服务类型。编码：由用户设置。解码：由libavcodec设置。


AVCodec;
3.常见变量及其作用
const char *name;//编解码器的名字，比较短。在编码器和解码器之间是全局唯一的。 这是用户查找编解码器的主要方式。
const char *long_name;//编解码器的名字，全称，比较长。
enum AVMediaType type;//指明了类型，是视频，音频，还是字幕
enum AVCodecID id;
const AVRational *supported_framerates;//支持的帧率（仅视频）
const enum AVPixelFormat *pix_fmts;//支持的像素格式（仅视频）
const int *supported_samplerates;//支持的采样率（仅音频）
const enum AVSampleFormat *sample_fmts;//支持的采样格式（仅音频）
const uint64_t *channel_layouts;//支持的声道数（仅音频）
int priv_data_size;//私有数据的大小
4.常用函数作用
void (*init_static_data)(struct AVCodec *codec);//初始化编解码器静态数据，从avcodec_register（）调用。
int (*encode2)(AVCodecContext *avctx, AVPacket *avpkt, const AVFrame *frame, int *got_packet_ptr);//将数据编码到AVPacket。
int (*decode)(AVCodecContext *, void *outdata, int *outdata_size, AVPacket *avpkt);//解码数据到AVPacket。
int (*close)(AVCodecContext *);//关闭编解码器。
void (*flush)(AVCodecContext *);//刷新缓冲区。当seek时会被调用。
每一个编解码器对应一个该结构体。程序运行时，上述函数指针会赋值为对应的编解码器的函数。


*/


