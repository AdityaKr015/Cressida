#pragma once

#include "core/process/wake_event.h"
#include "render/core/image_decoder.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

typedef struct _GCancellable GCancellable;

namespace app {
  struct PickedFile {
    std::uint64_t request = 0;
    std::string mime, name, error;
    std::vector<std::uint8_t> bytes;
    std::optional<DecodedRasterImage> raster;
    std::vector<PickedFile> files;
    bool cancelled = false;
  };
  // Portal and bounded file reads run on a private worker, never in the UI loop.
  class FilePicker {
  public:
    FilePicker();
    ~FilePicker();
    FilePicker(const FilePicker&) = delete;
    FilePicker& operator=(const FilePicker&) = delete;
    int eventFd() const { return m_wake.fd(); }
    void open(
        std::uint64_t request, std::string title, std::vector<std::string> types, std::size_t maximum,
        bool multiple = false
    );
    void cancel();
    std::optional<PickedFile> poll();
    static PickedFile readSelection(const std::string& uri, std::span<const std::string> types, std::size_t maximum);

  private:
    struct Request {
      std::uint64_t id;
      std::string title;
      std::vector<std::string> types;
      std::size_t maximum;
      bool multiple = false;
    };
    void run(std::stop_token stop);
    process::WakeEvent m_wake;
    std::mutex m_mutex;
    std::condition_variable_any m_condition;
    std::optional<Request> m_pending;
    std::deque<PickedFile> m_results;
    GCancellable* m_active = nullptr;
    std::jthread m_worker;
  };
} // namespace app
