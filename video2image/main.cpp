#include <QCoreApplication>
#include <QDebug>
extern "C"
{
    #include <libavutil/adler32.h>
    #include <libavutil/mem.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/timestamp.h>
    #include <libswscale/swscale.h>
}

static void ppm_save(uint8_t *buf[], int byte_size, int wrap, int width, int height, int iFrame, const char* output_filename)
{
    FILE *f;
    char filename[32];
    int  i;

    sprintf(filename, "%s-%d.ppm", output_filename, iFrame);
    // sprintf(filename, "%s-%d.raw", output_filename, iFrame);
    f = fopen(filename, "wb");
    if(f==NULL)
        return;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(buf[0], 1, byte_size, f);

    fclose(f);
}

static int video_decode_example(const char *input_filename, const char *output_filename)
{
    const AVCodec *codec = NULL;
    AVCodecContext *ctx= NULL;
    AVCodecParameters *origin_par = NULL;
    AVFrame *fr = NULL;
    uint8_t *byte_buffer = NULL;
    AVPacket *pkt;
    AVFormatContext *fmt_ctx = NULL;
    int number_of_written_bytes;
    int video_stream;
    int byte_buffer_size;
    int i = 0;
    int result;
    int num_bytes;
    uint8_t *buffer[4];
    int dst_linesize[4];

    result = avformat_open_input(&fmt_ctx, input_filename, NULL, NULL);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open file\n");
        return result;
    }

    result = avformat_find_stream_info(fmt_ctx, NULL);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't get stream info\n");
        return result;
    }

    video_stream = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream < 0) {
      av_log(NULL, AV_LOG_ERROR, "Can't find video stream in input file\n");
      return -1;
    }

    origin_par = fmt_ctx->streams[video_stream]->codecpar;

    codec = avcodec_find_decoder(origin_par->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        return -1;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        return AVERROR(ENOMEM);
    }

    result = avcodec_parameters_to_context(ctx, origin_par);
    if (result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        return result;
    }

    result = avcodec_open2(ctx, codec, NULL);
    if (result < 0) {
        av_log(ctx, AV_LOG_ERROR, "Can't open decoder\n");
        return result;
    }

    fr = av_frame_alloc();
    if (!fr) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
        return AVERROR(ENOMEM);
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        av_log(NULL, AV_LOG_ERROR, "Cannot allocate packet\n");
        return AVERROR(ENOMEM);
    }

    byte_buffer_size = av_image_get_buffer_size(ctx->pix_fmt, ctx->width, ctx->height, 16);
    byte_buffer = (uint8_t *)av_malloc(byte_buffer_size);
    if (!byte_buffer) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate buffer\n");
        return AVERROR(ENOMEM);
    }

    qDebug() << "#tb "<< video_stream << ":" << fmt_ctx->streams[video_stream]->time_base.num << "/" << fmt_ctx->streams[video_stream]->time_base.den;

    result = 0;
    while (result >= 0) {
        result = av_read_frame(fmt_ctx, pkt);
        if (result >= 0 && pkt->stream_index != video_stream) {
            av_packet_unref(pkt);
            continue;
        }

        if (result < 0)
            result = avcodec_send_packet(ctx, NULL);
        else {
            if (pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = pkt->dts = i;
            result = avcodec_send_packet(ctx, pkt);
        }
        av_packet_unref(pkt);

        if (result < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
            return result;
        }

        while (result >= 0) {
            result = avcodec_receive_frame(ctx, fr);
            if (result == AVERROR_EOF)
                goto finish;
            else if (result == AVERROR(EAGAIN)) {
                result = 0;
                break;
            } else if (result < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
                return result;
            }

            number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                    (const uint8_t* const *)fr->data, (const int*) fr->linesize,
                                    ctx->pix_fmt, ctx->width, ctx->height, 1);
            if (number_of_written_bytes < 0) {
                av_log(NULL, AV_LOG_ERROR, "Can't copy image to buffer\n");
                av_frame_unref(fr);
                return number_of_written_bytes;
            }
            qDebug() << video_stream << ctx->frame_num << av_ts2str(fr->pts) << av_ts2str(fr->pkt_dts) << number_of_written_bytes << av_adler32_update(0, (const uint8_t*)byte_buffer, number_of_written_bytes) << fr->format;

            num_bytes = av_image_alloc(buffer, dst_linesize, ctx->width, ctx->height, AV_PIX_FMT_RGB24, 1);
            if (num_bytes < 0) {
                av_log(NULL, AV_LOG_ERROR, "Can't allocate buffer\n");
                return AVERROR(ENOMEM);
            }

            static struct SwsContext *img_convert_ctx;
            if(img_convert_ctx == NULL) {
                int w = ctx->width;
                int h = ctx->height;
                
                img_convert_ctx = sws_getContext(w, h, 
                                ctx->pix_fmt, 
                                w, h, AV_PIX_FMT_RGB24, SWS_BICUBIC,
                                NULL, NULL, NULL);
                if(img_convert_ctx == NULL) {
                    fprintf(stderr, "Cannot initialize the conversion context!\n");
                    exit(1);
                }
            }
            int ret = sws_scale(img_convert_ctx, (const uint8_t * const*)fr->data, fr->linesize, 0, 
                        ctx->height, buffer, dst_linesize);
            
            qDebug() << "[frame] width: " << ctx->width << "height: " << ctx->height << "linesize: " << fr->linesize[0];
            qDebug() << "[image] num_bytes: " << num_bytes << "linesize: " << dst_linesize;
            ppm_save(buffer, num_bytes, dst_linesize[0], ctx->width, ctx->height, ctx->frame_num, output_filename);
            av_frame_unref(fr);
            av_freep(&buffer);
        }
    }

finish:
    av_packet_free(&pkt);
    av_frame_free(&fr);
    avformat_close_input(&fmt_ctx);
    avcodec_free_context(&ctx);
    av_freep(&byte_buffer);
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app (argc, argv);
    if (argc < 3)
    {
        av_log(NULL, AV_LOG_ERROR, "Incorrect input\n");
        return 1;
    }

    if (video_decode_example(argv[1], argv[2]) != 0)
        return 1;

    return 0;
}
