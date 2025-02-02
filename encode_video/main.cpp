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

void encode_images_to_video(const std::string& folder_path, const char* output_file, int width, int height, int fps) {

    // Create the output format context
    AVFormatContext* format_ctx = nullptr;
    avformat_alloc_output_context2(&format_ctx, nullptr, nullptr, output_file);
    if (!format_ctx) {
        qDebug() << "Could not allocate format context";
        return;
    }

    // Find the codec
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        qDebug() << "H.264 codec not found";
        return;
    }

    // Create the video stream
    AVStream* stream = avformat_new_stream(format_ctx, codec);
    if (!stream) {
        qDebug() << "Could not create new stream";
        return;
    }
    stream->id = format_ctx->nb_streams - 1;

    // Configure the codec context
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        qDebug() << "Could not allocate codec context";
        return;
    }
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        qDebug() << "Could not allocate packet";
        return;
    }
    int frame_duration = 1001;
    codec_ctx->codec_id = codec->id;
    // codec_ctx->bit_rate = 400000;
    // codec_ctx->bit_rate = 16210421;
    codec_ctx->width = width;
    codec_ctx->height = height;
    codec_ctx->time_base = (AVRational){1, fps * frame_duration};
    codec_ctx->framerate = (AVRational){fps, 1};
    // codec_ctx->gop_size = 10; // Group of pictures size
    // codec_ctx->max_b_frames = 1;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        qDebug() << "Could not open codec";
        return;
    }
    stream->time_base = codec_ctx->time_base;

    if (avcodec_parameters_from_context(stream->codecpar, codec_ctx) < 0) {
        qDebug() << "Could not copy codec parameters to stream";
        return;
    }

    // Open output file
    if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&format_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0) {
            qDebug() << "Could not open output file";
            return;
        }
    }

    // Write the file header
    if (avformat_write_header(format_ctx, nullptr) < 0) {
        qDebug() << "Error writing header";
        return;
    }

    // Prepare the scaling context
    SwsContext* sws_ctx = sws_getContext(
        width, height, AV_PIX_FMT_BGR24,  // Input dimensions and format
        width, height, AV_PIX_FMT_YUV420P, // Output dimensions and format
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_ctx) {
        qDebug() << "Could not initialize the sws context";
        return;
    }

    // Allocate video frame
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    frame->time_base = codec_ctx->time_base;
    frame->duration = frame_duration;
    frame->key_frame = 0;

    if (av_frame_get_buffer(frame, 32) < 0) {
        qDebug() << "Could not allocate frame buffer";
        return;
    }

    // Read images from the folder and encode them
    int pts = 0;
    std::set<fs::path> sorted_by_name;
    for (auto &entry : fs::directory_iterator(folder_path)) {
        if (!entry.is_regular_file()) continue;
        sorted_by_name.insert(entry.path());
    }

    for (auto &filename : sorted_by_name) {
        // Load the image using OpenCV
        cv::Mat img = cv::imread(filename.c_str());
        if (img.empty()) {
            qDebug() << "Could not read image: " << filename.c_str();
            continue;
        }

        // Resize the image
        cv::resize(img, img, cv::Size(width, height));

        // Convert the image to YUV420P format
        uint8_t* in_data[1] = { img.data };
        int in_linesize[1] = { static_cast<int>(img.step) };
        // qDebug() << "img.step: " << img.step << "in_linesize: " << in_linesize[0] << "height: " << height << "frame linesize: " << frame->linesize[0];
        sws_scale(sws_ctx, in_data, in_linesize, 0, height, frame->data, frame->linesize);

        frame->pts = pts;
        int ret = avcodec_send_frame(codec_ctx, frame);
        if (ret < 0) {
            qDebug() << "Error sending frame";
            continue;
        }

        pts += frame_duration;

        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                continue;
            }
            else if (ret < 0) {
                qDebug() << "Error during encoding";
                return;
            }
            packet->stream_index = stream->index;
            av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
            av_interleaved_write_frame(format_ctx, packet);
            av_packet_unref(packet);
        }
    }

    // Flush the encoder
    avcodec_send_frame(codec_ctx, nullptr);
    while (avcodec_receive_packet(codec_ctx, packet) == 0) {
        packet->stream_index = stream->index;
        av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
        av_interleaved_write_frame(format_ctx, packet);
        av_packet_unref(packet);
    }

    // Write trailer and cleanup
    av_write_trailer(format_ctx);
    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&format_ctx->pb);
    }
    avformat_free_context(format_ctx);

    qDebug() << "Video encoding completed successfully!";
}

int main(int argc, char **argv)
{
    QCoreApplication app (argc, argv);
    if (argc < 3)
    {
        qDebug() << "usage: " << argv[0] << "<input image folder> <output video file>\n";
        return 1;
    }

    int width = 1280;
    int height = 720;
    int fps = 30;

    encode_images_to_video(argv[1], argv[2], width, height, fps);

    return 0;
}