#pragma once

#include "api/rest_schedule.h"
#include "core/process/wake_event.h"

#include <curl/curl.h>

namespace api {

  // A single easy handle on a worker-owned multi handle preserves its connection
  // cache while allowing the queue's private pipe to interrupt idle reads.
  inline CURLcode performRestTransfer(
      CURL* easy, CURLM* multi, const process::WakeEvent& wake, const RestRequestScope& scope,
      RestCancellation& interrupted
  ) {
    const auto cancelled = [&] {
      const auto reason = scope.cancellation();
      if (reason != RestCancellation::None)
        interrupted = reason;
      return reason != RestCancellation::None;
    };
    if (cancelled())
      return CURLE_ABORTED_BY_CALLBACK;
    if (!multi || curl_multi_add_handle(multi, easy) != CURLM_OK)
      return CURLE_FAILED_INIT;
    struct Remove {
      CURLM* multi;
      CURL* easy;
      ~Remove() { (void)curl_multi_remove_handle(multi, easy); }
    } remove{multi, easy};
    int running = 0;
    auto status = curl_multi_perform(multi, &running);
    while (status == CURLM_OK && running) {
      if (cancelled())
        return CURLE_ABORTED_BY_CALLBACK;
      curl_waitfd signal{wake.fd(), CURL_WAIT_POLLIN, 0};
      // This is a socket/deadline wait, not a polling timer. Queue changes wake
      // it even when Discord has not sent any bytes or completed a connection.
      status = curl_multi_poll(multi, &signal, 1, 1000, nullptr);
      if (signal.revents & CURL_WAIT_POLLIN)
        wake.drain();
      if (cancelled())
        return CURLE_ABORTED_BY_CALLBACK;
      if (status == CURLM_OK)
        status = curl_multi_perform(multi, &running);
    }
    if (status != CURLM_OK)
      return CURLE_RECV_ERROR;
    int remaining = 0;
    while (const auto* message = curl_multi_info_read(multi, &remaining))
      if (message->msg == CURLMSG_DONE && message->easy_handle == easy)
        return message->data.result;
    return CURLE_RECV_ERROR;
  }

} // namespace api
