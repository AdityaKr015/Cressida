#pragma once

#include "api/guild_settings.h"
#include "api/role_styles.h"
#include "core/timer_manager.h"

#include <map>
#include <set>
#include <string>

namespace app {
  struct ServerImageCrop {
    std::string field, mime, texture;
    std::vector<std::uint8_t> bytes, pixels;
    int width = 0, height = 0;
    double zoom = 1, x = 0.5, y = 0.5;
    bool animated = false;
  };
  // Guild management drafts and result identities stay separate from account
  // preferences and from the channel currently displayed behind settings.
  struct ServerSettingsState {
    bool open = false, loading = false, saving = false, creating = false;
    bool confirmDiscard = false, confirmDelete = false, more = false, colorPicker = false;
    std::string guild, page = "profile", resource, search, filter, status, error, cursor;
    std::string pendingPage, pendingResource, destructiveAction, confirmation, reason;
    nlohmann::json overview = nlohmann::json::object();
    nlohmann::json record = nlohmann::json::object();
    nlohmann::json draft = nlohmann::json::object();
    nlohmann::json expressionDrafts = nlohmann::json::object();
    nlohmann::json emojiUploads = nlohmann::json::array();
    std::string emojiUploading;
    bool emojiUploadSaving = false;
    nlohmann::json guideIconDrafts = nlohmann::json::object();
    bool expressionSaving = false;
    std::optional<ServerImageCrop> imageCrop;
    std::map<std::string, nlohmann::json> lists;
    std::set<std::string> loaded;
    std::map<std::uint64_t, api::GuildSettingsRequest> pending;
    std::uint64_t generation = 0, serial = 0, mfaChallenge = 0;
    std::optional<api::GuildSettingsRequest> mfaRequest, saveRemainder;
    std::vector<std::string> mfaMethods;
    std::string mfaType, mfaCode, gameQuery, profileColorPicker;
    nlohmann::json gameResults = nlohmann::json::array();
    std::map<std::string, std::string> gameNames;
    Timer gameTimer;
    std::vector<api::RoleUpdates> roleJournal;
    bool roleJournalOverflow = false;
    float scroll = 0, navScroll = 0, navRight = 0;
    bool dirty() const {
      return !draft.empty()
          || !expressionDrafts.empty()
          || !emojiUploads.empty()
          || !guideIconDrafts.empty()
          || imageCrop.has_value();
    }
  };
} // namespace app
