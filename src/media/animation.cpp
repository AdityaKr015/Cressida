#include "media/animation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <webp/demux.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace media {
  namespace {
    constexpr std::size_t maxBytes = 64 * 1024 * 1024;
    constexpr int maxFrames = 1200;
    bool dimensions(int w, int h) { return w > 0 && h > 0 && w <= 4096 && h <= 4096 && std::int64_t(w) * h <= 8388608; }
    bool shrink(Animation& animation) {
      if (animation.width <= 64 && animation.height <= 64)
        return false;
      const int width = std::max(1, animation.width / 2), height = std::max(1, animation.height / 2);
      std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scale(
          sws_getContext(
              animation.width, animation.height, AV_PIX_FMT_RGBA, width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
              nullptr, nullptr
          ),
          sws_freeContext
      );
      if (!scale)
        return false;
      for (auto& frame : animation.frames) {
        std::vector<std::uint8_t> resized(std::size_t(width) * height * 4);
        const std::uint8_t* source[4] = {frame.rgba.data()};
        int sourceStride[4] = {animation.width * 4};
        std::uint8_t* dest[4] = {resized.data()};
        int destStride[4] = {width * 4};
        if (sws_scale(scale.get(), source, sourceStride, 0, animation.height, dest, destStride) != height)
          throw std::runtime_error("Could not resize animation");
        frame.rgba = std::move(resized);
      }
      animation.width = width;
      animation.height = height;
      return true;
    }
    void finish(Animation& animation, bool truncated) {
      if (truncated && animation.frames.size() > 1)
        animation.frames.resize(1);
      for (const auto& frame : animation.frames)
        animation.duration += frame.duration;
    }
    struct Input {
      std::span<const std::uint8_t> bytes;
      std::int64_t offset = 0;
      std::stop_token stop;
      std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      bool cancelled() const { return stop.stop_requested() || std::chrono::steady_clock::now() >= deadline; }
      static int read(void* opaque, std::uint8_t* out, int size) {
        auto& input = *static_cast<Input*>(opaque);
        if (input.cancelled())
          return AVERROR_EXIT;
        const auto count = std::min<std::size_t>(size, input.bytes.size() - input.offset);
        if (!count)
          return AVERROR_EOF;
        std::memcpy(out, input.bytes.data() + input.offset, count);
        input.offset += count;
        return static_cast<int>(count);
      }
      static std::int64_t seek(void* opaque, std::int64_t offset, int whence) {
        auto& input = *static_cast<Input*>(opaque);
        if (whence == AVSEEK_SIZE)
          return input.bytes.size();
        whence &= ~AVSEEK_FORCE;
        const auto base = whence == SEEK_SET ? 0
            : whence == SEEK_CUR             ? input.offset
            : whence == SEEK_END             ? std::int64_t(input.bytes.size())
                                             : -1;
        if (base < 0 || offset < -base || offset > std::int64_t(input.bytes.size()) - base)
          return AVERROR(EINVAL);
        return input.offset = base + offset;
      }
    };
    struct Decoder {
      AVFormatContext* format = nullptr;
      AVIOContext* io = nullptr;
      AVCodecContext* codec = nullptr;
      AVFrame* frame = nullptr;
      AVPacket* packet = nullptr;
      SwsContext* scale = nullptr;
      ~Decoder() {
        sws_freeContext(scale);
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        if (io) {
          av_freep(&io->buffer);
          avio_context_free(&io);
        }
      }
    };
  } // namespace
  std::size_t Animation::byteSize() const {
    std::size_t size = 0;
    for (const auto& frame : frames)
      size += frame.rgba.size();
    return size;
  }
  std::size_t Animation::frameAt(std::chrono::milliseconds elapsed) const {
    if (frames.empty() || duration.count() <= 0)
      return 0;
    elapsed = std::chrono::milliseconds(std::max<std::int64_t>(0, elapsed.count()) % duration.count());
    for (std::size_t i = 0; i < frames.size(); ++i) {
      if (elapsed < frames[i].duration)
        return i;
      elapsed -= frames[i].duration;
    }
    return 0;
  }
  std::shared_ptr<const Animation> decodeAnimation(std::span<const std::uint8_t> bytes, std::stop_token stop) {
    try {
      if (bytes.size() < 12 || bytes.size() > 20 * 1024 * 1024 || stop.stop_requested())
        return {};
      const bool webp = std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
      const bool gif = std::memcmp(bytes.data(), "GIF87a", 6) == 0 || std::memcmp(bytes.data(), "GIF89a", 6) == 0;
      const bool mp4 = std::memcmp(bytes.data() + 4, "ftyp", 4) == 0;
      const bool webm = bytes[0] == 0x1a && bytes[1] == 0x45 && bytes[2] == 0xdf && bytes[3] == 0xa3;
      if (!gif && !webp && !mp4 && !webm)
        return {};
      auto result = std::make_shared<Animation>();
      if (webp) {
        WebPData data{bytes.data(), bytes.size()};
        std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(WebPDemux(&data), WebPDemuxDelete);
        if (!demux
            || !dimensions(
                WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_WIDTH), WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_HEIGHT)
            )
            || WebPDemuxGetI(demux.get(), WEBP_FF_FRAME_COUNT) < 2)
          return {};
        WebPAnimDecoderOptions options;
        if (!WebPAnimDecoderOptionsInit(&options))
          return {};
        options.color_mode = MODE_RGBA;
        std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(
            WebPAnimDecoderNew(&data, &options), WebPAnimDecoderDelete
        );
        WebPAnimInfo info{};
        if (!decoder
            || !WebPAnimDecoderGetInfo(decoder.get(), &info)
            || !dimensions(info.canvas_width, info.canvas_height))
          return {};
        const double frames = std::min<unsigned>(maxFrames, info.frame_count);
        const double ratio = std::min(
            {1.0, 512.0 / std::max(info.canvas_width, info.canvas_height),
             std::sqrt(double(maxBytes) / (4.0 * info.canvas_width * info.canvas_height * frames))}
        );
        result->width = std::max(1, int(info.canvas_width * ratio));
        result->height = std::max(1, int(info.canvas_height * ratio));
        const auto frameBytes = std::size_t(result->width) * result->height * 4;
        std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scale(
            sws_getContext(
                info.canvas_width, info.canvas_height, AV_PIX_FMT_RGBA, result->width, result->height, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            ),
            sws_freeContext
        );
        if (!scale)
          return {};
        bool truncated = false;
        int previous = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (WebPAnimDecoderHasMoreFrames(decoder.get())) {
          if (stop.stop_requested())
            return {};
          if (result->frames.size() >= maxFrames
              || frameBytes > maxBytes - result->frames.size() * frameBytes
              || std::chrono::steady_clock::now() >= deadline) {
            truncated = true;
            break;
          }
          std::uint8_t* rgba = nullptr;
          int timestamp = 0;
          if (!WebPAnimDecoderGetNext(decoder.get(), &rgba, &timestamp))
            return {};
          const auto duration = std::chrono::milliseconds(std::clamp(timestamp - previous, 20, 10000));
          Frame frame{{}, duration};
          frame.rgba.resize(frameBytes);
          const std::uint8_t* source[4] = {rgba};
          int sourceStride[4] = {int(info.canvas_width) * 4};
          std::uint8_t* dest[4] = {frame.rgba.data()};
          int destStride[4] = {result->width * 4};
          if (sws_scale(scale.get(), source, sourceStride, 0, info.canvas_height, dest, destStride) != result->height)
            return {};
          result->frames.push_back(std::move(frame));
          previous = timestamp;
        }
        finish(*result, truncated);
        return result->frames.empty() ? nullptr : result;
      }
      Input input{bytes, 0, stop};
      Decoder decoder;
      auto* buffer = static_cast<unsigned char*>(av_malloc(32768));
      if (!buffer)
        return {};
      decoder.io = avio_alloc_context(buffer, 32768, 0, &input, Input::read, nullptr, Input::seek);
      if (!decoder.io) {
        av_free(buffer);
        return {};
      }
      decoder.format = avformat_alloc_context();
      if (!decoder.format)
        return {};
      decoder.format->pb = decoder.io;
      decoder.format->max_streams = 8;
      decoder.format->flags |= AVFMT_FLAG_CUSTOM_IO;
      decoder.format->io_open = [](AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) {
        return AVERROR(EPERM);
      };
      decoder.format->interrupt_callback = {
          [](void* opaque) { return static_cast<Input*>(opaque)->cancelled() ? 1 : 0; }, &input
      };
      AVDictionary* options = nullptr;
      av_dict_set(&options, "protocol_whitelist", "", 0);
      av_dict_set(&options, "format_whitelist", gif ? "gif" : mp4 ? "mov" : "matroska,webm", 0);
      av_dict_set(&options, "ignore_loop", "1", 0);
      const auto opened = avformat_open_input(
          &decoder.format, nullptr,
          av_find_input_format(
              gif ? "gif"
                  : mp4 ? "mov"
                        : "matroska,webm"
          ),
          &options
      );
      av_dict_free(&options);
      if (opened < 0)
        return {};
      const AVCodec* codec = nullptr;
      const int index = av_find_best_stream(decoder.format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
      if (index < 0 || !codec)
        return {};
      const auto* stream = decoder.format->streams[index];
      if (!dimensions(stream->codecpar->width, stream->codecpar->height))
        return {};
      decoder.codec = avcodec_alloc_context3(codec);
      if (!decoder.codec || avcodec_parameters_to_context(decoder.codec, stream->codecpar) < 0)
        return {};
      decoder.codec->thread_count = 1;
      decoder.codec->max_pixels = 8388608;
      if (avcodec_open2(decoder.codec, codec, nullptr) < 0)
        return {};
      decoder.frame = av_frame_alloc();
      decoder.packet = av_packet_alloc();
      if (!decoder.frame || !decoder.packet)
        return {};
      const double ratio = std::min(1.0, 384.0 / std::max(decoder.codec->width, decoder.codec->height));
      result->width = std::max(1, int(decoder.codec->width * ratio));
      result->height = std::max(1, int(decoder.codec->height * ratio));
      auto frameBytes = std::size_t(result->width) * result->height * 4;
      bool truncated = false, flushing = false;
      std::int64_t previousPts = AV_NOPTS_VALUE;
      int packets = 0;
      while (!input.cancelled()) {
        const int received = avcodec_receive_frame(decoder.codec, decoder.frame);
        if (received == AVERROR_EOF)
          break;
        if (received == AVERROR(EAGAIN)) {
          if (flushing || ++packets > 12000) {
            truncated = true;
            break;
          }
          int read;
          do {
            av_packet_unref(decoder.packet);
            read = av_read_frame(decoder.format, decoder.packet);
          } while (read >= 0 && decoder.packet->stream_index != index && !input.cancelled());
          if (read < 0) {
            flushing = true;
            avcodec_send_packet(decoder.codec, nullptr);
          } else if (avcodec_send_packet(decoder.codec, decoder.packet) < 0)
            return {};
          continue;
        }
        if (received < 0 || !dimensions(decoder.frame->width, decoder.frame->height))
          return {};
        if (frameBytes > maxBytes - result->frames.size() * frameBytes && shrink(*result))
          frameBytes = std::size_t(result->width) * result->height * 4;
        if (result->frames.size() >= maxFrames || frameBytes > maxBytes - result->frames.size() * frameBytes) {
          truncated = true;
          break;
        }
        const auto pts = decoder.frame->best_effort_timestamp;
        if (!result->frames.empty() && previousPts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE && pts > previousPts)
          result->frames.back().duration = std::chrono::milliseconds(
              std::clamp<std::int64_t>(av_rescale_q(pts - previousPts, stream->time_base, {1, 1000}), 20, 10000)
          );
        const auto fallback = av_rescale_q(decoder.frame->duration, stream->time_base, {1, 1000});
        Frame frame{{}, std::chrono::milliseconds(fallback > 0 ? std::clamp<std::int64_t>(fallback, 20, 10000) : 100)};
        frame.rgba.resize(frameBytes);
        decoder.scale = sws_getCachedContext(
            decoder.scale, decoder.frame->width, decoder.frame->height,
            static_cast<AVPixelFormat>(decoder.frame->format), result->width, result->height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (!decoder.scale)
          return {};
        std::uint8_t* dest[4] = {frame.rgba.data()};
        int stride[4] = {result->width * 4};
        if (sws_scale(
                decoder.scale, decoder.frame->data, decoder.frame->linesize, 0, decoder.frame->height, dest, stride
            )
            != result->height)
          return {};
        result->frames.push_back(std::move(frame));
        previousPts = pts;
        av_frame_unref(decoder.frame);
      }
      if (stop.stop_requested())
        return {};
      finish(*result, truncated || input.cancelled());
      return result->frames.empty() ? nullptr : result;
    } catch (...) {
      return {};
    }
  }
} // namespace media
