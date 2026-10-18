#include "api/uploads.h"

#include <stdexcept>

namespace api {
  MessageMime makeMessageMime(CURL* curl, std::string_view payload, const std::vector<Upload>& uploads) {
    if (!validUploads(uploads) || uploads.empty())
      throw std::runtime_error("Invalid image attachments");
    MessageMime mime(curl_mime_init(curl), curl_mime_free);
    if (!mime)
      throw std::runtime_error("Could not allocate image upload");
    auto check = [](CURLcode code) {
      if (code != CURLE_OK)
        throw std::runtime_error("Could not prepare image upload");
    };
    auto part = [&] {
      auto* value = curl_mime_addpart(mime.get());
      if (!value)
        throw std::runtime_error("Could not allocate upload part");
      return value;
    };
    auto* json = part();
    check(curl_mime_name(json, "payload_json"));
    check(curl_mime_data(json, payload.data(), payload.size()));
    check(curl_mime_type(json, "application/json"));
    for (std::size_t i = 0; i < uploads.size(); ++i) {
      const auto& upload = uploads[i];
      auto* image = part();
      check(curl_mime_name(image, ("files[" + std::to_string(i) + "]").c_str()));
      check(curl_mime_filename(image, upload.filename.c_str()));
      check(curl_mime_type(image, upload.mime.c_str()));
      check(curl_mime_data(image, reinterpret_cast<const char*>(upload.bytes.data()), upload.bytes.size()));
    }
    return mime;
  }
} // namespace api
