#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

// gcc -o haha haha.c -Wall -lavformat -lavcodec -lswresample -lswscale -lavutil -lm
// gcc -o tutorial01 tutorial01.c -lavutil -lavformat -lavcodec -lz -lavutil -lm
// If you have an older version of ffmpeg, you may need to drop -lavutil:
// gcc -o tutorial01 tutorial01.c -lavformat -lavcodec -lz -lm
// 

void print_argv(int argc, char *argv[]){
    
    int i;
    printf("%d\n",argc);
    for(i=0;i<argc;i++)
    {
        printf("%s ",argv[i]);
    }
    printf("\n");
}

void print_error(int err_code){
    char errbuf[100];
    av_strerror(err_code, errbuf, sizeof(errbuf));
    printf("error: %s \n", errbuf);
}


void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
  FILE *pFile;
  char szFilename[32];
  int  y;
  
  // Open file
  sprintf(szFilename, "frame%d.ppm", iFrame);
  pFile=fopen(szFilename, "wb");
  if(pFile==NULL)
    return;
  
  // Write header
  fprintf(pFile, "P6\n%d %d\n255\n", width, height);
  
  // Write pixel data
    for (y=0; y<height; y++){
        // data是一个指针数组，数组的每一个元素是一个指针，指向视频中图像的某一plane或音频中某一声道的plane。
        /*
        uint8_t *data[AV_NUM_DATA_POINTERS]：指针数组，存放YUV数据的地方。如图所示，一般占用前3个指针，分别指向Y，U，V数据。
        对于packed格式的数据（例如RGB24），会存到data[0]里面。
        对于planar格式的数据（例如YUV420P），则会分开成data[0]，data[1]，data[2]…（YUV420P中data[0]存Y，data[1]存U，data[2]存V）
        */
        /*
        size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
        参数
        ptr -- 这是指向要被写入的元素数组的指针。
        size -- 这是要被写入的每个元素的大小，以字节为单位。
        nmemb -- 这是元素的个数，每个元素的大小为 size 字节。
        stream -- 这是指向 FILE 对象的指针，该 FILE 对象指定了一个输出流。
        
        AVPicture结构中data和linesize关系

        AVPicture里面有data[4]和linesize[4]其中data是一个指向指针的指针(二级、二维指针)，也就是指向视频数据缓冲区的首地址，而data[0]~data[3]是一级指针，可以用如下的图来表示：

        data -->xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
                    ^                         ^                       ^
                    |                           |                        |
                data[0]                data[1]              data[2]

        比如说，当pix_fmt=PIX_FMT_YUV420P时，data中的数据是按照YUV的格式存储的，也就是：

        data -->YYYYYYYYYYYYYYYYYYYYYYYYUUUUUUUUUUUVVVVVVVVVVVV
                    ^                                        ^                      ^
                    |                                          |                       |
               data[0]                                data[1]             data[2]

         

        linesize是指对应于每一行的大小，为什么需要这个变量，是因为在YUV格式和RGB格式时，每行的大小不一定等于图像的宽度。

               linesize = width + padding size(16+16) for YUV
               linesize = width*pixel_size  for RGB
        padding is needed during Motion Estimation and Motion Compensation for Optimizing MV serach and  P/B frame reconstruction

         

        for RGB only one channel is available
        so RGB24 : data[0] = packet rgbrgbrgbrgb......
                   linesize[0] = width*3
        data[1],data[2],data[3],linesize[1],linesize[2],linesize[2] have no any means for RGB
        测试如下：(原始的320×182视频)
        如果pix_fmt=PIX_FMT_RGBA32
        linesize 的只分别为：1280  0    0     0

        如果pix_fmt=PIX_FMT_RGB24
        linesize 的只分别为：960   0    0     0

        如果pix_fmt=PIX_FMT_YUV420P
        linesize 的只分别为：352   176  176   0         
        */
        // 指针的加法
        // 说你是个指针你就是个指针
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
    }

  // Close file
  fclose(pFile);
}


int main(int argc, char *argv[]) {
    //print_argv(argc, argv);
    if(argc < 2){
       return 2;
    }

    printf("----------------------------\n");
    printf("avformat: %d.%d.%d\n", LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR, LIBAVFORMAT_VERSION_MICRO);
    printf("avcodec: %d.%d.%d\n", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);
    printf("----------------------------\n");

    av_register_all();
    AVFormatContext *pFormatCtx = NULL;
     // Open video file
    if(avformat_open_input(&pFormatCtx, argv[1], 0, 0)<0){
       printf("invalid file.");
       return 3; // Couldn't open file
    }
    
    //char a[1024];
    //strcpy(a, pFormatCtx->filename);
    //printf("%s", a);
    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        return 4; // Couldn't find stream information
    }
    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, argv[1], 0);
    
    printf("-----------------------------------------------\n");

    int i;

    // Find the first video stream
    int videoStream=-1;
    for(i=0; i<pFormatCtx->nb_streams; i++){
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
            videoStream=i;
            break;
        }
    }
        
    if(videoStream==-1){
        printf("Didn't find a video stream\n");
        return -1; // Didn't find a video stream
    }

    printf("video stream index:  %d\n", i);

    /*
    The stream's information about the codec is in what we call the "codec context." This contains all the information about the codec that the stream is using, and now we have a pointer to it. But we still have to find the actual codec and open it:
    */
    AVCodecContext *pCodecCtxOrig = NULL;
    AVCodecContext *pCodecCtx = NULL;
    // Get a pointer to the codec context for the video stream
    pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;

    AVCodec *pCodec = NULL;
    // Find the decoder for the video stream
    // Find the decoder with the CodecID id. Returns NULL on failure. 
    // This should be called after getting the desired AVCodecContext from a stream in AVFormatContext, using codecCtx->codec_id.

    pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
    if(pCodec==NULL) {
      fprintf(stderr, "Unsupported codec!\n");
      return -1; // Codec not found
    }

    // //  // 配置解码器
    // Copy context
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    // if(avcodec_copy_context(pCodecCtxOrig, pCodecCtx) != 0) {
    //     fprintf(stderr, "Couldn't copy codec context");
    //     return -1; // Error copying codec context
    // }
    /*
    Copy the settings of the source AVCodecContext into the destination AVCodecContext. 
    The resulting destination codec context will be unopened, i.e. 
    you are required to call avcodec_open2() before you can use this AVCodecContext 
    to decode/encode video/audio data.
    dest should be initialized with avcodec_alloc_context3(NULL), but otherwise uninitialized.
    dest should be initialized with avcodec_alloc_context3(NULL), but otherwise uninitialized.
    */
    /*
    Note that we must not use the AVCodecContext from the video stream directly! 
    So we have to use avcodec_copy_context() to copy the context to a new location (after allocating memory for it, of course).
    */
    int av_cc_i = avcodec_copy_context(pCodecCtx, pCodecCtxOrig);
    
    if(av_cc_i != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        print_error(av_cc_i);
        return -1; // Error copying codec context
    }

    // AVCodecContext *dest, avcodec_copy_context you are required to call avcodec_open2() before you can use this AVCodecContext to decode/encode video/audio data.
     // Open codec
    if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
        return -1; // Could not open codec
    }

    // AVFrame中存储的是经过解码后的原始数据。在解码中，AVFrame是解码器的输出；在编码中，AVFrame是编码器的输入。
    AVFrame *pFrame = NULL;

    // Allocate video frame
    pFrame=av_frame_alloc();
    if(pFrame==NULL){
        printf("av_frame_alloc error\n");
        return -1;
    }

    // Since we're planning to output PPM files, which are stored in 24-bit RGB, 
    // we're going to have to convert our frame from its native format to RGB. 
    // ffmpeg will do these conversions for us. For most projects (including ours)
    // we're going to want to convert our initial frame to a specific format. Let's allocate a frame for the converted frame now.
    AVFrame *pFrameRGB = NULL;
    pFrameRGB=av_frame_alloc();

    if(pFrameRGB==NULL){
        printf("av_frame_alloc error\n");
        return -1;
    }

    /*
    Even though we've allocated the frame, we still need a place to put the raw data when we convert it. 
    We use avpicture_get_size to get the size we need, and allocate the space manually:
    */
    uint8_t *buffer = NULL;
    int numBytes;
    // Determine required buffer size and allocate buffer
    // Calculates how many bytes will be required for a picture of the given width, height, and pic format.
    numBytes=avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

    // Memory allocation of size byte with alignment suitable for all memory accesses (including vectors if available on the CPU). av_malloc(0) must return a non NULL pointer.
    // av_malloc is ffmpeg's malloc that is just a simple wrapper around malloc that makes sure the memory addresses are aligned and such. It will not protect you from memory leaks, double freeing, or other malloc problems.
    buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

    // Now we use avpicture_fill to associate the frame with our newly allocated buffer. About the AVPicture cast: the AVPicture struct is a subset of the AVFrame struct - the beginning of the AVFrame struct is identical to the AVPicture struct.

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    // 这个函数的使用本质上是为已经分配的空间的结构体AVPicture挂上一段用于保存数据的空间，这个结构体中有一个指针数组data[4]，挂在这个数组里。一般我们这么使用：
    avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);


    // ----------------------------------
    // Reading the Data
    // ----------------------------------
    // struct SwsContext  （software scale） 主要用于视频图像的转换，比如格式转换：
    // struct SwrContext   （software resample） 主要用于音频重采样，比如采样率转换，声道转换
    // What we're going to do is read through the entire video stream by reading in the packet, decoding it into our frame, and once our frame is complete, we will convert and save it.
    // The struct in which raw packet data is stored. This data should be given to avcodec_decode_audio2 or avcodec_decode_video to to get a frame.
    struct SwsContext *sws_ctx = NULL;
    int frameFinished;
    // 
    /*
    1.AVPacket简介
    AVPacket是FFmpeg中很重要的一个数据结构，它保存了解复用（demuxer)之后，解码（decode）之前的数据（仍然是压缩后的数据）和关于这些数据的一些附加的信息，如显示时间戳（pts），解码时间戳（dts）,数据时长（duration），所在流媒体的索引（stream_index）等等。
    对于视频（Video）来说，AVPacket通常包含一个压缩的Frame；而音频（Audio）则有可能包含多个压缩的Frame。并且，一个packet也有可能是空的，不包含任何压缩数据data，只含有边缘数据side data（side data,容器提供的关于packet的一些附加信息，例如，在编码结束的时候更新一些流的参数,在另外一篇av_read_frame会介绍）
    AVPacket的大小是公共的ABI(Public ABI)一部分，这样的结构体在FFmpeg很少，由此也可见AVPacket的重要性，它可以被分配在栈空间上（可以使用语句AVPacket pkt;在栈空间定义一个Packet），
    3.AVPacket中的内存管理

    AVPacket实际上可看作一个容器，它本身并不包含压缩的流媒体数据，而是通过data指针引用数据的缓存空间。所以将Packet作为参数传递的时候，就要根据具体的需求，对data引用的这部分数据缓存空间进行特殊的处理。当从一个Packet去创建另一个Packet的时候，有两种情况：

    1）两个Packet的data引用的是同一数据缓存空间，这个时候要注意数据缓存空间的释放问题和修改问题（相当于iOS的retain）

    2）两个Packet的data引用不同的数据缓存空间，每个Packet都有数据缓存空间的copy

    AVPacket中的AVBufferRef *buf;就是用来管理这个引用计数的，AVBufferRef有两个函数：av_packet_ref() 和av_packet_unref()增加和减少引用计数的，AVBufferRef的声明如下：
     操作AVPacket的函数大约有30个，主要分为：AVPacket的创建初始化，AVPacket中的data数据管理（clone，free,copy），AVPacket中的side_data数据管理。

        void av_init_packet(AVPacket *pkt);

              初始化packet的值为默认值，该函数不会影响data引用的数据缓存空间和size，需要单独处理。

        int av_new_packet(AVPacket *pkt, int size);

                av_init_packet的增强版，不但会初始化字段，还为data分配了存储空间

        AVPacket *av_packet_alloc(void);

                  创建一个AVPacket，将其字段设为默认值（data为空，没有数据缓存空间）。

        void av_packet_free(AVPacket **pkt);

                   释放使用av_packet_alloc创建的AVPacket，如果该Packet有引用计数（packet->buf不为空），则先调用av_packet_unref。

        AVPacket *av_packet_clone(const AVPacket *src);

                  其功能是av_packet_alloc和av_packet_ref

        int av_copy_packet(AVPacket *dst, const AVPacket *src);

                 复制一个新的packet，包括数据缓存

        int av_copy_packet_side_data(AVPacket *dst, const AVPacket *src);

                初始化一个引用计数的packet，并指定了其数据缓存

        int av_grow_packet(AVPacket *pkt, int grow_by);

                    增大Packet->data指向的数据缓存

        void av_shrink_packet(AVPacket *pkt, int size);

                减小Packet->data指向的数据缓存

        3.1 废弃函数介绍 ------> av_dup_packet和av_free_packet

        int av_dup_packet(AVPacket *pkt);

                复制src->data引用的数据缓存，赋值给dst。也就是创建两个独立packet，这个功能现在可用使用函数av_packet_ref来代替

        void av_free_packet(AVPacket *pkt);

                释放packet，包括其data引用的数据缓存，现在可以使用av_packet_unref代替

        3.2 函数对比 --------->av_free_packet和av_packet_free

        void av_free_packet(AVPacket *pkt);

                    只是清空里边的数据内容，内存地址仍然在。我的版本是3.3已经废弃，所以用av_packet_unref替代。

            如果不清空会发生什么情况呢，举个简单的例子，一个char数组大小为128，里面有100个自己的内容。第二次使用你没有清空第一次的内容，第二次的数据大小为60，那么第一次的最后40个字节的数据仍会保留，造成数据冗余，极大可能对你的处理造成影响（这个跟自己的处理有关系，并不一定）。

        void av_packet_free(AVPacket **pkt);

                    类似于free(p); p = Null;不仅清空内容还清空内存（一般就是如果用了av_packet_alloc后就要调用av_packet_free来释放。但如果有引用计数，在调用av_packet_free前一般先调用av_packet_unref）


    */
    AVPacket packet;
    // initialize SWS context for software scaling
    // struct SwsContext *sws_getContext(
    //         int srcW, /* 输入图像的宽度 */
    //         int srcH, /* 输入图像的宽度 */
    //         enum AVPixelFormat srcFormat, /* 输入图像的像素格式 */
    //         int dstW, /* 输出图像的宽度 */
    //         int dstH, /* 输出图像的高度 */
    //         enum AVPixelFormat dstFormat,  输出图像的像素格式 
    //         int flags,/* 选择缩放算法(只有当输入输出图像大小不同时有效),一般选择SWS_FAST_BILINEAR */
    //         SwsFilter *srcFilter, /* 输入图像的滤波器信息, 若不需要传NULL */
    //         SwsFilter *dstFilter, /* 输出图像的滤波器信息, 若不需要传NULL */
    //         const double *param /* 特定缩放算法需要的参数(?)，默认为NULL */
    //         );

    sws_ctx = sws_getContext(pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
        );

    
    i=0;
    // av_read_frame从字面意思上来看，就是从内存中读取一帧数据，
    /*
    av_read_frame::::
    Return the next frame of a stream. The information is stored as a packet in pkt.
    The returned packet is valid until the next av_read_frame() or until av_close_input_file() 
    and must be freed with av_free_packet. For video, the packet contains exactly one frame. 
    For audio, it contains an integer number of frames if each frame has a known fixed size 
    (e.g. PCM or ADPCM data). If the audio frames have a variable size (e.g. MPEG audio), then it contains one frame.
    pkt->pts, pkt->dts and pkt->duration are always set to correct values in AVStream.timebase units 
    (and guessed if the format cannot provided them). pkt->pts can be AV_NOPTS_VALUE if the video format 
    has B frames, so it is better to rely on pkt->dts if you do not decompress the payload.
    Returns: 0 if OK, < 0 if error or end of file.
    */


    /*
    The process, again, is simple: av_read_frame() reads in a packet and stores it in the AVPacket struct. 
    Note that we've only allocated the packet structure - ffmpeg allocates the internal data for us, 
    which is pointed to by packet.data. This is freed by the av_free_packet() later. avcodec_decode_video() 
    converts the packet to a frame for us. However, we might not have all the information we need for a frame after 
    decoding a packet, so avcodec_decode_video() sets frameFinished for us when we have the next frame. Finally, 
    we use sws_scale() to convert from the native format (pCodecCtx->pix_fmt) to RGB. Remember that you can cast an 
    AVFrame pointer to an AVPicture pointer. Finally, we pass the frame and height and width information to our SaveFrame function.
    */
    while(av_read_frame(pFormatCtx, &packet)>=0) {
          // Is this a packet from the video stream?
          if(packet.stream_index==videoStream) {
            // Decode video frame
            /*
            int avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture, int *frameFinished, const AVPacket *avpkt)
            Decodes a video frame from buf into picture. The avcodec_decode_video2() function decodes a frame of video from the input buffer buf of size buf_size. To decode it, it makes use of the videocodec which was coupled with avctx using avcodec_open2(). The resulting decoded frame is stored in picture.
            Warning: The sample alignment and buffer problems that apply to avcodec_decode_audio4 apply to this function as well.
            avctx: The codec context.
            picture: The AVFrame in which the decoded video will be stored.
            frameFinished: Zero if no frame could be decompressed, otherwise it is non-zero. avpkt: The input AVPacket containing the input buffer. You can create such packet with av_init_packet() and by then setting data and size, some decoders might in addition need other fields like flags&AV_PKT_FLAG_KEY. All decoders are designed to use the least fields possible.
            */
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
                // Did we get a video frame?
                if(frameFinished) {
                // Convert the image from its native format to RGB
                    // FFmpeg中的 sws_scale() 函数主要是用来做视频像素格式和分辨率的转换，其优势在于：可以在同一个函数里实现：
                    // 1.图像色彩空间转换， 2:分辨率缩放，3:前后图像滤波处理。不足之处在于：效率相对较低，
                    // 不如libyuv或shader，其关联的函数主要有：
                    /*
                    真正用来做转换的函数则是： sws_scale() ，其函数定义如下：

                        int sws_scale(struct SwsContext *c, const uint8_t *const srcSlice[],
                                      const int srcStride[], int srcSliceY, int srcSliceH,
                                      uint8_t *const dst[], const int dstStride[]);
                        下面对其函数参数进行详细说明：

                        1.参数 SwsContext *c， 转换格式的上下文。也就是 sws_getContext 函数返回的结果。
                        2.参数 const uint8_t *const srcSlice[], 输入图像的每个颜色通道的数据指针。其实就是解码后的AVFrame中的data[]数组。因为不同像素的存储格式不同，所以srcSlice[]维数
                        也有可能不同。
                        以YUV420P为例，它是planar格式，它的内存中的排布如下：
                        YYYYYYYY UUUU VVVV
                        使用FFmpeg解码后存储在AVFrame的data[]数组中时：
                        data[0]——-Y分量, Y1, Y2, Y3, Y4, Y5, Y6, Y7, Y8……
                        data[1]——-U分量, U1, U2, U3, U4……
                        data[2]——-V分量, V1, V2, V3, V4……
                        linesize[]数组中保存的是对应通道的数据宽度 ，
                        linesize[0]——-Y分量的宽度
                        linesize[1]——-U分量的宽度
                        linesize[2]——-V分量的宽度

                        而RGB24，它是packed格式，它在data[]数组中则只有一维，它在存储方式如下：
                        data[0]: R1, G1, B1, R2, G2, B2, R3, G3, B3, R4, G4, B4……
                        这里要特别注意，linesize[0]的值并不一定等于图片的宽度，有时候为了对齐各解码器的CPU，实际尺寸会大于图片的宽度，这点在我们编程时（比如OpengGL硬件转换/渲染）要特别注意，否则解码出来的图像会异常。

                        3.参数const int srcStride[]，输入图像的每个颜色通道的跨度。.也就是每个通道的行字节数，对应的是解码后的AVFrame中的linesize[]数组。根据它可以确立下一行的起始位置，不过stride和width不一定相同，这是因为：
                        a.由于数据帧存储的对齐，有可能会向每行后面增加一些填充字节这样 stride = width + N；
                        b.packet色彩空间下，每个像素几个通道数据混合在一起，例如RGB24，每个像素3字节连续存放，因此下一行的位置需要跳过3*width字节。

                        4.参数int srcSliceY, int srcSliceH,定义在输入图像上处理区域，srcSliceY是起始位置，srcSliceH是处理多少行。如果srcSliceY=0，srcSliceH=height，表示一次性处理完整个图像。这种设置是为了多线程并行，例如可以创建两个线程，第一个线程处理 [0, h/2-1]行，第二个线程处理 [h/2, h-1]行。并行处理加快速度。
                        5.参数uint8_t *const dst[], const int dstStride[]定义输出图像信息（输出的每个颜色通道数据指针，每个颜色通道行字节数）
                    */
                    sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
                      pFrame->linesize, 0, pCodecCtx->height,
                      pFrameRGB->data, pFrameRGB->linesize);
                
                    // Save the frame to disk
                    if(++i<=5){
                        printf("----> %d\n", i);
                        SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
                    }
                        
                }
          }
    
        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }
    // Free the RGB image
    av_free(buffer);
    av_free(pFrameRGB);

    // Free the YUV frame
    av_free(pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);

    // Close the video file
    avformat_close_input(&pFormatCtx);
    
    printf("hello av\n");
    return 0;
}







