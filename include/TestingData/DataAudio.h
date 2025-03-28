#ifndef DATA_AUDIO_H
#define DATA_AUDIO_H

#include "Data.h"
#include <vector>
#include <cmath>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

struct AudioSample {
    float left;
    float right;
    
    AudioSample(float l = 0.0f, float r = 0.0f) : left(l), right(r) {}
};

using MetadataAudio = std::tuple<AudioSample*, size_t, int>;

class DataAudio : public Data<MetadataAudio> {
public:
    DataAudio(const std::string& filename) : _channels(0) {
        _filename = filename;
        av_log_set_level(AV_LOG_ERROR);
    }
    
    ~DataAudio() override { 
        clear(); 
        clear_copy();
    }

    void read() override {
        if (!_filename.empty()) load();
    }

    void clear() override {
        _samples.clear();
        _samples.shrink_to_fit();
        _sampleCount = 0;
        _sampleRate = 0;
        _channels = 0;
    }

    MetadataAudio& copy() override {
        clear_copy();
        
        AudioSample* copyData = new AudioSample[_sampleCount];
        std::copy(_samples.begin(), _samples.end(), copyData);
        
        _copy = std::make_tuple(copyData, _sampleCount, _sampleRate);
        return _copy;
    }

    void clear_copy() override {
        if (auto* data = std::get<0>(_copy)) {
            delete[] data;
            _copy = std::make_tuple(nullptr, 0, 0);
        }
    }

    const std::string save_copy(const std::string& dirname, int args_id, int thread_num = 0) const override {
        try {
            auto data = std::get<0>(_copy);
            if (data) {
                std::string filename = "proc" + proc_data_str(args_id, thread_num) + "_" + _filename + ".wav";
                std::filesystem::path file_path = std::filesystem::path(dirname) / filename;
                save(true, args_id, thread_num, file_path.string());
                return filename;
            }
            throw std::runtime_error("Copy data not found");
        } catch (const std::bad_variant_access&) {
            throw std::runtime_error("Copy data not found");
        }
    }

    const std::string title() const override {
        std::stringstream ss;
        ss << "Аудио. Частота: " << _sampleRate << " Гц, ";
        ss << "Сэмплов: " << _sampleCount << ", ";
        ss << "Каналов: " << _channels;
        return ss.str();
    }

protected:
    std::vector<AudioSample> _samples;
    size_t _sampleCount = 0;
    int _sampleRate = 0;
    int _channels;

    void save(bool saveCopy, int args_id, int thread_num, const std::string& filename) const override {
        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        AVFrame* frame = nullptr;
        AVPacket* pkt = nullptr;
        AVChannelLayout out_layout;

        try {
            if (avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename.c_str()) < 0) {
                throw std::runtime_error("Could not create output context");
            }

            const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_PCM_F32LE);
            if (!codec) {
                throw std::runtime_error("Codec not found");
            }

            AVStream* stream = avformat_new_stream(fmt_ctx, nullptr);
            if (!stream) {
                throw std::runtime_error("Could not create stream");
            }

            codec_ctx = avcodec_alloc_context3(codec);
            if (!codec_ctx) {
                throw std::runtime_error("Could not allocate codec context");
            }

            if (_channels == 1) {
                av_channel_layout_from_mask(&out_layout, AV_CH_LAYOUT_MONO);
            } else {
                av_channel_layout_from_mask(&out_layout, AV_CH_LAYOUT_STEREO);
            }

            codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLT;
            codec_ctx->sample_rate = _sampleRate;
            av_channel_layout_copy(&codec_ctx->ch_layout, &out_layout);
            codec_ctx->bit_rate = 64000;
            stream->time_base = (AVRational){1, _sampleRate};

            if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
                throw std::runtime_error("Could not open codec");
            }

            if (avcodec_parameters_from_context(stream->codecpar, codec_ctx) < 0) {
                throw std::runtime_error("Could not copy codec parameters");
            }

            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
                    throw std::runtime_error("Could not open output file");
                }
            }

            if (avformat_write_header(fmt_ctx, nullptr) < 0) {
                throw std::runtime_error("Could not write header");
            }

            frame = av_frame_alloc();
            if (!frame) {
                throw std::runtime_error("Could not allocate frame");
            }
            
            frame->nb_samples = std::min(1024, static_cast<int>(_sampleCount));
            frame->format = codec_ctx->sample_fmt;
            av_channel_layout_copy(&frame->ch_layout, &codec_ctx->ch_layout);
            
            if (av_frame_get_buffer(frame, 0) < 0) {
                throw std::runtime_error("Could not allocate frame data");
            }

            pkt = av_packet_alloc();
            if (!pkt) {
                throw std::runtime_error("Could not allocate packet");
            }

            int64_t pts = 0;
            size_t samples_written = 0;
            while (samples_written < _sampleCount) {
                size_t samples_to_write = std::min(
                    static_cast<size_t>(frame->nb_samples), 
                    _sampleCount - samples_written
                );

                if (av_frame_make_writable(frame) < 0) {
                    throw std::runtime_error("Frame is not writable");
                }

                if (saveCopy) {
                    auto copyData = std::get<0>(_copy);
                    for (size_t i = 0; i < samples_to_write; ++i) {
                        size_t idx = samples_written + i;
                        if (_channels == 1) {
                            ((float*)frame->data[0])[i] = copyData[idx].left;
                        } else {
                            ((float*)frame->data[0])[i*2] = copyData[idx].left;
                            ((float*)frame->data[0])[i*2 + 1] = copyData[idx].right;
                        }
                    }
                } else {
                    for (size_t i = 0; i < samples_to_write; ++i) {
                        size_t idx = samples_written + i;
                        if (_channels == 1) {
                            ((float*)frame->data[0])[i] = _samples[idx].left;
                        } else {
                            ((float*)frame->data[0])[i*2] = _samples[idx].left;
                            ((float*)frame->data[0])[i*2 + 1] = _samples[idx].right;
                        }
                    }
                }

                frame->nb_samples = samples_to_write;
                frame->pts = pts;
                pts += samples_to_write;

                if (avcodec_send_frame(codec_ctx, frame) == 0) {
                    while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                        av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
                        pkt->stream_index = stream->index;
                        if (av_interleaved_write_frame(fmt_ctx, pkt) < 0) {
                            throw std::runtime_error("Error writing audio frame");
                        }
                        av_packet_unref(pkt);
                    }
                }

                samples_written += samples_to_write;
            }

            if (avcodec_send_frame(codec_ctx, nullptr) == 0) {
                while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                    av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
                    pkt->stream_index = stream->index;
                    if (av_interleaved_write_frame(fmt_ctx, pkt) < 0) {
                        throw std::runtime_error("Error writing audio frame");
                    }
                    av_packet_unref(pkt);
                }
            }

            av_write_trailer(fmt_ctx);
            av_channel_layout_uninit(&out_layout);
        }
        catch (...) {
            av_channel_layout_uninit(&out_layout);
            if (pkt) av_packet_free(&pkt);
            if (frame) av_frame_free(&frame);
            if (codec_ctx) avcodec_free_context(&codec_ctx);
            if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&fmt_ctx->pb);
            }
            if (fmt_ctx) avformat_free_context(fmt_ctx);
            throw;
        }

        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmt_ctx->pb);
        }
        if (fmt_ctx) avformat_free_context(fmt_ctx);
    }

    void load() override {
        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        SwrContext* swr_ctx = nullptr;
        AVFrame* frame = nullptr;
        AVPacket* pkt = nullptr;
        AVChannelLayout in_layout, out_layout;
    
        try {
            AVDictionary* format_options = nullptr;
            av_dict_set(&format_options, "scan_all_pmts", "1", AV_DICT_MATCH_CASE);
            
            if (avformat_open_input(&fmt_ctx, _filename.c_str(), nullptr, &format_options) != 0) {
                av_dict_free(&format_options);
                throw std::runtime_error("Could not open file: " + _filename);
            }
            av_dict_free(&format_options);
    
            if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
                throw std::runtime_error("Could not find stream information");
            }
    
            int audio_stream_idx = -1;
            const AVCodec* codec = nullptr;
            AVCodecParameters* codec_par = nullptr;
            
            for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
                codec_par = fmt_ctx->streams[i]->codecpar;
                if (codec_par->codec_type == AVMEDIA_TYPE_AUDIO) {
                    audio_stream_idx = i;
                    codec = avcodec_find_decoder(codec_par->codec_id);
                    if (codec) break;
                }
            }
            
            if (audio_stream_idx == -1 || !codec) {
                throw std::runtime_error("Could not find audio stream or unsupported codec");
            }
    
            codec_ctx = avcodec_alloc_context3(codec);
            if (!codec_ctx) {
                throw std::runtime_error("Could not allocate codec context");
            }
    
            if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0) {
                throw std::runtime_error("Could not copy codec parameters");
            }
    
            AVDictionary* decoder_options = nullptr;
            av_dict_set(&decoder_options, "strict", "experimental", 0);
            
            if (avcodec_open2(codec_ctx, codec, &decoder_options) < 0) {
                av_dict_free(&decoder_options);
                throw std::runtime_error("Could not open codec");
            }
            av_dict_free(&decoder_options);
    
            av_channel_layout_copy(&in_layout, &codec_ctx->ch_layout);
            _sampleRate = codec_ctx->sample_rate;
            _channels = in_layout.nb_channels;
    
            if (_channels == 1) {
                av_channel_layout_from_mask(&out_layout, AV_CH_LAYOUT_MONO);
            } else {
                av_channel_layout_from_mask(&out_layout, AV_CH_LAYOUT_STEREO);
                _channels = 2;
            }
    
            frame = av_frame_alloc();
            if (!frame) throw std::runtime_error("Could not allocate frame");
            
            pkt = av_packet_alloc();
            if (!pkt) throw std::runtime_error("Could not allocate packet");
    
            if (swr_alloc_set_opts2(&swr_ctx,
                                  &out_layout, AV_SAMPLE_FMT_FLT, _sampleRate,
                                  &in_layout, codec_ctx->sample_fmt, _sampleRate,
                                  0, nullptr) < 0 || !swr_ctx || swr_init(swr_ctx) < 0) {
                throw std::runtime_error("Could not initialize resampler");
            }
    
            _samples.reserve(fmt_ctx->duration * _sampleRate / AV_TIME_BASE * _channels);
    
            while (av_read_frame(fmt_ctx, pkt) >= 0) {
                if (pkt->stream_index == audio_stream_idx) {
                    if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                        while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                            int out_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
                            if (out_samples <= 0) {
                                out_samples = frame->nb_samples;
                            }
                            
                            uint8_t* out_data[AV_NUM_DATA_POINTERS] = {nullptr};
                            int linesize;
                            if (av_samples_alloc(out_data, &linesize, out_layout.nb_channels,
                                              out_samples, AV_SAMPLE_FMT_FLT, 0) < 0) {
                                throw std::runtime_error("Could not allocate samples");
                            }
                            
                            int converted = swr_convert(swr_ctx, out_data, out_samples,
                                                      (const uint8_t**)frame->data, frame->nb_samples);
                            
                            if (converted > 0) {
                                size_t prev_size = _samples.size();
                                _samples.resize(prev_size + converted);
                                
                                float* audio_data = (float*)out_data[0];
                                for (int i = 0; i < converted; ++i) {
                                    if (_channels == 1) {
                                        _samples[prev_size + i] = AudioSample(audio_data[i], 0.0f);
                                    } else {
                                        _samples[prev_size + i] = AudioSample(
                                            audio_data[i*2], 
                                            audio_data[i*2 + 1]
                                        );
                                    }
                                }
                            }
                            
                            if (out_data[0]) av_freep(&out_data[0]);
                        }
                    }
                }
                av_packet_unref(pkt);
            }
    
            _sampleCount = _samples.size();
            av_channel_layout_uninit(&in_layout);
            av_channel_layout_uninit(&out_layout);
        }
        catch (...) {
            av_channel_layout_uninit(&in_layout);
            av_channel_layout_uninit(&out_layout);
            if (pkt) av_packet_free(&pkt);
            if (frame) av_frame_free(&frame);
            if (swr_ctx) swr_free(&swr_ctx);
            if (codec_ctx) avcodec_free_context(&codec_ctx);
            if (fmt_ctx) avformat_close_input(&fmt_ctx);
            throw;
        }
    
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (swr_ctx) swr_free(&swr_ctx);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (fmt_ctx) avformat_close_input(&fmt_ctx);
    }
};

#endif