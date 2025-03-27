#ifndef DATA_IMAGE_H
#define DATA_IMAGE_H

#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
}

#include "Data.h"

struct RGBImage {
    uint8_t R;
    uint8_t G;
    uint8_t B;
};

using MetadataImage = std::tuple<RGBImage**, size_t, size_t>;

class DataImage : public Data<MetadataImage> {
public:
    DataImage(const std::string& filename) {
        _filename = filename;
    }

    void read() override {
        if (!this->_filename.empty()) {
            load();
        }
    }

    void clear() override {
        _data.clear();
        _data.shrink_to_fit();
    }

    MetadataImage& copy() override {
        clear_copy();

        RGBImage** copy = new RGBImage*[_height];
        for(size_t y = 0; y < _height; ++y) {
            copy[y] = new RGBImage[_width];
            for(size_t x = 0; x < _width; ++x) {
                int index = (y * _width + x) * 3;
                copy[y][x].R = _data[index];
                copy[y][x].G = _data[index + 1];
                copy[y][x].B = _data[index + 2];
            }
        }
        _copy = std::make_tuple(copy, _height, _width);
        return _copy;
    }

    void clear_copy() override {
        try {
            auto data = std::get<0>(_copy);
            if (data) {
                for (size_t y = 0; y < _height; y++) {
                    delete[] data[y];
                }
                delete[] data;
                data = nullptr;
            }
        } catch (const std::bad_variant_access& e) {
            return;
        }
    }

    const std::string save_copy(const std::string& dirname, int args_id, int thread_num) const override {
        try {
            auto data = std::get<0>(_copy);
            if (data) {
                std::string filename = "proc" + proc_data_str(args_id, thread_num) + "_" + _filename + ".png";
                std::filesystem::path file_path = std::filesystem::path(dirname) / filename;
                save(true, args_id, thread_num, file_path);
                return filename;
            } else {
                throw std::runtime_error("Copy data not found");
            }
        } catch (const std::bad_variant_access& e) {
            throw std::runtime_error("Copy data not found");
        }
    }

    const std::string title() const override {
        std::string title = "Изображение. Размер=" + std::to_string(_width) + " на " + std::to_string(_height); 
        return title;
    }

private:
    std::vector<uint8_t> _data;
    size_t _width = 0;
    size_t _height = 0;

    void load() override {
        AVFormatContext* formatContext = nullptr;
        AVCodecContext* codecContext = nullptr;
        AVFrame* frame = nullptr;
        SwsContext* swsContext = nullptr;
        AVPacket* packet = nullptr;
        
        try {
            if (avformat_open_input(&formatContext, _filename.c_str(), nullptr, nullptr) != 0) {
                throw std::runtime_error("Could not open file: " + _filename);
            }
        
            if (avformat_find_stream_info(formatContext, nullptr) < 0) {
                throw std::runtime_error("Could not find stream information");
            }
        
            int videoStreamIndex = -1;
            for (size_t i = 0; i < formatContext->nb_streams; i++) {
                if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    videoStreamIndex = i;
                    break;
                }
            }
            if (videoStreamIndex == -1) {
                throw std::runtime_error("Could not find video stream");
            }
        
            AVCodecParameters* codecParameters = formatContext->streams[videoStreamIndex]->codecpar;
        
            const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
            if (!codec) {
                throw std::runtime_error("Unsupported codec");
            }
        
            codecContext = avcodec_alloc_context3(codec);
            if (!codecContext) {
                throw std::runtime_error("Could not allocate codec context");
            }
            
            if (avcodec_parameters_to_context(codecContext, codecParameters) < 0) {
                throw std::runtime_error("Could not copy codec parameters to context");
            }
        
            if (avcodec_open2(codecContext, codec, nullptr) < 0) {
                throw std::runtime_error("Could not open codec");
            }
        
            frame = av_frame_alloc();
            if (!frame) {
                throw std::runtime_error("Could not allocate frame");
            }
        
            packet = av_packet_alloc();
            if (!packet) {
                throw std::runtime_error("Could not allocate packet");
            }
            
            while (av_read_frame(formatContext, packet) >= 0) {
                if (packet->stream_index == videoStreamIndex) {
                    if (avcodec_send_packet(codecContext, packet) == 0) {
                        if (avcodec_receive_frame(codecContext, frame) == 0) {
                            _width = frame->width;
                            _height = frame->height;
        
                            if (codecContext->pix_fmt == AV_PIX_FMT_YUVJ420P) {
                                codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
                                codecContext->color_range = AVCOL_RANGE_JPEG;
                            }
        
                            int srcRange = (codecContext->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
                            int dstRange = 1;
        
                            swsContext = sws_getContext(
                                _width, 
                                _height, 
                                codecContext->pix_fmt, 
                                _width, 
                                _height, 
                                AV_PIX_FMT_RGB24, 
                                SWS_BILINEAR, 
                                nullptr, 
                                nullptr, 
                                nullptr
                            );
                            if (!swsContext) {
                                throw std::runtime_error("Could not create SwsContext");
                            }
        
                            const int* coeffs = sws_getCoefficients(SWS_CS_DEFAULT);
                            sws_setColorspaceDetails(
                                swsContext, 
                                coeffs, srcRange,
                                coeffs, dstRange,
                                0, 1 << 16, 1 << 16 
                            );
        
                            _data.resize(_width * _height * 3);
        
                            uint8_t* dest[1] = {_data.data()};
                            int destLinesize[1] = {static_cast<int>(_width) * 3};
        
                            sws_scale(swsContext, frame->data, frame->linesize, 0, _height, dest, destLinesize);
                            
                            av_packet_unref(packet);
                            break;
                        }
                    }
                }
                av_packet_unref(packet);
            }
        }
        catch (...) {
            if (packet) {
                av_packet_free(&packet);
            }
            if (swsContext) {
                sws_freeContext(swsContext);
            }
            if (frame) {
                av_frame_free(&frame);
            }
            if (codecContext) {
                avcodec_free_context(&codecContext);
            }
            if (formatContext) {
                avformat_close_input(&formatContext);
            }
            throw;
        }
        
        if (packet) {
            av_packet_free(&packet);
        }
        if (swsContext) {
            sws_freeContext(swsContext);
        }
        if (frame) {
            av_frame_free(&frame);
        }
        if (codecContext) {
            avcodec_free_context(&codecContext);
        }
        if (formatContext) {
            avformat_close_input(&formatContext);
        }
    }

    void save(bool saveCopy, int args_id, int thread_num, const std::string& filename) const override {    
        AVFormatContext* outputContext = nullptr;
        if (avformat_alloc_output_context2(&outputContext, nullptr, nullptr, filename.c_str()) < 0) {
            throw std::runtime_error("Could not create output context");
        }
    
        if (!(outputContext->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&outputContext->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
                avformat_free_context(outputContext);
                throw std::runtime_error("Could not open output file"); 
            }
        }
    
        AVStream* stream = avformat_new_stream(outputContext, nullptr);
        if (!stream) {
            avformat_free_context(outputContext);
            throw std::runtime_error("Could not create stream");
        }
    
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
        if (!codec) {
            avformat_free_context(outputContext);
            throw std::runtime_error("PNG codec not found");
        }
    
        AVCodecContext* codecContext = avcodec_alloc_context3(codec);
        if (!codecContext) {
            avformat_free_context(outputContext);
            throw std::runtime_error("Could not allocate codec context");
        }
    
        codecContext->width = _width;
        codecContext->height = _height;
        codecContext->pix_fmt = AV_PIX_FMT_RGB24;
        codecContext->time_base = {1, 25};
    
        stream->time_base = codecContext->time_base;
        avcodec_parameters_from_context(stream->codecpar, codecContext);
    
        if (avcodec_open2(codecContext, codec, nullptr) < 0) {
            avcodec_free_context(&codecContext);
            avformat_free_context(outputContext);
            throw std::runtime_error("Could not open codec");
        }
    
        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            avcodec_free_context(&codecContext);
            avformat_free_context(outputContext);
            throw std::runtime_error("Could not allocate frame");
        }
    
        frame->format = codecContext->pix_fmt;
        frame->width = codecContext->width;
        frame->height = codecContext->height;
        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            avcodec_free_context(&codecContext);
            avformat_free_context(outputContext);
            throw std::runtime_error("Could not allocate frame buffer");
        }
    
        if (av_frame_make_writable(frame) < 0) {
            av_frame_free(&frame);
            avcodec_free_context(&codecContext);
            avformat_free_context(outputContext);
            throw std::runtime_error("Frame is not writable");
        }
    
        uint8_t* srcData;
        if (saveCopy) {
            auto data = std::get<0>(_copy);
            srcData = new uint8_t[_width * _height * 3];
            for (size_t y = 0; y < _height; ++y) {
                for (size_t x = 0; x < _width; ++x) {
                    size_t index = (y * _width + x) * 3;
                    srcData[index] = data[y][x].R;
                    srcData[index + 1] = data[y][x].G;
                    srcData[index + 2] = data[y][x].B;
                }
                memcpy(frame->data[0] + y * frame->linesize[0], srcData + y * _width * 3, _width * 3);
            }
        } else {
            for(int y = 0; y < _height; y++) {
                memcpy(frame->data[0] + y * frame->linesize[0], _data.data() + y * _width * 3, _width * 3);
            }
        }
    
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            av_frame_free(&frame);
            avcodec_free_context(&codecContext);
            avformat_free_context(outputContext);
            throw std::runtime_error("Could not allocate packet");
        }
    
        AVDictionary *options = nullptr;
        av_dict_set(&options, "update", "1", 0);
        av_dict_set(&options, "frames:v", "1", 0);

        if (avformat_write_header(outputContext, &options) < 0) {
            av_dict_free(&options);
            av_packet_free(&packet);
            av_frame_free(&frame);
            avcodec_free_context(&codecContext);
            avformat_free_context(outputContext);
            throw std::runtime_error("Could not write header");
        }
    
        frame->pts = 0;

        if (avcodec_send_frame(codecContext, frame) == 0) {
            if (avcodec_receive_packet(codecContext, packet) == 0) {
                if (av_write_frame(outputContext, packet) < 0) {
                    av_packet_free(&packet);
                    av_frame_free(&frame);
                    avcodec_free_context(&codecContext);
                    avformat_free_context(outputContext);
                    throw std::runtime_error("Could not write frame");
                }
                av_packet_unref(packet);
            }
        }
    
        av_write_trailer(outputContext);

        av_dict_free(&options);
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codecContext);
        if (!(outputContext->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&outputContext->pb);
        }
        avformat_free_context(outputContext);
    }
};

#endif