#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace api {

  enum class UserSettingsSection {
    Account,
    Profile,
    GuildProfile,
    GuildMember,
    Sessions,
    AuthorizedApps,
    Connections,
    ConnectionRefresh,
    ConnectionAuthorize,
    Relationships,
    Subscriptions,
    Payments,
    PaymentSources,
    Entitlements,
    Collectibles,
    DataPackage,
    EmailVerification,
    EmailChange,
    Phone,
    PhoneVerification,
    PhoneResend,
    Password,
    PasswordRecovery,
    DisableAccount,
    DeleteAccount,
    BackupCodes,
    Authenticator,
    SmsAuthentication,
    SecurityKeys,
    GiftCode,
    PaymentInvoice,
    PurchaseHistory
  };
  enum class UserSettingsVerb { Get, Create, Update, Delete };

  struct UserSettingsRequest {
    UserSettingsSection section = UserSettingsSection::Account;
    UserSettingsVerb verb = UserSettingsVerb::Get;
    std::string resourceId, guildId, service;
    std::string cursor;
    unsigned limit = 100;
    nlohmann::json values = nlohmann::json::object();
    std::uint64_t request = 0;
    std::uint64_t mfaChallenge = 0;
    std::string mfaType, mfaCode;
    // These are request-only, session-bound inputs. Never echo them in events,
    // save them to state, or retain them after the operation is completed.
    std::string password, newPassword, verificationCode, authenticatorSecret, giftCode;
    bool operator==(const UserSettingsRequest&) const = default;
  };

  struct UserSettingsHttpRequest {
    std::string path, method = "GET";
    nlohmann::json body;
    bool mayRotateToken = false;
    bool endsSession = false;
    // Backup codes and enrollment seeds require ephemeral, covered native UI.
    bool sensitiveResponse = false;
  };

  struct UserSettingsResult {
    nlohmann::json data = nlohmann::json::object();
    bool more = false;
    std::string cursor;
  };

  // Select fixed Discord resource routes and validate every explicit mutation.
  std::optional<UserSettingsHttpRequest> userSettingsHttpRequest(const UserSettingsRequest& request);
  UserSettingsResult parseUserSettings(const nlohmann::json& value, const UserSettingsRequest& request);
  UserSettingsRequest publicUserSettingsRequest(UserSettingsRequest request);
  bool sameUserSettingsAction(const UserSettingsRequest& original, const UserSettingsRequest& retry);
  std::string generateAuthenticatorSecret();
  // Native account management cannot create a passkey or authorize payment
  // provider UI. These are actual supported Discord browser destinations.
  std::string userSettingsBrowserUrl(UserSettingsSection section);
  // Refund requests use the same support flow as Discord's current billing UI.
  std::string userPaymentRefundUrl();
  std::string userGiftCodeBrowserUrl(const std::string& code);

} // namespace api
