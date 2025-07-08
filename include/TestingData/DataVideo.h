#ifndef DATA_VIDEO_H
#define DATA_VIDEO_H

#include "Data.h"
#include <libavutil/opt.h>
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

class DataVideo;

class VideoFrameBuffer {
private:
    DataVideo* parent;
    size_t frame_index;
    bool modified;
    std::vector<std::vector<uint8_t>> frame_data;
    size_t width;
    size_t height;

public:
    VideoFrameBuffer() = delete;
    VideoFrameBuffer(DataVideo* p, size_t idx, size_t w, size_t h);
    VideoFrameBuffer(const VideoFrameBuffer&) = default;
    VideoFrameBuffer(VideoFrameBuffer&&) = default;
    ~VideoFrameBuffer() {
        commit();
    }

    uint8_t& at(size_t row, size_t col, size_t channel) {
        modified = true;
        return frame_data[row][col * 3 + channel];
    }

    const uint8_t& c_at(size_t row, size_t col, size_t channel) const {
        return frame_data[row][col * 3 + channel];
    }

    void mark_unmodified() { modified = false; }

    void commit();
};

class AudioFrameBuffer {
private:
    DataVideo* parent;
    size_t frame_index;
    bool modified;
    std::vector<float> audio_data;
    size_t sample_count;
    int channel_count;

public:
    AudioFrameBuffer() = delete;
    AudioFrameBuffer(DataVideo* p, size_t idx, size_t samples, int channels);
    AudioFrameBuffer(const AudioFrameBuffer&) = default;
    AudioFrameBuffer(AudioFrameBuffer&&) = default;
    ~AudioFrameBuffer() {
        commit();
    }

    float& at(size_t sample, int channel) {
        modified = true;
        return audio_data[sample * channel_count + channel];
    }

    const float& at(size_t sample, int channel) const {
        return audio_data[sample * channel_count + channel];
    }

    void mark_unmodified() { modified = false; }

    void commit();
};

using VideoFrame = std::function<VideoFrameBuffer(size_t)>;
using AudioFrame = std::function<AudioFrameBuffer(size_t)>;
using MetadataVideo = std::tuple<
    VideoFrame,
    AudioFrame,
    size_t,     // Video frame count
    size_t,     // Video width
    size_t,     // Video height
    size_t,     // Audio frame count
    int,        // Sample rate
    int         // Channel count
>;

class DataVideo : public Data<MetadataVideo> {
private:
    struct StreamInfo {
        int stream_index;
        AVCodecParameters* codecpar;
        AVStream* stream;
    };

    std::string filename;
    std::string copy_filename;
    size_t width;
    size_t height;
    int sample_rate;
    int channels;
    size_t video_frame_count;
    size_t audio_frame_count;
    
    std::vector<int64_t> video_positions;
    std::vector<int64_t> audio_positions;
    std::vector<size_t> audio_sample_counts;

    void init_ffmpeg() const {
        av_log_set_level(AV_LOG_ERROR);
    }

    void create_temp_copy() {

        copy_filename = std::filesystem::temp_directory_path() / 
                      ("video_copy_" + std::to_string(std::time(nullptr)) + 
                      "_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + 
                      std::filesystem::path(filename).extension().string());

        try {
            std::filesystem::copy_file(filename, copy_filename, 
                                     std::filesystem::copy_options::overwrite_existing);
            
        } catch (const std::exception& e) {
            copy_filename.clear();
            throw std::runtime_error("Failed to create video copy: " + std::string(e.what()));
        }
    }

    void load_metadata() {
        AVFormatContext* fmt_ctx = nullptr;
        init_ffmpeg();

        if(avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, nullptr) != 0) {
            throw std::runtime_error("Failed to open video file: " + filename);
        }

        if(avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            avformat_close_input(&fmt_ctx);
            throw std::runtime_error("Failed to find stream info in: " + filename);
        }

        StreamInfo video_info = {-1, nullptr, nullptr};
        StreamInfo audio_info = {-1, nullptr, nullptr};

        for(unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
            AVCodecParameters* codecpar = fmt_ctx->streams[i]->codecpar;
            if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_info.stream_index == -1) {
                video_info = {static_cast<int>(i), codecpar, fmt_ctx->streams[i]};
            }
            else if(codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_info.stream_index == -1) {
                audio_info = {static_cast<int>(i), codecpar, fmt_ctx->streams[i]};
            }
        }

        if(video_info.stream_index == -1 && audio_info.stream_index == -1) {
            avformat_close_input(&fmt_ctx);
            throw std::runtime_error("No video or audio streams found in: " + filename);
        }

        if(video_info.stream_index != -1) {
            width = video_info.codecpar->width;
            height = video_info.codecpar->height;
            video_frame_count = video_info.stream->nb_frames;
            if(video_frame_count == 0) {
                double fps = av_q2d(video_info.stream->avg_frame_rate);
                video_frame_count = static_cast<size_t>(fmt_ctx->duration * fps / AV_TIME_BASE);
            }
        }

        if(audio_info.stream_index != -1) {
            sample_rate = audio_info.codecpar->sample_rate;
            channels = audio_info.codecpar->ch_layout.nb_channels;
            if(audio_info.stream->duration > 0) {
                double duration = audio_info.stream->duration * av_q2d(audio_info.stream->time_base);
                audio_frame_count = static_cast<size_t>(duration * sample_rate);
            }
        }

        avformat_close_input(&fmt_ctx);
    }

    void build_frame_index() {
        AVFormatContext* fmt_ctx = nullptr;
        AVPacket* pkt = av_packet_alloc();
        init_ffmpeg();

        if(avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, nullptr) != 0) {
            av_packet_free(&pkt);
            throw std::runtime_error("Failed to open file for indexing: " + filename);
        }

        if(avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            avformat_close_input(&fmt_ctx);
            av_packet_free(&pkt);
            throw std::runtime_error("Failed to find stream info for indexing");
        }

        while(av_read_frame(fmt_ctx, pkt) >= 0) {
            if(pkt->stream_index == 0) {
                video_positions.push_back(pkt->pos);
            }
            else if(pkt->stream_index == 1) {
                audio_positions.push_back(pkt->pos);
                audio_sample_counts.push_back(
                    pkt->duration > 0 ? pkt->duration : pkt->size / (2 * channels)
                );
            }
            av_packet_unref(pkt);
        }

        video_frame_count = video_positions.size();
        audio_frame_count = audio_positions.size();

        av_packet_free(&pkt);
        avformat_close_input(&fmt_ctx);
    }

    VideoFrameBuffer load_video_frame_impl(size_t index, const std::string& source_file) const {
        if (index >= video_frame_count) {
            throw std::out_of_range("Invalid video frame index");
        }
    
        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        SwsContext* sws_ctx = nullptr;
        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        VideoFrameBuffer result(const_cast<DataVideo*>(this), index, width, height);
        std::vector<uint8_t> rgb_buffer(width * height * 3);
    
        try {
            // 1. Открываем файл
            if (avformat_open_input(&fmt_ctx, source_file.c_str(), nullptr, nullptr) != 0) {
                throw std::runtime_error("Cannot open input file");
            }
    
            // 2. Получаем информацию о потоках
            if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
                throw std::runtime_error("Cannot find stream information");
            }
    
            // 3. Находим видео поток
            int video_stream_idx = -1;
            for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
                if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    video_stream_idx = i;
                    break;
                }
            }
    
            if (video_stream_idx == -1) {
                throw std::runtime_error("No video stream found");
            }
    
            AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
            const AVCodec* codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
            if (!codec) {
                throw std::runtime_error("Unsupported codec");
            }
    
            // 4. Настраиваем контекст кодера
            codec_ctx = avcodec_alloc_context3(codec);
            if (!codec_ctx) {
                throw std::runtime_error("Cannot allocate codec context");
            }
    
            if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
                throw std::runtime_error("Cannot copy codec parameters");
            }
    
            if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
                throw std::runtime_error("Cannot open codec");
            }
    
            // 5. Новый подход: последовательное чтение с начала
            av_seek_frame(fmt_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec_ctx);
    
            bool frame_decoded = false;
            int current_frame = 0;
            const int target_frame = static_cast<int>(index);
            const int max_attempts = target_frame + 100; // Запас на случай проблем
    
            while (!frame_decoded && current_frame < max_attempts && av_read_frame(fmt_ctx, pkt) >= 0) {
                if (pkt->stream_index == video_stream_idx) {
                    if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                        while (!frame_decoded) {
                            int ret = avcodec_receive_frame(codec_ctx, frame);
                            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                                break;
                            } else if (ret < 0) {
                                throw std::runtime_error("Error during decoding");
                            }
    
                            if (current_frame == target_frame) {
                                // Конвертируем в RGB
                                sws_ctx = sws_getContext(
                                    frame->width, frame->height, codec_ctx->pix_fmt,
                                    width, height, AV_PIX_FMT_RGB24,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                                
                                if (!sws_ctx) {
                                    throw std::runtime_error("Cannot create sws context");
                                }
    
                                uint8_t* dest[1] = {rgb_buffer.data()};
                                int dest_linesize[1] = {static_cast<int>(width * 3)};
                                sws_scale(sws_ctx, frame->data, frame->linesize, 0, 
                                         frame->height, dest, dest_linesize);
    
                                // Копируем в буфер результата
                                for (size_t y = 0; y < height; ++y) {
                                    for (size_t x = 0; x < width; ++x) {
                                        size_t idx = (y * width + x) * 3;
                                        result.at(y, x, 0) = rgb_buffer[idx];
                                        result.at(y, x, 1) = rgb_buffer[idx+1];
                                        result.at(y, x, 2) = rgb_buffer[idx+2];
                                    }
                                }
    
                                frame_decoded = true;
                            }
                            av_frame_unref(frame);
                            current_frame++;
                        }
                    }
                }
                av_packet_unref(pkt);
            }
    
            if (!frame_decoded) {
                throw std::runtime_error("Cannot decode frame " + std::to_string(index) + 
                                       " (reached frame " + std::to_string(current_frame) + ")");
            }
        }
        catch (...) {
            if (sws_ctx) sws_freeContext(sws_ctx);
            if (frame) av_frame_free(&frame);
            if (pkt) av_packet_free(&pkt);
            if (codec_ctx) avcodec_free_context(&codec_ctx);
            if (fmt_ctx) avformat_close_input(&fmt_ctx);
            throw;
        }
    
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (fmt_ctx) avformat_close_input(&fmt_ctx);
    
        return result;
    }

    AudioFrameBuffer load_audio_frame_impl(size_t index, const std::string& source_file) const {
        if(index >= audio_frame_count) {
            throw std::out_of_range("Invalid audio frame index");
        }

        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        SwrContext* swr_ctx = nullptr;
        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVChannelLayout in_layout, out_layout;
        AudioFrameBuffer result(const_cast<DataVideo*>(this), index, audio_sample_counts[index], channels);

        try {
            if(avformat_open_input(&fmt_ctx, source_file.c_str(), nullptr, nullptr) != 0) {
                throw std::runtime_error("Failed to open audio file for frame loading");
            }

            const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
            if(!codec) {
                throw std::runtime_error("Unsupported audio codec");
            }

            codec_ctx = avcodec_alloc_context3(codec);
            if(!codec_ctx) {
                throw std::runtime_error("Failed to allocate audio codec context");
            }

            if(avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[1]->codecpar) < 0) {
                throw std::runtime_error("Failed to copy audio codec parameters");
            }

            if(avcodec_open2(codec_ctx, codec, nullptr) < 0) {
                throw std::runtime_error("Failed to open audio codec");
            }

            av_channel_layout_copy(&in_layout, &codec_ctx->ch_layout);
            av_channel_layout_default(&out_layout, channels);

            if(swr_alloc_set_opts2(&swr_ctx,
                                 &out_layout, AV_SAMPLE_FMT_FLT, sample_rate,
                                 &in_layout, codec_ctx->sample_fmt, sample_rate,
                                 0, nullptr) < 0 || !swr_ctx) {
                throw std::runtime_error("Failed to create swr context");
            }

            if(swr_init(swr_ctx) < 0) {
                throw std::runtime_error("Failed to initialize swr context");
            }

            if(av_seek_frame(fmt_ctx, 1, audio_positions[index], AVSEEK_FLAG_BYTE) < 0) {
                throw std::runtime_error("Failed to seek to audio frame");
            }

            bool frame_decoded = false;
            while(!frame_decoded && av_read_frame(fmt_ctx, pkt) >= 0) {
                if(pkt->stream_index == 1) {
                    if(avcodec_send_packet(codec_ctx, pkt) == 0) {
                        while(avcodec_receive_frame(codec_ctx, frame) == 0) {
                            uint8_t* out_data[1] = {reinterpret_cast<uint8_t*>(&result.at(0, 0))};
                            if(swr_convert(swr_ctx, out_data, result.at(0, 0),
                                          (const uint8_t**)frame->data, frame->nb_samples) < 0) {
                                throw std::runtime_error("Failed to convert audio samples");
                            }

                            frame_decoded = true;
                            break;
                        }
                    }
                }
                av_packet_unref(pkt);
            }

            if(!frame_decoded) {
                throw std::runtime_error("Failed to decode audio frame");
            }
        }
        catch(...) {
            if(swr_ctx) swr_free(&swr_ctx);
            av_channel_layout_uninit(&in_layout);
            av_channel_layout_uninit(&out_layout);
            if(frame) av_frame_free(&frame);
            if(pkt) av_packet_free(&pkt);
            if(codec_ctx) avcodec_free_context(&codec_ctx);
            if(fmt_ctx) avformat_close_input(&fmt_ctx);
            throw;
        }

        if(swr_ctx) swr_free(&swr_ctx);
        av_channel_layout_uninit(&in_layout);
        av_channel_layout_uninit(&out_layout);
        if(frame) av_frame_free(&frame);
        if(pkt) av_packet_free(&pkt);
        if(codec_ctx) avcodec_free_context(&codec_ctx);
        if(fmt_ctx) avformat_close_input(&fmt_ctx);

        return result;
    }

public:
    

    DataVideo(const std::string& filename) : 
        filename(filename),
        width(0), height(0),
        sample_rate(0), channels(0),
        video_frame_count(0), audio_frame_count(0) {
        init_ffmpeg();
    }
    
    ~DataVideo() override {
        clear();
        clear_copy();
    }
    
    void save(bool saveCopy, int args_id, int thread_num, const std::string& filename) const override {}
    void load() override {}

    void read() override {
        if(!filename.empty()) {
            load_metadata();
            build_frame_index();
        }
    }

    VideoFrameBuffer read_video_frame(size_t index) const {
        if(index >= video_frame_count) {
            throw std::out_of_range("Invalid video frame index");
        }
        return load_video_frame_impl(index, filename);
    }

    AudioFrameBuffer read_audio_frame(size_t index) const {
        if(index >= audio_frame_count) {
            throw std::out_of_range("Invalid audio frame index");
        }
    
        return load_audio_frame_impl(index, filename);
    }

    void clear() override {
        video_positions.clear();
        audio_positions.clear();
        audio_sample_counts.clear();
        width = height = 0;
        sample_rate = channels = 0;
        video_frame_count = audio_frame_count = 0;
    }

    void clear_copy() override {
            try {
                if(std::filesystem::exists(copy_filename)) {
                    std::filesystem::remove(copy_filename);
                }
            } catch (...) {
                // Игнорируем ошибки удаления
            }
    }

    MetadataVideo& copy() override {
        clear_copy();
        create_temp_copy();
        
        auto video_loader = [this](size_t idx) { return read_video_frame(idx); };
        auto audio_loader = [this](size_t idx) { return read_audio_frame(idx); };
        
        _copy = std::make_tuple(
            video_loader,
            audio_loader,
            video_frame_count,
            width,
            height,
            audio_frame_count,
            sample_rate,
            channels
        );
        
        return _copy;
    }

    const std::string save_copy(const std::string& dir, int args_id, int thread_num = 0) const override {
        if(video_frame_count == 0) {
            throw std::runtime_error("No video frames available to save");
        }

        std::string output_name = "proc" + proc_data_str(args_id, thread_num) + "_" + 
                                std::filesystem::path(filename).filename().string();
        std::filesystem::path output_path = std::filesystem::path(dir) / output_name;
        
        std::filesystem::copy_file(filename, output_path, 
                                 std::filesystem::copy_options::overwrite_existing);
        
        return output_name;
    }

    const std::string title() const override {
        std::ostringstream ss;
        ss << "Видео размером " << width << " на " << height << ", "
           << video_frame_count << " кадров";
        
        if (audio_frame_count > 0) {
            ss << ", Аудио: " << sample_rate << " Гц, " << channels << " каналов";
        }
        
        return ss.str();
    }

    const std::string type() const override {
        return std::string("video");
    }

    void commit_frame(size_t index, const std::vector<std::vector<uint8_t>>& frame_data) {
        // === Проверка ===
        if (index >= video_frame_count) {
            throw std::out_of_range("Frame index out of range");
        }
        if (frame_data.empty() || frame_data.size() != height || frame_data[0].size() != width * 3) {
            throw std::invalid_argument("Invalid frame data dimensions");
        }
    
        // === Создание временного выходного файла ===
        std::string temp_output = "temp_output.mp4";
    
        AVFormatContext* input_fmt_ctx = nullptr;
        AVFormatContext* output_fmt_ctx = nullptr;
        AVCodecContext* dec_ctx = nullptr;
        AVCodecContext* enc_ctx = nullptr;
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        SwsContext* sws_ctx = nullptr;
    
        int ret = 0;
        int video_stream_index = -1;
        bool frame_replaced = false;
    
        try {
            // === Открываем входной файл ===
            if ((ret = avformat_open_input(&input_fmt_ctx, copy_filename.c_str(), nullptr, nullptr)) < 0) {
                throw std::runtime_error("Failed to open input file");
            }
            if ((ret = avformat_find_stream_info(input_fmt_ctx, nullptr)) < 0) {
                throw std::runtime_error("Failed to find stream info");
            }
    
            // === Создаём выходной формат ===
            avformat_alloc_output_context2(&output_fmt_ctx, nullptr, nullptr, temp_output.c_str());
            if (!output_fmt_ctx) {
                throw std::runtime_error("Failed to create output context");
            }
    
            // === Копируем потоки ===
            for (unsigned i = 0; i < input_fmt_ctx->nb_streams; i++) {
                AVStream* in_stream = input_fmt_ctx->streams[i];
                AVStream* out_stream = avformat_new_stream(output_fmt_ctx, nullptr);
                if (!out_stream) {
                    throw std::runtime_error("Failed to allocate output stream");
                }
    
                if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
                    throw std::runtime_error("Failed to copy codec parameters");
                }
    
                out_stream->codecpar->codec_tag = 0;
                out_stream->time_base = in_stream->time_base;
    
                if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    video_stream_index = i;
                }
            }
    
            if (video_stream_index == -1) {
                throw std::runtime_error("No video stream found");
            }
    
            // === Открываем файл для записи ===
            if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if ((ret = avio_open(&output_fmt_ctx->pb, temp_output.c_str(), AVIO_FLAG_WRITE)) < 0) {
                    throw std::runtime_error("Failed to open output file");
                }
            }
    
            // === Пишем заголовок ===
            if ((ret = avformat_write_header(output_fmt_ctx, nullptr)) < 0) {
                throw std::runtime_error("Failed to write header");
            }
    
            // === Инициализируем декодер ===
            const AVCodec* decoder = avcodec_find_decoder(input_fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
            dec_ctx = avcodec_alloc_context3(decoder);
            avcodec_parameters_to_context(dec_ctx, input_fmt_ctx->streams[video_stream_index]->codecpar);
            avcodec_open2(dec_ctx, decoder, nullptr);
    
            // === Инициализируем энкодер ===
            const AVCodec* encoder = avcodec_find_encoder(dec_ctx->codec_id);
            enc_ctx = avcodec_alloc_context3(encoder);
    
            enc_ctx->height = dec_ctx->height;
            enc_ctx->width = dec_ctx->width;
            enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
            enc_ctx->pix_fmt = encoder->pix_fmts ? encoder->pix_fmts[0] : dec_ctx->pix_fmt;
            enc_ctx->time_base = input_fmt_ctx->streams[video_stream_index]->time_base;
    
            if (output_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    
            avcodec_open2(enc_ctx, encoder, nullptr);
    
            // === Начинаем обработку кадров ===
            size_t current_frame_index = 0;
    
            while (av_read_frame(input_fmt_ctx, packet) >= 0) {
                if (packet->stream_index == video_stream_index) {
                    avcodec_send_packet(dec_ctx, packet);
                    while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
                        if (current_frame_index == index) {
                            // === Конвертируем RGB => YUV ===
                            AVFrame* rgb_frame = av_frame_alloc();
                            rgb_frame->format = AV_PIX_FMT_RGB24;
                            rgb_frame->width = width;
                            rgb_frame->height = height;
                            av_image_alloc(rgb_frame->data, rgb_frame->linesize, width, height, AV_PIX_FMT_RGB24, 1);
    
                            // Копируем данные
                            for (int y = 0; y < height; ++y) {
                                memcpy(rgb_frame->data[0] + y * rgb_frame->linesize[0],
                                       frame_data[y].data(), width * 3);
                            }
    
                            // Конвертация RGB -> YUV
                            sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                                                     width, height, enc_ctx->pix_fmt,
                                                     SWS_BICUBIC, nullptr, nullptr, nullptr);
    
                            AVFrame* yuv_frame = av_frame_alloc();
                            yuv_frame->format = enc_ctx->pix_fmt;
                            yuv_frame->width = width;
                            yuv_frame->height = height;
                            av_frame_get_buffer(yuv_frame, 32);
                            av_frame_make_writable(yuv_frame);
    
                            sws_scale(sws_ctx, rgb_frame->data, rgb_frame->linesize, 0, height,
                                      yuv_frame->data, yuv_frame->linesize);
                            yuv_frame->pts = frame->pts;
    
                            // Кодируем и записываем
                            avcodec_send_frame(enc_ctx, yuv_frame);
                            AVPacket enc_pkt;
                            av_init_packet(&enc_pkt);
                            enc_pkt.data = nullptr;
                            enc_pkt.size = 0;
    
                            while (avcodec_receive_packet(enc_ctx, &enc_pkt) >= 0) {
                                enc_pkt.stream_index = video_stream_index;
                                av_packet_rescale_ts(&enc_pkt, enc_ctx->time_base,
                                                     output_fmt_ctx->streams[video_stream_index]->time_base);
                                av_interleaved_write_frame(output_fmt_ctx, &enc_pkt);
                                av_packet_unref(&enc_pkt);
                            }
    
                            av_frame_free(&rgb_frame);
                            av_frame_free(&yuv_frame);
                            frame_replaced = true;
    
                        } else {
                            // Просто перекодируем оригинальный кадр
                            avcodec_send_frame(enc_ctx, frame);
                            AVPacket enc_pkt;
                            av_init_packet(&enc_pkt);
                            enc_pkt.data = nullptr;
                            enc_pkt.size = 0;
    
                            while (avcodec_receive_packet(enc_ctx, &enc_pkt) >= 0) {
                                enc_pkt.stream_index = video_stream_index;
                                av_packet_rescale_ts(&enc_pkt, enc_ctx->time_base,
                                                     output_fmt_ctx->streams[video_stream_index]->time_base);
                                av_interleaved_write_frame(output_fmt_ctx, &enc_pkt);
                                av_packet_unref(&enc_pkt);
                            }
                        }
    
                        current_frame_index++;
                        av_frame_unref(frame);
                    }
                } else {
                    // Копируем не-видео потоки без изменений
                    AVStream* in_stream = input_fmt_ctx->streams[packet->stream_index];
                    AVStream* out_stream = output_fmt_ctx->streams[packet->stream_index];
    
                    av_packet_rescale_ts(packet, in_stream->time_base, out_stream->time_base);
                    packet->pos = -1;
                    av_interleaved_write_frame(output_fmt_ctx, packet);
                }
    
                av_packet_unref(packet);
            }
    
            // Завершаем запись
            av_write_trailer(output_fmt_ctx);
    
            // === Закрытие ===
            avcodec_free_context(&dec_ctx);
            avcodec_free_context(&enc_ctx);
            avformat_close_input(&input_fmt_ctx);
    
            if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&output_fmt_ctx->pb);
            }
    
            avformat_free_context(output_fmt_ctx);
            av_frame_free(&frame);
            av_packet_free(&packet);
            if (sws_ctx) sws_freeContext(sws_ctx);
    
            // === Проверка, был ли заменён кадр ===
            if (!frame_replaced) {
                throw std::runtime_error("Target frame not found or replaced");
            }
    
            // === Заменяем оригинальный файл новым ===
            std::filesystem::rename(temp_output, copy_filename);
        }
        catch (...) {
            // Очистка ресурсов при ошибке
            if (input_fmt_ctx) avformat_close_input(&input_fmt_ctx);
            if (output_fmt_ctx) {
                if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                    avio_closep(&output_fmt_ctx->pb);
                }
                avformat_free_context(output_fmt_ctx);
            }
            if (dec_ctx) avcodec_free_context(&dec_ctx);
            if (enc_ctx) avcodec_free_context(&enc_ctx);
            if (frame) av_frame_free(&frame);
            if (packet) av_packet_free(&packet);
            if (sws_ctx) sws_freeContext(sws_ctx);
    
            // Удаляем временный файл, если он остался
            std::remove(temp_output.c_str());
    
            throw;  // Пробрасываем исключение дальше
        }
    }
    
    void commit_audio(size_t index, const std::vector<float>& audio_data, size_t sample_count) {
        if (index >= audio_frame_count) {
            throw std::out_of_range("Invalid audio frame index");
        }
    
        if (copy_filename.empty()) {
            create_temp_copy();
        }

        AVFormatContext* fmt_ctx = nullptr;
        AVFormatContext* out_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        SwrContext* swr_ctx = nullptr;
        AVFrame* av_frame = av_frame_alloc();
        AVPacket* pkt = av_packet_alloc();
    
        try {
            // Open input file
            if (avformat_open_input(&fmt_ctx, copy_filename.c_str(), nullptr, nullptr) != 0) {
                throw std::runtime_error("Failed to open input audio file");
            }
    
            if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
                throw std::runtime_error("Failed to find stream info");
            }
    
            // Find audio stream
            int audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (audio_stream_idx < 0) {
                throw std::runtime_error("Audio stream not found");
            }
    
            AVStream* in_stream = fmt_ctx->streams[audio_stream_idx];
            const AVCodec* codec = avcodec_find_decoder(in_stream->codecpar->codec_id);
            if (!codec) {
                throw std::runtime_error("Unsupported audio codec");
            }
    
            codec_ctx = avcodec_alloc_context3(codec);
            if (avcodec_parameters_to_context(codec_ctx, in_stream->codecpar) < 0) {
                throw std::runtime_error("Failed to copy codec parameters");
            }
    
            if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
                throw std::runtime_error("Failed to open codec");
            }
    
            // Create temporary output file
            std::string temp_output = copy_filename;
            
            // Prepare output context
            if (avformat_alloc_output_context2(&out_ctx, nullptr, nullptr, temp_output.c_str()) < 0) {
                throw std::runtime_error("Failed to create output context");
            }
    
            // Copy streams to output
            for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
                AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
                if (!out_stream) {
                    throw std::runtime_error("Failed to create output stream");
                }
                if (avcodec_parameters_copy(out_stream->codecpar, fmt_ctx->streams[i]->codecpar) < 0) {
                    throw std::runtime_error("Failed to copy codec parameters");
                }
                out_stream->time_base = fmt_ctx->streams[i]->time_base;
            }
    
            if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&out_ctx->pb, temp_output.c_str(), AVIO_FLAG_WRITE) < 0) {
                    throw std::runtime_error("Failed to open output file");
                }
            }
    
            if (avformat_write_header(out_ctx, nullptr) < 0) {
                throw std::runtime_error("Failed to write header");
            }
    
            // Prepare frame for encoding
            av_frame->nb_samples = static_cast<int>(sample_count);
            av_frame->sample_rate = codec_ctx->sample_rate;
            av_frame->format = codec_ctx->sample_fmt;
            av_frame->pts = in_stream->time_base.den * index / in_stream->time_base.num;
            av_channel_layout_copy(&av_frame->ch_layout, &codec_ctx->ch_layout);
    
            if (av_frame_get_buffer(av_frame, 0) < 0) {
                throw std::runtime_error("Failed to allocate frame");
            }
    
            // Convert float to codec format
            swr_ctx = swr_alloc();
            av_opt_set_chlayout(swr_ctx, "in_chlayout", &codec_ctx->ch_layout, 0);
            av_opt_set_chlayout(swr_ctx, "out_chlayout", &codec_ctx->ch_layout, 0);
            av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
            av_opt_set_int(swr_ctx, "out_sample_rate", codec_ctx->sample_rate, 0);
            av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
            av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", codec_ctx->sample_fmt, 0);
    
            if (swr_init(swr_ctx) < 0) {
                throw std::runtime_error("Failed to initialize swresample");
            }
    
            const uint8_t* src_data[1] = {reinterpret_cast<const uint8_t*>(audio_data.data())};
            if (swr_convert(swr_ctx, av_frame->data, av_frame->nb_samples,
                           src_data, av_frame->nb_samples) < 0) {
                throw std::runtime_error("Failed to convert audio samples");
            }
    
            // Write frame
            if (avcodec_send_frame(codec_ctx, av_frame) == 0) {
                while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                    av_packet_rescale_ts(pkt, codec_ctx->time_base, out_ctx->streams[audio_stream_idx]->time_base);
                    pkt->stream_index = audio_stream_idx;
                    if (av_interleaved_write_frame(out_ctx, pkt) < 0) {
                        throw std::runtime_error("Failed to write audio frame");
                    }
                    av_packet_unref(pkt);
                }
            }
    
            // Copy other frames
            AVPacket* in_pkt = av_packet_alloc();
            av_seek_frame(fmt_ctx, -1, 0, AVSEEK_FLAG_BYTE);
            
            while (av_read_frame(fmt_ctx, in_pkt) >= 0) {
                if (in_pkt->stream_index != audio_stream_idx || 
                    std::find(audio_positions.begin(), audio_positions.end(), in_pkt->pos) == audio_positions.end() ||
                    in_pkt->pos == audio_positions[index]) {
                    av_packet_rescale_ts(in_pkt, fmt_ctx->streams[in_pkt->stream_index]->time_base,
                                       out_ctx->streams[in_pkt->stream_index]->time_base);
                    if (av_interleaved_write_frame(out_ctx, in_pkt) < 0) {
                        throw std::runtime_error("Failed to write frame");
                    }
                }
                av_packet_unref(in_pkt);
            }
            av_packet_free(&in_pkt);
    
            av_write_trailer(out_ctx);
    
            // Replace original copy with new version
            avformat_close_input(&fmt_ctx);
            if (out_ctx && !(out_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&out_ctx->pb);
            }
            avformat_free_context(out_ctx);
        }
        catch (...) {
            if (fmt_ctx) avformat_close_input(&fmt_ctx);
            if (out_ctx) {
                if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
                    avio_closep(&out_ctx->pb);
                }
                avformat_free_context(out_ctx);
            }
            if (swr_ctx) swr_free(&swr_ctx);
            if (av_frame) av_frame_free(&av_frame);
            if (pkt) av_packet_free(&pkt);
            if (codec_ctx) avcodec_free_context(&codec_ctx);
            throw;
        }
    
        if (swr_ctx) swr_free(&swr_ctx);
        if (av_frame) av_frame_free(&av_frame);
        if (pkt) av_packet_free(&pkt);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
    }
};

inline VideoFrameBuffer::VideoFrameBuffer(DataVideo* p, size_t idx, size_t w, size_t h) : 
    parent(p), frame_index(idx), modified(false), width(w), height(h),
    frame_data(h, std::vector<uint8_t>(w * 3)) {}

inline void VideoFrameBuffer::commit() {
    if (modified && parent) {
        parent->commit_frame(frame_index, frame_data);
        modified = false;
    }
}

inline AudioFrameBuffer::AudioFrameBuffer(DataVideo* p, size_t idx, size_t samples, int channels) : 
    parent(p), frame_index(idx), modified(false), 
    sample_count(samples), channel_count(channels),
    audio_data(samples * channels) {}

inline void AudioFrameBuffer::commit() {
    if (modified && parent) {
        parent->commit_audio(frame_index, audio_data, sample_count);
        modified = false;
    }
}

#endif // DATA_VIDEO_H