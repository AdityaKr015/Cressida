#include "discord/uploads.h"

#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char** argv) {
  if (argc != 2)
    return 2;
  using nlohmann::json;
  try {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    const std::vector<discord::Upload> uploads{
        {"plant.png", "image/png", {0x89, 'P', 'N', 'G', 0, 10, 255}}, {"second.jpg", "image/jpeg", {255, 216, 255}}
    };
    const auto payload = json{
        {"content", "first\nsecond 🌿"},
        {"attachments", json::array({{{"id", 0}, {"filename", "plant.png"}}, {{"id", 1}, {"filename", "second.jpg"}}})}
    }.dump();
    auto mime = discord::makeMessageMime(curl.get(), payload, uploads);
    curl_easy_setopt(curl.get(), CURLOPT_URL, argv[1]);
    curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, mime.get());
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROXY, "*");
    std::string response;
    curl_easy_setopt(
        curl.get(), CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t count, void* user) -> size_t {
          static_cast<std::string*>(user)->append(data, size * count);
          return size * count;
        }
    );
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    const auto status = curl_easy_perform(curl.get());
    long code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
    if (status != CURLE_OK || code != 200 || response != "ok")
      throw std::runtime_error("Multipart message upload was not accepted by the fixture");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
