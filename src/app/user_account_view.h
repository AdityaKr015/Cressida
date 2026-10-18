#pragma once

#include "api/user_settings.h"
#include "render/core/image_decoder.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app {
  struct UserAccountPending {
    api::UserSettingsRequest request;
    std::uint64_t generation = 0;
    std::string cache;
  };
  struct UserProfileCrop {
    std::string field, mime, texture;
    std::vector<std::uint8_t> bytes, pixels;
    int width = 0, height = 0;
    double zoom = 1, x = .5, y = .5;
    bool animated = false;
  };
  // Private account data, verification inputs and drafts are session-only.
  // Requests kept for matching are scrubbed before entering this state.
  struct UserAccountState {
    std::uint64_t serial = 0, generation = 1, dialogGeneration = 0, saveRequest = 0;
    std::map<std::uint64_t, UserAccountPending> pending;
    std::map<std::string, nlohmann::json> records;
    std::set<std::string> loaded, loading;
    std::map<std::string, std::string> errors;
    std::string page, guild, status, error, dialog, resource, service;
    std::map<std::string, std::string> fields;
    nlohmann::json profile = nlohmann::json::object(), draft = nlohmann::json::object();
    std::vector<api::UserSettingsRequest> profileSaves;
    nlohmann::json backupCodes = nlohmann::json::array();
    std::optional<api::UserSettingsRequest> mfaRequest;
    std::uint64_t mfaChallenge = 0;
    std::vector<std::string> mfaMethods;
    std::string mfaMethod;
    std::string mfaCache;
    std::optional<UserProfileCrop> crop;
    std::uint64_t uploadRequest = 0, uploadGeneration = 0;
    std::string uploadField;
    float dialogX = 0, dialogY = 0, dialogWidth = 0, dialogHeight = 0;
    float dialogScroll = 0;
    bool revealEmail = false, revealPhone = false, busy = false, confirmed = false;
  };
  bool userAccountProfileReady(const UserAccountState& state);
} // namespace app
