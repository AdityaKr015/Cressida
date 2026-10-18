#pragma once

#include "api/user_settings.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app {
  struct UserSettingsListPending {
    api::UserSettingsRequest request;
    std::uint64_t generation = 0;
    std::string cache;
    std::uint64_t directoryRevision = 0, giftVersion = 0;
  };
  struct UserSettingsListsState {
    std::uint64_t serial = 0, generation = 1, mfaChallenge = 0;
    std::string page, search, status, error, confirmation, mfaMethod, mfaCode, confirmLabel;
    std::map<std::uint64_t, UserSettingsListPending> pending;
    std::map<std::string, nlohmann::json> records;
    std::map<std::string, std::string> errors;
    std::set<std::string> loaded, loading;
    std::optional<api::UserSettingsRequest> confirmationRequest, mfaRequest;
    std::vector<std::string> mfaMethods;
    std::string nicknameTarget, nickname, requestUsername, giftCode, invoicePayment;
    std::uint64_t giftVersion = 1, giftPreviewVersion = 0, confirmationGiftVersion = 0;
    bool giftRedeemed = false;
    std::map<std::string, bool> more;
    std::map<std::string, std::string> cursors;
    std::set<std::string> excludedBackends;
    std::size_t relationshipFilter = 0;
    bool busy = false;
  };
} // namespace app
