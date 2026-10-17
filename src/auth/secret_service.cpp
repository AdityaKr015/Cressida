#include "auth/credentials.h"

#include <chrono>
#include <libsecret/secret.h>

namespace auth {
  namespace {
    const SecretSchema schema = {
        "dev.lysec.Cressida.Session",
        SECRET_SCHEMA_NONE,
        {{"application", SECRET_SCHEMA_ATTRIBUTE_STRING},
         {"slot", SECRET_SCHEMA_ATTRIBUTE_STRING},
         {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}
    };
    // A keyring unlock prompt may outlive a network request. Cancellation and
    // a finite deadline keep shutdown and alternate login attempts responsive.
    struct Call {
      std::unique_ptr<GCancellable, decltype(&g_object_unref)> owner{g_cancellable_new(), g_object_unref};
      GCancellable* cancel = owner.get();
      GError* error = nullptr;
      std::mutex mutex;
      std::condition_variable_any ready;
      std::stop_callback<std::function<void()>> cancellation;
      std::jthread deadline;
      explicit Call(std::stop_token stop) : cancellation(stop, [this] { g_cancellable_cancel(cancel); }) {
        deadline = std::jthread([this](std::stop_token done) {
          std::unique_lock lock(mutex);
          ready.wait_for(lock, done, std::chrono::seconds(30), [] { return false; });
          if (!done.stop_requested())
            g_cancellable_cancel(cancel);
        });
      }
      ~Call() {
        deadline.request_stop();
        ready.notify_all();
        deadline.join();
        g_clear_error(&error);
      }
      CredentialFailure failure() const {
        return g_cancellable_is_cancelled(cancel) ? CredentialFailure::Cancelled : CredentialFailure::Unavailable;
      }
    };
    class SecretService final : public CredentialBackend {
    public:
      std::expected<std::string, CredentialFailure> load(std::stop_token stop) override {
        Call call(stop);
        auto* password = secret_password_lookup_sync(
            &schema, call.cancel, &call.error, "application", "dev.lysec.Cressida", "slot", "current", nullptr
        );
        std::unique_ptr<gchar, decltype(&secret_password_free)> owner(password, secret_password_free);
        if (call.error || g_cancellable_is_cancelled(call.cancel))
          return std::unexpected(call.failure());
        return password ? std::string(password) : std::string{};
      }
      std::expected<void, CredentialFailure> save(const std::string& token, std::stop_token stop) override {
        Call call(stop);
        if (!secret_password_store_sync(
                &schema, SECRET_COLLECTION_DEFAULT, "Cressida — Discord login", token.c_str(), call.cancel, &call.error,
                "application", "dev.lysec.Cressida", "slot", "current", nullptr
            ))
          return std::unexpected(call.failure());
        return {};
      }
      std::expected<void, CredentialFailure> erase(std::stop_token stop) override {
        Call call(stop);
        auto* attributes =
            secret_attributes_build(&schema, "application", "dev.lysec.Cressida", "slot", "current", nullptr);
        auto* items = secret_service_search_sync(
            nullptr, &schema, attributes, static_cast<SecretSearchFlags>(SECRET_SEARCH_ALL | SECRET_SEARCH_UNLOCK),
            call.cancel, &call.error
        );
        g_hash_table_unref(attributes);
        bool success = call.error == nullptr;
        for (auto* node = items; node && success; node = node->next)
          success = secret_item_delete_sync(SECRET_ITEM(node->data), call.cancel, &call.error);
        g_list_free_full(items, g_object_unref);
        if (!success || g_cancellable_is_cancelled(call.cancel))
          return std::unexpected(call.failure());
        return {};
      }
    };
  } // namespace
  std::unique_ptr<CredentialBackend> secretServiceBackend() { return std::make_unique<SecretService>(); }
} // namespace auth
