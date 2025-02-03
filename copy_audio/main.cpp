#include <QCoreApplication>
#include <QDebug>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <opencv2/opencv.hpp>  // For reading images, optional if not using OpenCV
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    QCoreApplication app (argc, argv);
    if (argc < 4)
    {
        qDebug() << "usage: " << argv[0] << "<input video without audio stream> <input video with audio> <output video with audio>\n";
        return 1;
    }

    const char *in_filename_target = argv[1];
    const char *in_filename_source = argv[2];
    const char *out_filename = argv[3];
    int ret, i;
    AVDictionary *opts = NULL;

    AVFormatContext *input_format_context_target = NULL, *input_format_context_source = NULL, *output_format_context = NULL;

    if ((ret = avformat_open_input(&input_format_context_target, in_filename_target, NULL, NULL)) < 0)
    {
        qDebug() << "Could not open input file " << in_filename_target;
        return 0;
    }
    if ((ret = avformat_find_stream_info(input_format_context_target, NULL)) < 0)
    {
        qDebug() << "Failed to retrieve input stream information";
        return 0;
    }

    if ((ret = avformat_open_input(&input_format_context_source, in_filename_source, NULL, NULL)) < 0)
    {
        qDebug() << "Could not open input file " << in_filename_source;
        return 0;
    }
    if ((ret = avformat_find_stream_info(input_format_context_source, NULL)) < 0)
    {
        qDebug() << "Failed to retrieve input stream information";
        return 0;
    }

    avformat_alloc_output_context2(&output_format_context, NULL, NULL, out_filename);
    if (!output_format_context)
    {
        qDebug() << "Could not create output context";
        return 0;
    }

    int stream_index = 0;
    int *streams_list = NULL;
    int number_of_streams = 0;

    number_of_streams = input_format_context_source->nb_streams;
    streams_list = (int *)av_malloc_array(number_of_streams, sizeof(*streams_list));
    qDebug() << "number_of_streams" << number_of_streams;

    for (i = 0; i < input_format_context_target->nb_streams; i++)
    {
        AVStream *out_stream;
        AVStream *in_stream = input_format_context_target->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
        {
            continue;
        }
        streams_list[i] = stream_index++;
        out_stream = avformat_new_stream(output_format_context, NULL);
        if (!out_stream)
        {
            qDebug() << "Failed allocating output stream";
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0)
        {
            qDebug() << "Failed to copy codec parameters";
            goto end;
        }
    }

    for (i = 0; i < input_format_context_source->nb_streams; i++)
    {
        AVStream *out_stream;
        AVStream *in_stream = input_format_context_source->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        {
            continue;
        }
        streams_list[i] = stream_index++;
        out_stream = avformat_new_stream(output_format_context, NULL);
        if (!out_stream)
        {
            qDebug() << "Failed allocating output stream";
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0)
        {
            qDebug() << "Failed to copy codec parameters";
            goto end;
        }
    }

    av_dump_format(output_format_context, 0, out_filename, 1);

    if (!(output_format_context->oformat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&output_format_context->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    // https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga18b7b10bb5b94c4842de18166bc677cb
    ret = avformat_write_header(output_format_context, &opts);
    if (ret < 0)
    {
        qDebug() << "Error occurred when opening output file";
        goto end;
    }

    AVPacket packet;
    while (1)
    {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(input_format_context_target, &packet);
        if (ret < 0)
            break;
        in_stream = input_format_context_target->streams[packet.stream_index];
        if (packet.stream_index >= number_of_streams || streams_list[packet.stream_index] < 0)
        {
            av_packet_unref(&packet);
            continue;
        }
        packet.stream_index = streams_list[packet.stream_index];
        out_stream = output_format_context->streams[packet.stream_index];
        /* copy packet */
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        // https://ffmpeg.org/doxygen/trunk/structAVPacket.html#ab5793d8195cf4789dfb3913b7a693903
        packet.pos = -1;

        // https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga37352ed2c63493c38219d935e71db6c1
        ret = av_interleaved_write_frame(output_format_context, &packet);
        if (ret < 0)
        {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&packet);
    }

    while (1)
    {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(input_format_context_source, &packet);
        if (ret < 0)
            break;
        in_stream = input_format_context_source->streams[packet.stream_index];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        {
            continue;
        }
        if (packet.stream_index >= number_of_streams || streams_list[packet.stream_index] < 0)
        {
            av_packet_unref(&packet);
            continue;
        }
        packet.stream_index = streams_list[packet.stream_index];
        out_stream = output_format_context->streams[1];
        /* copy packet */
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        packet.pos = -1;

        ret = av_interleaved_write_frame(output_format_context, &packet);
        if (ret < 0)
        {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&packet);
    }

    av_write_trailer(output_format_context);

end:
    avformat_close_input(&input_format_context_target);
    avformat_close_input(&input_format_context_source);
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);
    av_freep(&streams_list);
    if (ret < 0 && ret != AVERROR_EOF)
    {
        qDebug() << "Error occurred: " << av_err2str(ret);
        return 1;
    }

    return 0;
}