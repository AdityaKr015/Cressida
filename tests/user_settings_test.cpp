#include "api/user_settings.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
  using Json = nlohmann::json;
  using Section = api::UserSettingsSection;
  using Verb = api::UserSettingsVerb;
  using Request = api::UserSettingsRequest;
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
  api::UserSettingsHttpRequest route(Request request) {
    const auto result = api::userSettingsHttpRequest(request);
    require(result.has_value(), "A valid explicit account settings action needs a validated route");
    return *result;
  }
} // namespace

int main() {
  try {
    require(route({}).path == "/users/@me", "Account inspection uses the authenticated self endpoint");
    Request edit{
        .section = Section::Account,
        .verb = Verb::Update,
        .values = {{"global_name", "Fen"}, {"avatar", nullptr}},
        .password = "synthetic-password"
    };
    const auto update = route(edit);
    require(
        update.path == "/users/@me"
            && update.method == "PATCH"
            && update.mayRotateToken
            && update.body["global_name"] == "Fen"
            && update.body["avatar"].is_null()
            && update.body["password"] == edit.password,
        "Account mutations contain only the user's explicit edits"
    );
    edit.values["token"] = "never-accepted";
    require(!api::userSettingsHttpRequest(edit), "A user-supplied account token is never a writable setting");
    edit.values = {{"username", "../admin"}};
    require(!api::userSettingsHttpRequest(edit), "Unique usernames cannot inject protocol paths");
    edit.values = {{"global_name", "Fen"}, {"unrecognized", true}};
    require(
        !api::userSettingsHttpRequest(edit),
        "Unknown mutation keys fail closed instead of changing undocumented preferences"
    );
    edit.values = {{"global_name", std::string("\xFF", 1)}};
    require(!api::userSettingsHttpRequest(edit), "Invalid UTF-8 fails validation without escaping a JSON exception");

    Request profile{
        .section = Section::Profile,
        .verb = Verb::Update,
        .resourceId = "123",
        .values = {
            {"bio", "Native profile"},
            {"pronouns", "they/them"},
            {"banner", nullptr},
            {"theme_colors", {0x123456, 0x654321}},
            {"collectibles_sku_ids", {"100", "200"}}
        }
    };
    require(
        route(profile).path == "/users/@me/profile",
        "Global profile metadata uses its separate current-user profile endpoint"
    );
    profile.section = Section::GuildProfile;
    profile.guildId = "456";
    require(
        route(profile).path == "/guilds/456/profile/@me",
        "Server profile metadata uses Discord's verified personal-account route"
    );
    profile.values["nick"] = "Nickname";
    require(
        !api::userSettingsHttpRequest(profile),
        "Nickname and profile edits cannot be silently combined across REST resources"
    );
    profile.values = {{"collectibles_sku_ids", {"100", "100"}}};
    require(!api::userSettingsHttpRequest(profile), "Profile cosmetic choices cannot contain duplicate SKU IDs");
    require(
        route(
            {.section = Section::GuildMember,
             .verb = Verb::Update,
             .guildId = "456",
             .values = {{"nick", "Fen"}, {"avatar", nullptr}, {"collectibles", {{"nameplate", {{"sku_id", "789"}}}}}}}
        ).path
            == "/guilds/456/members/@me",
        "Server nickname/avatar/nameplate use the current member endpoint"
    );
    require(
        !api::userSettingsHttpRequest(
            {.section = Section::GuildProfile, .verb = Verb::Get, .resourceId = "123", .guildId = "../456"}
        ),
        "Server profile reads validate the server identity"
    );

    Request password{
        .section = Section::Password,
        .verb = Verb::Update,
        .request = 10,
        .password = "synthetic-old",
        .newPassword = "synthetic-new"
    };
    require(
        route(password).body == Json({{"password", "synthetic-old"}, {"new_password", "synthetic-new"}}),
        "A password update never submits unrelated profile settings"
    );
    auto retry = api::publicUserSettingsRequest(password);
    retry.request = 11;
    retry.mfaChallenge = 5;
    retry.mfaType = "totp";
    retry.mfaCode = "123456";
    require(
        retry.password.empty() && retry.newPassword.empty() && api::sameUserSettingsAction(password, retry),
        "MFA can resume the exact worker-owned password change from a scrubbed UI request"
    );
    retry.values = {{"global_name", "Changed during verification"}};
    require(!api::sameUserSettingsAction(password, retry), "MFA challenges cannot authorize changed actions or values");
    retry = password;
    retry.section = Section::DeleteAccount;
    require(
        !api::sameUserSettingsAction(password, retry), "A challenge cannot be substituted for another account resource"
    );

    Request session{
        .section = Section::Sessions, .verb = Verb::Delete, .values = {{"session_id_hashes", {"device-1", "device-2"}}}
    };
    require(
        route(session).path == "/auth/sessions/logout" && route(session).method == "POST",
        "Device logout follows the current dedicated session logout API"
    );
    session.values["session_id_hashes"] = Json::array();
    require(
        !api::userSettingsHttpRequest(session), "An empty device selection must never become a sign-out-all action"
    );
    require(
        route({.section = Section::AuthorizedApps, .verb = Verb::Delete, .resourceId = "789"}).path
            == "/oauth2/tokens/789",
        "Deauthorization revokes the selected authorization record"
    );
    require(
        !api::userSettingsHttpRequest(
            {.section = Section::AuthorizedApps, .verb = Verb::Delete, .resourceId = "../789"}
        ),
        "App authorization IDs are validated before building routes"
    );
    Request connection{
        .section = Section::Connections,
        .verb = Verb::Update,
        .resourceId = "account@example.com",
        .service = "paypal",
        .values = {{"visibility", false}, {"metadata_visibility", true}, {"friend_sync", false}}
    };
    require(
        route(connection).path == "/users/@me/connections/paypal/account%40example.com"
            && route(connection).body["visibility"].is_boolean(),
        "Connection IDs are encoded and current visibility mutations use booleans"
    );
    connection.values["visibility"] = 1;
    require(
        !api::userSettingsHttpRequest(connection),
        "Do not send legacy numeric visibility values to the current patch API"
    );
    connection.service = "../../auth";
    require(!api::userSettingsHttpRequest(connection), "Connection provider names cannot create arbitrary API routes");
    require(
        route(
            {.section = Section::Relationships,
             .verb = Verb::Update,
             .resourceId = "12",
             .values = {{"user_ignored", true}}}
        ).path
            == "/users/@me/relationships/12/ignore",
        "Ignored users use their own verified relationship route"
    );
    require(
        route(
            {.section = Section::Relationships,
             .verb = Verb::Update,
             .resourceId = "12",
             .values = {{"user_ignored", false}}}
        ).method
            == "DELETE",
        "Unignore preserves friendship instead of removing the relationship"
    );
    require(
        !api::userSettingsHttpRequest(
            {.section = Section::Relationships, .verb = Verb::Update, .resourceId = "12", .values = {{"type", 0u}}}
        ),
        "Unsigned zero is not a valid friend or block mutation"
    );

    require(
        route({.section = Section::Subscriptions, .verb = Verb::Delete, .resourceId = "123"}).method == "DELETE",
        "Subscription cancellation uses the verified native cancellation endpoint"
    );
    require(
        !api::userSettingsHttpRequest(
            {.section = Section::Subscriptions,
             .verb = Verb::Update,
             .resourceId = "123",
             .values = {{"cancel_at_period_end", false}}}
        ),
        "A guessed renewal toggle cannot create a billing mutation"
    );
    require(
        route(
            {.section = Section::PaymentSources, .verb = Verb::Update, .resourceId = "9", .values = {{"default", true}}}
        ).body
            == Json({{"default", true}}),
        "Making a saved payment source default never creates a purchase"
    );
    Request payments{.section = Section::Payments, .cursor = "500", .limit = 1};
    require(
        route(payments).path == "/users/@me/billing/payments?limit=1&before=500",
        "Payment history uses bounded cursor pagination"
    );
    const auto page = api::parseUserSettings(
        Json::array(
            {{{"id", "400"},
              {"amount", 999},
              {"currency", "usd"},
              {"payment_source", {{"token", "private-processor-data"}}},
              {"token", "must-never-leak"}}}
        ),
        payments
    );
    require(
        page.more
            && page.cursor == "400"
            && !page.data[0].contains("token")
            && !page.data[0].contains("payment_source"),
        "Payment pagination preserves progress while filtering processor credentials"
    );

    Request gift{.section = Section::GiftCode, .giftCode = "SyntheticGift123"};
    require(
        route(gift).path
                == "/entitlements/gift-codes/SyntheticGift123?with_application=true&with_subscription_plan=true"
            && route(gift).method == "GET",
        "Previewing a gift resolves its product without redeeming it"
    );
    gift.verb = Verb::Create;
    require(!api::userSettingsHttpRequest(gift), "Gift redemption requires an explicit confirmation");
    gift.values = {{"confirm", true}};
    require(
        route(gift).path == "/entitlements/gift-codes/SyntheticGift123/redeem"
            && route(gift).method == "POST"
            && route(gift).body == Json({{"channel_id", nullptr}, {"gateway_checkout_context", nullptr}}),
        "Confirmed gift redemption cannot create a payment-processor authorization"
    );
    auto giftRetry = api::publicUserSettingsRequest(gift);
    giftRetry.mfaChallenge = 11;
    giftRetry.mfaType = "totp";
    giftRetry.mfaCode = "123456";
    require(
        giftRetry.giftCode.empty() && api::sameUserSettingsAction(gift, giftRetry),
        "Opaque verification resumes a worker-held gift without echoing its bearer code"
    );
    giftRetry.giftCode = "DifferentGift";
    require(
        !api::sameUserSettingsAction(gift, giftRetry),
        "A different explicit gift code cannot replace the challenged gift"
    );
    gift.giftCode = "../../users/@me";
    require(!api::userSettingsHttpRequest(gift), "Gift codes cannot escape their fixed entitlement route");
    gift.giftCode = std::string(129, 'x');
    require(!api::userSettingsHttpRequest(gift), "Gift bearer codes are bounded independently of JSON values");
    gift.giftCode = "SyntheticGift123";
    gift.values["payment_source_id"] = "123";
    require(
        !api::userSettingsHttpRequest(gift),
        "Native redemption never guesses payment-source or recurring-payment consent"
    );
    gift.values = Json::object();
    gift.verb = Verb::Get;
    const auto preview =
        api::parseUserSettings(
            {{"code", gift.giftCode},
             {"flags", 1},
             {"uses", 0},
             {"max_uses", 1},
             {"expires_at", nullptr},
             {"store_listing", {{"id", "10"}, {"sku", {{"id", "20"}, {"name", "Nitro"}, {"token", "private"}}}}},
             {"user", {{"id", "30"}, {"username", "gifter"}, {"email", "private"}}},
             {"subscription_plan", {{"id", "40"}, {"interval", 1}, {"interval_count", 1}}},
             {"token", "private"}},
            gift
        )
            .data;
    require(
        preview["requires_payment_source"] == true
            && preview["expires_at"].is_null()
            && preview["store_listing"]["sku"]["name"] == "Nitro"
            && !preview.contains("code")
            && !preview.contains("token")
            && !preview["user"].contains("email")
            && !preview["store_listing"]["sku"].contains("token"),
        "Gift previews expose product and eligibility metadata without bearer codes or private account data"
    );
    gift.verb = Verb::Create;
    const auto redeemed =
        api::parseUserSettings({{"id", "50"}, {"sku_id", "20"}, {"code", "private"}, {"token", "private"}}, gift).data;
    require(
        redeemed["redeemed"] == true
            && redeemed["sku_id"] == "20"
            && !redeemed.contains("code")
            && !redeemed.contains("token"),
        "Gift acknowledgements project only the resulting entitlement and explicit success"
    );
    require(
        api::userGiftCodeBrowserUrl("SyntheticGift123") == "https://discord.gift/SyntheticGift123"
            && api::userGiftCodeBrowserUrl("evil.test/secret").empty(),
        "Payment-required gifts open only validated official gift links"
    );

    Request invoice{.section = Section::PaymentInvoice, .resourceId = "400"};
    require(
        route(invoice).path == "/users/@me/billing/invoice/breakdown?payment_id=400",
        "Invoice retrieval uses the receipt's verified payment identity"
    );
    invoice.resourceId = "400&payment_id=500";
    require(!api::userSettingsHttpRequest(invoice), "Invoice identifiers cannot inject a second billing query");
    invoice.resourceId = "400";
    const auto links = api::parseUserSettings(
                           {{"invoiceLink", "https://invoice.example.test/invoice.pdf?signature=synthetic"},
                            {"refundInvoiceLinks",
                             {"https://invoice.example.test/refund.pdf", "javascript:alert(1)",
                              "https://user:pass@example.test/private", "https://example.test/a\nb"}},
                            {"token", "private"}},
                           invoice
    )
                           .data;
    require(
        links["invoiceLink"].is_string() && links["refundInvoiceLinks"].size() == 1 && !links.contains("token"),
        "Invoice browser handoff accepts only bounded HTTPS links without credentials or control characters"
    );
    require(
        api::userPaymentRefundUrl() == "https://support.discord.com/hc/en-us/requests/new?ticket_form_id=360000118612",
        "Refund requests use the current client's actual support workflow"
    );

    Request receipts{.section = Section::PurchaseHistory, .cursor = "next/page+token=", .limit = 30};
    require(
        route(receipts).path == "/users/@me/billing/history?limit=30&before=next%2Fpage%2Btoken%3D",
        "Current purchase history encodes bounded opaque pagination cursors"
    );
    const auto receiptPage = api::parseUserSettings(
        {{"receipts",
          Json::array(
              {{{"id", "400"},
                {"status", 2},
                {"total", 1000},
                {"currency", "jpy"},
                {"has_invoice_url", true},
                {"payments",
                 Json::array({{{"id", "400"}, {"amount", 1000}, {"payment_source", {{"token", "private"}}}}})},
                {"refund",
                 {{"eligible", true},
                  {"policy", 1},
                  {"window", {{"days", 5}, {"days_since_purchase", 2}}},
                  {"token", "private"}}}}}
          )},
         {"cursor", "next/page+token="}},
        receipts
    );
    require(
        receiptPage.more
            && receiptPage.cursor == "next/page+token="
            && receiptPage.data["receipts"][0]["payments"][0]["id"] == "400"
            && receiptPage.data["receipts"][0]["refund"]["eligible"] == true
            && receiptPage.data["receipts"][0]["total"] == 1000
            && !receiptPage.data["receipts"][0]["refund"].contains("token")
            && !receiptPage.data["receipts"][0]["payments"][0].contains("payment_source"),
        "Receipt history preserves authoritative refund eligibility, currency minor units, and pagination without "
        "processor data"
    );
    require(
        !api::parseUserSettings({{"receipts", Json::array()}, {"cursor", nullptr}}, receipts).more,
        "A null receipt cursor completes pagination even when the requested page size was full"
    );

    Request harvest{
        .section = Section::DataPackage,
        .verb = Verb::Create,
        .values = {{"backends", {"Account", "Messages", "Zendesk"}}}
    };
    require(
        route(harvest).body == harvest.values, "Data requests submit exactly the user-selected current backend names"
    );
    harvest.values["backends"] = {"account"};
    require(!api::userSettingsHttpRequest(harvest), "Data package backend names are case-sensitive protocol values");
    require(
        api::parseUserSettings(nullptr, {.section = Section::DataPackage}).data.is_null(),
        "No previous data request is a valid nullable response"
    );
    require(
        route({.section = Section::EmailChange, .verb = Verb::Create}).method == "PUT",
        "Email changes send the current-email verification step"
    );
    require(
        route({.section = Section::EmailChange, .verb = Verb::Update, .verificationCode = "123456"}).body
            == Json({{"code", "123456"}}),
        "Current-email proof verification uses the confirmed native request schema"
    );
    require(
        api::parseUserSettings(
            {{"token", "private-email-proof"}}, {.section = Section::EmailChange, .verb = Verb::Update}
        )
                .data
            == Json({{"verified", true}}),
        "Email verification tokens remain exclusively on the worker"
    );
    Request phone{
        .section = Section::PhoneVerification,
        .verb = Verb::Create,
        .values = {{"phone", "+12025550100"}},
        .verificationCode = "123456"
    };
    require(
        route(phone).path == "/phone-verifications/verify" && route(phone).body["phone"] == "+12025550100",
        "Phone verification binds the code to the requested number"
    );
    require(
        api::parseUserSettings({{"token", "private-phone-proof"}}, phone).data == Json({{"verified", true}}),
        "Phone proof tokens are never returned in events"
    );

    for (const auto section : {Section::DisableAccount, Section::DeleteAccount}) {
      require(
          !api::userSettingsHttpRequest({.section = section, .verb = Verb::Delete}),
          "Ending an account requires explicit confirmed intent"
      );
      const auto destructive = route(
          {.section = section, .verb = Verb::Delete, .values = {{"confirm", true}}, .password = "synthetic-password"}
      );
      require(
          destructive.endsSession && destructive.method == "POST" && !destructive.body.contains("confirm"),
          "Account-ending actions send only the verified body and identify the logout effect"
      );
    }
    Request backups{.section = Section::BackupCodes, .verb = Verb::Create, .password = "synthetic-password"};
    require(
        route(backups).path == "/auth/verify/view-backup-codes-challenge",
        "Backup codes use current email nonce verification"
    );
    require(
        api::parseUserSettings({{"nonce", "private-view"}, {"regenerate_nonce", "private-reset"}}, backups).data
            == Json({{"challenge_ready", true}}),
        "Backup-code nonces never enter the event-loop state"
    );
    backups.verb = Verb::Update;
    backups.password.clear();
    backups.verificationCode = "verification-key";
    backups.values = {{"regenerate", true}};
    require(
        route(backups).sensitiveResponse && route(backups).body["nonce"].is_null(),
        "A backup regeneration request requires its worker-retained nonce before it can be sent"
    );
    const auto codes =
        api::parseUserSettings(
            {{"backup_codes", Json::array({{{"code", "12345678"}, {"consumed", false}}})}, {"token", "private"}},
            backups
        )
            .data;
    require(
        codes["backup_codes"][0]["code"] == "12345678" && !codes.contains("token"),
        "Only intentionally revealed backup codes enter the ephemeral sensitive result"
    );
    const auto secret = api::generateAuthenticatorSecret();
    require(
        secret.size() == 32
            && secret != api::generateAuthenticatorSecret()
            && std::ranges::all_of(secret, [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'); }),
        "Authenticator enrollment uses fresh cryptographic base32 secret material"
    );
    Request totp{
        .section = Section::Authenticator,
        .verb = Verb::Create,
        .verificationCode = "123456",
        .authenticatorSecret = secret
    };
    require(
        route(totp).mayRotateToken && route(totp).body["secret"] == secret,
        "TOTP enrollment submits the generated secret and recognizes token rotation"
    );
    const auto sanitized = api::publicUserSettingsRequest(totp);
    require(
        sanitized.authenticatorSecret.empty() && sanitized.verificationCode.empty(),
        "Authenticator secrets never survive in the public request echo"
    );
    require(
        !api::userSettingsHttpRequest({.section = Section::SecurityKeys, .verb = Verb::Create}),
        "A passkey cannot be fabricated without native platform WebAuthn support"
    );
    require(
        api::userSettingsBrowserUrl(Section::SecurityKeys) == "https://discord.com/settings/account",
        "Unsupported platform enrollment opens the actual verified Discord account route"
    );

    const auto self = api::parseUserSettings(
                          {{"id", "123"},
                           {"username", "fen"},
                           {"email", "fen@example.com"},
                           {"phone", nullptr},
                           {"mfa_enabled", true},
                           {"flags", 16},
                           {"token", "private-token"},
                           {"auth", {{"session_id_hash", "private-session"}}},
                           {"analytics_token", "private-analytics"}},
                          {}
    )
                          .data;
    require(
        self["mfa_sms"] == true
            && self["phone"].is_null()
            && self["email"] == "fen@example.com"
            && !self.contains("token")
            && !self.contains("auth")
            && !self.contains("analytics_token"),
        "Private account information is available without exposing authentication or analytics credentials"
    );
    Request readProfile{.section = Section::Profile, .resourceId = "123"};
    const auto projected =
        api::parseUserSettings(
            {{"user", {{"id", "123"}, {"username", "fen"}, {"email", "private"}, {"token", "private"}}},
             {"user_profile",
              {{"bio", "About me"},
               {"pronouns", "they/them"},
               {"collectibles", Json::array({{{"sku_id", "100"}, {"type", 1}, {"token", "private"}}})}}}},
            readProfile
        )
            .data;
    require(
        projected["bio"] == "About me"
            && projected["collectibles"][0]["sku_id"] == "100"
            && !projected["user"].contains("email")
            && !projected["collectibles"][0].contains("token"),
        "Profile snapshots retain current cosmetic SKUs for unknown-preserving edits without private user fields"
    );
    const auto owned =
        api::parseUserSettings(
            Json::array(
                {{{"sku_id", "500"},
                  {"type", 1000},
                  {"bundled_products",
                   Json::array(
                       {{{"sku_id", "501"},
                         {"items",
                          Json::array(
                              {{{"sku_id", "502"}, {"type", 0}, {"asset", "hash"}, {"access_token", "private"}}}
                          )}}}
                   )}}}
            ),
            {.section = Section::Collectibles}
        )
            .data;
    require(
        owned[0]["bundled_products"][0]["items"][0]["sku_id"] == "502"
            && !owned[0]["bundled_products"][0]["items"][0].contains("access_token"),
        "Native owned-cosmetic pickers retain actual item SKUs through bundles without exposing unknown fields"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
