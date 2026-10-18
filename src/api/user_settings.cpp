#include "api/user_settings.h"

#include "api/models.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace api {
  namespace {
    using Json = nlohmann::json;
    using Section = UserSettingsSection;
    using Verb = UserSettingsVerb;

    bool keys(const Json& value, std::initializer_list<std::string_view> allowed) {
      if (!value.is_object())
        return false;
      return std::ranges::all_of(value.items(), [&](const auto& item) {
        return std::ranges::find(allowed, item.key()) != allowed.end();
      });
    }
    bool string(const Json& value, std::size_t maximum, bool nullable = false, std::size_t minimum = 0) {
      return (nullable && value.is_null())
          || (value.is_string()
              && value.get_ref<const std::string&>().size() <= maximum * 4
              && utf8Length(value.get_ref<const std::string&>()) <= maximum
              && utf8Length(value.get_ref<const std::string&>()) >= minimum);
    }
    bool opaque(const std::string& value, std::size_t maximum = 256) {
      return !value.empty()
          && value.size() <= maximum
          && std::ranges::all_of(
              value, [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; }
          )
          && value != "."
          && value != "..";
    }
    bool cursorToken(const std::string& value) {
      return !value.empty()
          && value.size() <= 512
          && std::ranges::all_of(value, [](unsigned char c) { return c > 32 && c < 127; });
    }
    bool giftCode(const std::string& value) {
      return !value.empty() && value.size() <= 128 && std::ranges::all_of(value, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
      });
    }
    bool httpsUrl(const Json& value) {
      if (!value.is_string())
        return false;
      const auto& url = value.get_ref<const std::string&>();
      if (!url.starts_with("https://") || url.size() > 8192 || std::ranges::any_of(url, [](unsigned char c) {
            return c <= 32 || c == 127 || c == '\\';
          }))
        return false;
      const auto end = url.find_first_of("/?#", 8);
      const auto authority = std::string_view(url).substr(8, end == std::string::npos ? end : end - 8);
      return !authority.empty()
          && authority.size() <= 259
          && authority.find('@') == std::string_view::npos
          && std::ranges::all_of(authority, [](unsigned char c) {
               return std::isalnum(c) || c == '.' || c == '-' || c == ':';
             });
    }
    std::string segment(const std::string& value) {
      constexpr char hex[] = "0123456789ABCDEF";
      std::string result;
      for (unsigned char c : value)
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.')
          result += static_cast<char>(c);
        else {
          result += '%';
          result += hex[c >> 4];
          result += hex[c & 15];
        }
      return result;
    }
    bool number(const Json& value, std::int64_t minimum, std::int64_t maximum, bool nullable = false) {
      if (nullable && value.is_null())
        return true;
      if (!value.is_number_integer())
        return false;
      if (value.is_number_unsigned())
        return value.get<std::uint64_t>() >= static_cast<std::uint64_t>(minimum)
            && value.get<std::uint64_t>() <= static_cast<std::uint64_t>(maximum);
      const auto n = value.get<std::int64_t>();
      return n >= minimum && n <= maximum;
    }
    bool image(const Json& value) {
      if (value.is_null())
        return true;
      if (!value.is_string())
        return false;
      const auto& data = value.get_ref<const std::string&>();
      if (data.size() > 14 * 1024 * 1024)
        return false;
      const auto comma = data.find(',');
      if (comma == std::string::npos)
        return false;
      const auto prefix = std::string_view(data).substr(0, comma);
      if (prefix != "data:image/png;base64"
          && prefix != "data:image/jpeg;base64"
          && prefix != "data:image/gif;base64"
          && prefix != "data:image/webp;base64")
        return false;
      const auto encoded = std::string_view(data).substr(comma + 1);
      if (encoded.empty() || encoded.size() % 4 || encoded.size() / 4 * 3 > 10 * 1024 * 1024 + 2)
        return false;
      const auto padding = encoded.ends_with("==") ? 2u : encoded.ends_with('=') ? 1u : 0u;
      if (encoded.size() / 4 * 3 - padding > 10 * 1024 * 1024)
        return false;
      return std::ranges::all_of(encoded.substr(0, encoded.size() - padding), [](unsigned char c) {
        return std::isalnum(c) || c == '+' || c == '/';
      });
    }
    bool id(const Json& value, bool nullable = false) {
      return (nullable && value.is_null()) || (value.is_string() && validSnowflake(value.get<std::string>()));
    }
    bool colorPair(const Json& value) {
      return value.is_null()
          || (value.is_array() && value.size() == 2 && std::ranges::all_of(value, [](const auto& item) {
                return number(item, 0, 0xFFFFFF);
              }));
    }
    bool service(const std::string& value) {
      // These are fixed documented connection types, not arbitrary route names.
      constexpr std::string_view services[] = {
          "amazon-music", "battlenet", "bluesky", "bungie",    "crunchyroll",     "domain",   "ebay",
          "epicgames",    "facebook",  "github",  "instagram", "leagueoflegends", "mastodon", "paypal",
          "playstation",  "reddit",    "roblox",  "samsung",   "skype",           "spotify",  "steam",
          "tiktok",       "twitch",    "twitter", "xbox",      "youtube"
      };
      return std::ranges::find(services, value) != std::end(services);
    }
    Json selected(const Json& value, std::initializer_list<const char*> names, std::size_t textLimit = 4096) {
      auto result = Json::object();
      if (!value.is_object())
        return result;
      for (const auto* name : names) {
        const auto at = value.find(name);
        if (at == value.end())
          continue;
        if (at->is_null()
            || at->is_boolean()
            || at->is_number_integer()
            || (at->is_string() && at->get_ref<const std::string&>().size() <= textLimit))
          result[name] = *at;
      }
      return result;
    }
    Json account(const Json& value, bool privateFields = false) {
      auto result = selected(
          value,
          {"id", "username", "discriminator", "global_name", "avatar", "banner", "bio", "pronouns", "accent_color",
           "premium_type", "public_flags", "bot", "system"}
      );
      if (privateFields)
        result.update(selected(
            value,
            {"email", "phone", "verified", "mfa_enabled", "flags", "locale", "premium_since", "purchased_flags",
             "nsfw_allowed"}
        ));
      if (privateFields && value.contains("flags") && number(value["flags"], 0, INT64_MAX))
        result["mfa_sms"] = (value["flags"].get<std::uint64_t>() & 16) != 0;
      for (const auto* name : {"primary_guild", "avatar_decoration_data", "display_name_styles"})
        if (value.contains(name) && value[name].is_object())
          result[name] = selected(
              value[name],
              {"identity_guild_id", "identity_enabled", "tag", "badge", "sku_id", "asset", "font_id", "effect_id"}, 256
          );
      if (value.contains("collectibles") && value["collectibles"].is_object()) {
        const auto plate = value["collectibles"].find("nameplate");
        if (plate != value["collectibles"].end())
          result["collectibles"]["nameplate"] =
              plate->is_null() ? Json(nullptr) : selected(*plate, {"sku_id", "asset", "label", "palette"}, 512);
      }
      if (result.contains("id") && id(result["id"])) {
        const auto user = result["id"].get<std::string>();
        for (const auto* field : {"avatar", "banner"})
          if (result.contains(field) && result[field].is_string()) {
            const auto hash = result[field].get<std::string>();
            if (validAssetHash(hash))
              result[std::string(field) + "_url"] =
                  std::string(field == std::string_view("avatar") ? "/avatars/" : "/banners/")
                  + user
                  + "/"
                  + hash
                  + (hash.starts_with("a_") ? ".gif?size=256" : ".png?size=256");
          }
      }
      return result;
    }
    Json profile(const Json& value, const UserSettingsRequest& request) {
      auto result = value.contains("user") ? account(value["user"]) : Json::object();
      const bool guild = request.section == Section::GuildProfile;
      const auto* field = guild ? "guild_member_profile" : "user_profile";
      const auto& metadata = value.contains(field) && value[field].is_object() ? value[field] : value;
      result.update(selected(
          metadata,
          {"guild_id", "user_id", "bio", "pronouns", "banner", "accent_color", "popout_animation_particle_type",
           "profile_visibility"},
          16000
      ));
      if (metadata.contains("collectibles")
          && metadata["collectibles"].is_array()
          && metadata["collectibles"].size() <= 16) {
        result["collectibles"] = Json::array();
        for (const auto& item : metadata["collectibles"])
          result["collectibles"].push_back(selected(item, {"sku_id", "type", "asset", "label", "palette"}, 512));
      }
      if (metadata.contains("theme_colors") && colorPair(metadata["theme_colors"]))
        result["theme_colors"] = metadata["theme_colors"];
      if (value.contains("user"))
        result["user"] = account(value["user"]);
      if (value.contains("guild_member") && value["guild_member"].is_object()) {
        result["guild_member"] = selected(value["guild_member"], {"nick", "avatar", "banner", "joined_at"});
        if (value["guild_member"].contains("user"))
          result["guild_member"]["user"] = account(value["guild_member"]["user"]);
      }
      const auto banner = metadata.find("banner");
      if (banner != metadata.end()
          && banner->is_string()
          && validAssetHash(banner->get<std::string>())
          && validSnowflake(request.resourceId)) {
        const auto hash = banner->get<std::string>();
        result["banner_url"] = (guild ? "/guilds/" + request.guildId + "/users/" + request.resourceId + "/banners/"
                                      : "/banners/" + request.resourceId + "/")
            + hash
            + (hash.starts_with("a_") ? ".gif?size=512" : ".png?size=512");
      }
      return result;
    }
    Json cosmetic(const Json& value, unsigned depth = 0) {
      auto result = selected(
          value,
          {"sku_id", "name", "title", "description", "type", "premium_type", "category_sku_id", "asset", "label",
           "palette", "purchased_at", "expires_at", "purchase_type", "base_variant_name", "base_variant_sku_id",
           "variant_label", "variant_value"},
          1024
      );
      if (depth < 4)
        for (const auto* key : {"items", "bundled_products", "variants"})
          if (value.contains(key) && value[key].is_array() && value[key].size() <= 256) {
            result[key] = Json::array();
            for (const auto& item : value[key])
              result[key].push_back(cosmetic(item, depth + 1));
          }
      return result;
    }
    Json entitlement(const Json& value) {
      auto result = selected(
          value,
          {"id", "sku_id", "application_id", "type", "deleted", "consumed", "starts_at", "ends_at", "guild_id",
           "user_id"},
          256
      );
      if (value.contains("sku"))
        result["sku"] = selected(value["sku"], {"id", "name", "application_id", "type"}, 256);
      if (value.contains("application"))
        result["application"] = selected(value["application"], {"id", "name", "icon"}, 256);
      return result;
    }
    Json payment(const Json& value) {
      auto result = selected(
          value,
          {"id",
           "amount",
           "amount_refunded",
           "currency",
           "description",
           "status",
           "created_at",
           "sku_id",
           "subscription_id",
           "tax",
           "tax_inclusive",
           "disputed",
           "refundable",
           "gift",
           "payment_source_id",
           "payment_gateway",
           "has_invoice_url",
           "has_refund_invoice_urls",
           "downloadable_invoice",
           "flags"},
          1024
      );
      if (value.contains("sku"))
        result["sku"] = selected(value["sku"], {"id", "name", "application_id", "type"}, 256);
      if (value.contains("premium_refund_disqualification_reasons")
          && value["premium_refund_disqualification_reasons"].is_array()
          && value["premium_refund_disqualification_reasons"].size() <= 64) {
        result["premium_refund_disqualification_reasons"] = Json::array();
        for (const auto& reason : value["premium_refund_disqualification_reasons"])
          if (string(reason, 128) || number(reason, 0, INT32_MAX))
            result["premium_refund_disqualification_reasons"].push_back(reason);
      }
      return result;
    }
  } // namespace

  std::optional<UserSettingsHttpRequest> userSettingsHttpRequest(const UserSettingsRequest& request) try {
    if (!request.values.is_object()
        || request.values.size() > 32
        || request.limit < 1
        || request.limit > 100
        || (!request.cursor.empty()
            && (request.section == Section::PurchaseHistory ? !cursorToken(request.cursor)
                                                            : !validSnowflake(request.cursor)))
        || request.password.size() > 1024
        || request.newPassword.size() > 1024
        || request.verificationCode.size() > 256
        || request.authenticatorSecret.size() > 128
        || request.giftCode.size() > 128
        || request.mfaCode.size() > 1024
        || request.values.dump().size() > 16 * 1024 * 1024)
      return {};
    UserSettingsHttpRequest result;
    const auto& values = request.values;
    const auto mutation = [&](const char* method, const std::string& path) {
      result.method = method;
      result.path = path;
      result.body = values;
    };
    switch (request.section) {
    case Section::Account:
      result.path = "/users/@me";
      if (request.verb == Verb::Get) {
        if (!values.empty())
          return {};
      } else if (request.verb == Verb::Update) {
        if (values.empty()
            || !keys(
                values,
                {"username", "global_name", "email", "avatar", "avatar_description", "avatar_decoration_sku_id",
                 "nameplate_sku_id", "primary_guild_id", "display_name_font_id", "display_name_effect_id",
                 "display_name_colors"}
            ))
          return {};
        for (const auto& [key, value] : values.items()) {
          if (key == "username") {
            if (!string(value, 32, false, 2))
              return {};
            const auto name = value.get<std::string>();
            if (name.find("..") != std::string::npos
                || name.find("discord") != std::string::npos
                || name == "everyone"
                || name == "here"
                || !std::ranges::all_of(name, [](unsigned char c) {
                     return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
                   }))
              return {};
          } else if (key == "global_name") {
            if (!string(value, 32, true))
              return {};
          } else if (key == "email") {
            if (!string(value, 320, false, 3))
              return {};
            const auto email = value.get<std::string>();
            if (email.find('@') == std::string::npos
                || std::ranges::any_of(email, [](unsigned char c) { return c <= 32 || c == 127; }))
              return {};
          } else if (key == "avatar") {
            if (!image(value))
              return {};
          } else if (key == "avatar_description") {
            if (!string(value, 1024, true))
              return {};
          } else if (key == "display_name_font_id" || key == "display_name_effect_id") {
            if (!number(value, 0, 1000, true))
              return {};
          } else if (key == "display_name_colors") {
            if (!value.is_null()
                && (!value.is_array()
                    || value.empty()
                    || value.size() > 3
                    || !std::ranges::all_of(value, [](const auto& color) { return number(color, 0, 0xFFFFFF); })))
              return {};
          } else if (!id(value, true))
            return {};
        }
        mutation("PATCH", result.path);
        if (!request.password.empty())
          result.body["password"] = request.password;
        // The worker attaches its current-email proof; it is never UI input.
        if (values.contains("email"))
          result.body["email_token"] = nullptr;
        result.mayRotateToken = true;
      } else
        return {};
      break;
    case Section::Password:
      if (request.verb != Verb::Update || !values.empty() || request.password.empty() || request.newPassword.empty())
        return {};
      mutation("PATCH", "/users/@me");
      result.body = {{"password", request.password}, {"new_password", request.newPassword}};
      result.mayRotateToken = true;
      break;
    case Section::PasswordRecovery:
      if (request.verb != Verb::Create
          || !keys(values, {"login"})
          || !values.contains("login")
          || !string(values["login"], 320, false, 2))
        return {};
      mutation("POST", "/auth/forgot");
      break;
    case Section::Profile:
    case Section::GuildProfile:
      if (request.section == Section::GuildProfile && !validSnowflake(request.guildId))
        return {};
      if (request.verb == Verb::Get) {
        if (!validSnowflake(request.resourceId) || !values.empty())
          return {};
        result.path = "/users/"
            + request.resourceId
            + "/profile"
            + (request.section == Section::GuildProfile ? "?guild_id=" + request.guildId : "");
      } else if (request.verb == Verb::Update) {
        if (values.empty()
            || !keys(
                values,
                {"bio", "pronouns", "banner", "accent_color", "theme_colors", "collectibles_sku_ids",
                 "profile_visibility"}
            ))
          return {};
        for (const auto& [key, value] : values.items()) {
          if (key == "bio") {
            if (!string(value, 4000, true))
              return {};
          } else if (key == "pronouns") {
            if (!string(value, 40, true))
              return {};
          } else if (key == "banner") {
            if (!image(value))
              return {};
          } else if (key == "accent_color") {
            if (!number(value, 0, 0xFFFFFF, true))
              return {};
          } else if (key == "theme_colors") {
            if (!colorPair(value))
              return {};
          } else if (key == "collectibles_sku_ids") {
            if (!value.is_array() || value.size() > 16)
              return {};
            std::set<std::string> unique;
            for (const auto& sku : value)
              if (!id(sku) || !unique.insert(sku.get<std::string>()).second)
                return {};
          } else if (key == "profile_visibility") {
            if (!number(value, 0, 3))
              return {};
          }
        }
        mutation(
            "PATCH",
            request.section == Section::GuildProfile ? "/guilds/" + request.guildId + "/profile/@me"
                                                     : "/users/@me/profile"
        );
      } else
        return {};
      break;
    case Section::GuildMember:
      if (!validSnowflake(request.guildId))
        return {};
      if (request.verb == Verb::Get) {
        if (!validSnowflake(request.resourceId) || !values.empty())
          return {};
        result.path = "/guilds/" + request.guildId + "/members/" + request.resourceId;
      } else if (request.verb == Verb::Update) {
        if (values.empty()
            || !keys(
                values,
                {"nick", "avatar", "avatar_decoration_sku_id", "collectibles", "display_name_font_id",
                 "display_name_effect_id", "display_name_colors"}
            ))
          return {};
        for (const auto& [key, value] : values.items()) {
          if (key == "nick") {
            if (!string(value, 32, true))
              return {};
          } else if (key == "avatar") {
            if (!image(value))
              return {};
          } else if (key == "avatar_decoration_sku_id") {
            if (!id(value, true))
              return {};
          } else if (key == "collectibles") {
            if (!keys(value, {"nameplate"}) || !value.contains("nameplate"))
              return {};
            const auto& plate = value["nameplate"];
            if (!plate.is_null() && (!keys(plate, {"sku_id"}) || !plate.contains("sku_id") || !id(plate["sku_id"])))
              return {};
          } else if (key == "display_name_colors") {
            if (!colorPair(value))
              return {};
          } else if (!number(value, 0, 1000, true))
            return {};
        }
        mutation("PATCH", "/guilds/" + request.guildId + "/members/@me");
      } else
        return {};
      break;
    case Section::Sessions:
      if (request.verb == Verb::Get && values.empty())
        result.path = "/auth/sessions";
      else if (
          request.verb == Verb::Delete
          && keys(values, {"session_id_hashes"})
          && values.contains("session_id_hashes")
          && values["session_id_hashes"].is_array()
          && !values["session_id_hashes"].empty()
          && values["session_id_hashes"].size() <= 100
          && std::ranges::all_of(values["session_id_hashes"], [](const auto& item) {
               return item.is_string() && opaque(item.template get<std::string>(), 128);
             })
      )
        mutation("POST", "/auth/sessions/logout");
      else
        return {};
      break;
    case Section::AuthorizedApps:
      if (request.verb == Verb::Get && values.empty())
        result.path = "/oauth2/tokens";
      else if (request.verb == Verb::Delete && validSnowflake(request.resourceId) && values.empty())
        mutation("DELETE", "/oauth2/tokens/" + request.resourceId);
      else
        return {};
      break;
    case Section::Connections:
    case Section::ConnectionRefresh:
    case Section::ConnectionAuthorize:
      if (request.section == Section::Connections && request.verb == Verb::Get && values.empty()) {
        result.path = "/users/@me/connections";
        break;
      }
      if (!service(request.service))
        return {};
      if (request.section == Section::ConnectionAuthorize) {
        if (request.verb != Verb::Get || !values.empty())
          return {};
        result.path = "/connections/" + request.service + "/authorize";
      } else {
        if (request.resourceId.empty()
            || request.resourceId.size() > 256
            || std::ranges::any_of(request.resourceId, [](unsigned char c) { return c < 32 || c == 127; }))
          return {};
        result.path = "/users/@me/connections/" + request.service + "/" + segment(request.resourceId);
        if (request.section == Section::ConnectionRefresh) {
          if (request.verb != Verb::Create || !values.empty())
            return {};
          mutation("POST", result.path + "/refresh");
        } else if (request.verb == Verb::Delete && values.empty())
          mutation("DELETE", result.path);
        else if (
            request.verb == Verb::Update
            && !values.empty()
            && keys(values, {"visibility", "metadata_visibility", "friend_sync", "show_activity"})
            && std::ranges::all_of(values, [](const auto& item) { return item.is_boolean(); })
        )
          mutation("PATCH", result.path);
        else
          return {};
      }
      break;
    case Section::Relationships:
      result.path = "/users/@me/relationships";
      if (request.verb == Verb::Get && values.empty())
        break;
      if (request.verb == Verb::Create
          && keys(values, {"username", "discriminator"})
          && values.contains("username")
          && string(values["username"], 32, false, 2)
          && (!values.contains("discriminator") || number(values["discriminator"], 0, 9999)))
        mutation("POST", result.path);
      else if (validSnowflake(request.resourceId)) {
        result.path += "/" + request.resourceId;
        if (request.verb == Verb::Delete && values.empty())
          mutation("DELETE", result.path);
        else if (
            request.verb == Verb::Update
            && keys(values, {"user_ignored"})
            && values.contains("user_ignored")
            && values["user_ignored"].is_boolean()
        )
          mutation(values["user_ignored"] == true ? "PUT" : "DELETE", result.path + "/ignore");
        else if (
            request.verb == Verb::Update
            && keys(values, {"type"})
            && values.contains("type")
            && number(values["type"], 1, 2)
        )
          mutation("PUT", result.path);
        else if (
            request.verb == Verb::Update
            && keys(values, {"nickname"})
            && values.contains("nickname")
            && string(values["nickname"], 32, true)
        )
          mutation("PATCH", result.path);
        else
          return {};
      } else
        return {};
      break;
    case Section::Subscriptions:
      if (request.verb == Verb::Get
          && keys(values, {"include_inactive"})
          && (!values.contains("include_inactive") || values["include_inactive"].is_boolean()))
        result.path = "/users/@me/billing/subscriptions"
            + std::string(values.value("include_inactive", false) ? "?include_inactive=true" : "");
      else if (request.verb == Verb::Delete && validSnowflake(request.resourceId) && values.empty())
        mutation("DELETE", "/users/@me/billing/subscriptions/" + request.resourceId);
      else
        return {};
      break;
    case Section::Payments:
      if (request.verb != Verb::Get || !values.empty())
        return {};
      result.path = "/users/@me/billing/payments?limit="
          + std::to_string(request.limit)
          + (request.cursor.empty() ? "" : "&before=" + request.cursor);
      break;
    case Section::PurchaseHistory:
      if (request.verb != Verb::Get || !values.empty())
        return {};
      result.path = "/users/@me/billing/history?limit="
          + std::to_string(request.limit)
          + (request.cursor.empty() ? "" : "&before=" + segment(request.cursor));
      break;
    case Section::PaymentInvoice:
      if (request.verb != Verb::Get || !values.empty() || !validSnowflake(request.resourceId))
        return {};
      result.path = "/users/@me/billing/invoice/breakdown?payment_id=" + request.resourceId;
      break;
    case Section::GiftCode:
      if (!giftCode(request.giftCode)
          || !request.resourceId.empty()
          || !request.guildId.empty()
          || !request.service.empty())
        return {};
      result.path = "/entitlements/gift-codes/" + request.giftCode;
      if (request.verb == Verb::Get && values.empty())
        result.path += "?with_application=true&with_subscription_plan=true";
      else if (request.verb == Verb::Create && keys(values, {"confirm"}) && values.value("confirm", Json{}) == true) {
        mutation("POST", result.path + "/redeem");
        result.body = {{"channel_id", nullptr}, {"gateway_checkout_context", nullptr}};
      } else
        return {};
      break;
    case Section::PaymentSources:
      result.path = "/users/@me/billing/payment-sources";
      if (request.verb == Verb::Get && values.empty())
        break;
      if (!validSnowflake(request.resourceId))
        return {};
      result.path += "/" + request.resourceId;
      if (request.verb == Verb::Delete && values.empty())
        mutation("DELETE", result.path);
      else if (request.verb == Verb::Update && keys(values, {"default"}) && values.value("default", Json{}) == true)
        mutation("PATCH", result.path);
      else
        return {};
      break;
    case Section::Entitlements:
      if (request.verb != Verb::Get || !values.empty())
        return {};
      result.path = "/users/@me/entitlements?with_sku=true&with_application=true&exclude_ended=false";
      break;
    case Section::Collectibles:
      if (request.verb != Verb::Get || !values.empty())
        return {};
      result.path = "/users/@me/collectibles-purchases";
      break;
    case Section::DataPackage:
      result.path = "/users/@me/harvest";
      if (request.verb == Verb::Get && values.empty())
        break;
      if (request.verb != Verb::Create
          || !keys(values, {"backends"})
          || !values.contains("backends")
          || !values["backends"].is_array()
          || values["backends"].empty()
          || values["backends"].size() > 7)
        return {};
      {
        const std::set<Json> allowed{"Account", "Analytics", "Activities", "Ads", "Messages", "Servers", "Zendesk"};
        std::set<Json> unique;
        for (const auto& backend : values["backends"])
          if (!allowed.contains(backend) || !unique.insert(backend).second)
            return {};
      }
      mutation("POST", result.path);
      break;
    case Section::EmailVerification:
      if (request.verb != Verb::Create || !values.empty())
        return {};
      mutation("POST", "/auth/verify/resend");
      break;
    case Section::EmailChange:
      if (!values.empty())
        return {};
      if (request.verb == Verb::Create)
        mutation("PUT", "/users/@me/email");
      else if (request.verb == Verb::Update && !request.verificationCode.empty()) {
        mutation("POST", "/users/@me/email/verify-code");
        result.body = {{"code", request.verificationCode}};
      } else
        return {};
      break;
    case Section::Phone:
    case Section::PhoneVerification:
    case Section::PhoneResend:
      if (request.section == Section::Phone && request.verb == Verb::Delete && values.empty()) {
        mutation("DELETE", "/users/@me/phone");
        result.body = {{"password", request.password}, {"change_phone_reason", "user_settings_update"}};
      } else {
        if (!keys(values, {"phone"}) || !values.contains("phone") || !values["phone"].is_string())
          return {};
        const auto phone = values["phone"].get<std::string>();
        if (phone.size() < 7
            || phone.size() > 32
            || phone.front() != '+'
            || !std::ranges::all_of(std::string_view(phone).substr(1), [](unsigned char c) {
                 return c >= '0' && c <= '9';
               }))
          return {};
        if (request.section == Section::Phone && request.verb == Verb::Create) {
          mutation("POST", "/users/@me/phone");
          result.body = {{"phone", phone}, {"change_phone_reason", "user_settings_update"}};
        } else if (request.section == Section::Phone && request.verb == Verb::Update) {
          mutation("POST", "/users/@me/phone");
          result.body = {
              {"phone_token", nullptr}, {"password", request.password}, {"change_phone_reason", "user_settings_update"}
          };
        } else if (
            request.section == Section::PhoneVerification
            && request.verb == Verb::Create
            && !request.verificationCode.empty()
        ) {
          mutation("POST", "/phone-verifications/verify");
          result.body = {{"phone", phone}, {"code", request.verificationCode}};
        } else if (request.section == Section::PhoneResend && request.verb == Verb::Create) {
          mutation("POST", "/phone-verifications/resend");
          result.body = {{"phone", phone}};
        } else
          return {};
      }
      break;
    case Section::DisableAccount:
    case Section::DeleteAccount:
      if (request.verb != Verb::Delete || !keys(values, {"confirm"}) || values.value("confirm", Json{}) != true)
        return {};
      mutation("POST", request.section == Section::DeleteAccount ? "/users/@me/delete" : "/users/@me/disable");
      result.body = {{"password", request.password}};
      result.endsSession = true;
      break;
    case Section::BackupCodes:
      if (request.verb == Verb::Create && values.empty()) {
        mutation("POST", "/auth/verify/view-backup-codes-challenge");
        result.body = {{"password", request.password}};
      } else if (
          request.verb == Verb::Update
          && keys(values, {"regenerate"})
          && !request.verificationCode.empty()
          && values.contains("regenerate")
          && values["regenerate"].is_boolean()
      ) {
        mutation("POST", "/users/@me/mfa/codes-verification");
        result.body = {{"key", request.verificationCode}, {"nonce", nullptr}, {"regenerate", values["regenerate"]}};
        result.sensitiveResponse = true;
      } else
        return {};
      break;
    case Section::Authenticator:
      if (!values.empty())
        return {};
      if (request.verb == Verb::Create) {
        const auto& secret = request.authenticatorSecret;
        if (secret.size() < 16
            || secret.size() > 128
            || !std::ranges::all_of(
                secret, [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'); }
            )
            || request.verificationCode.size() != 6
            || !std::ranges::all_of(request.verificationCode, [](unsigned char c) { return c >= '0' && c <= '9'; }))
          return {};
        mutation("POST", "/users/@me/mfa/totp/enable");
        result.body = {{"code", request.verificationCode}, {"secret", secret}};
        result.sensitiveResponse = true;
      } else if (request.verb == Verb::Delete)
        mutation("POST", "/users/@me/mfa/totp/disable");
      else
        return {};
      result.mayRotateToken = true;
      break;
    case Section::SmsAuthentication:
      if (!values.empty())
        return {};
      if (request.verb == Verb::Create)
        mutation("POST", "/users/@me/mfa/sms/enable");
      else if (request.verb == Verb::Delete) {
        mutation("POST", "/users/@me/mfa/sms/disable");
        result.body = {{"password", request.password}};
      } else
        return {};
      break;
    case Section::SecurityKeys:
      result.path = "/users/@me/mfa/webauthn/credentials";
      if (request.verb == Verb::Get && values.empty())
        break;
      if (!opaque(request.resourceId, 128))
        return {};
      result.path += "/" + segment(request.resourceId);
      if (request.verb == Verb::Delete && values.empty())
        mutation("DELETE", result.path);
      else if (
          request.verb == Verb::Update
          && keys(values, {"name"})
          && values.contains("name")
          && string(values["name"], 100, false, 1)
      )
        mutation("PATCH", result.path);
      else
        return {};
      break;
    }
    return result.path.empty() ? std::nullopt : std::optional(std::move(result));
  } catch (const std::exception&) {
    return {};
  }

  UserSettingsResult parseUserSettings(const Json& value, const UserSettingsRequest& request) {
    UserSettingsResult result;
    const auto array = [&](const Json& input, auto project) {
      if (!input.is_array() || input.size() > 10000)
        throw std::runtime_error("Invalid account settings list");
      auto output = Json::array();
      for (const auto& item : input)
        output.push_back(project(item));
      return output;
    };
    switch (request.section) {
    case Section::Account:
    case Section::Password:
      result.data = account(value, true);
      break;
    case Section::PasswordRecovery:
      result.data = selected(value, {"method"}, 64);
      result.data["requested"] = true;
      break;
    case Section::Profile:
    case Section::GuildProfile:
      result.data = profile(value, request);
      break;
    case Section::GuildMember:
      result.data = selected(value, {"nick", "avatar", "banner", "joined_at", "guild_id"});
      if (value.contains("user"))
        result.data["user"] = account(value["user"]);
      if (value.contains("avatar_decoration_data"))
        result.data["avatar_decoration_data"] = selected(value["avatar_decoration_data"], {"sku_id", "asset"}, 512);
      if (value.contains("collectibles")
          && value["collectibles"].is_object()
          && value["collectibles"].contains("nameplate"))
        result.data["collectibles"]["nameplate"] = value["collectibles"]["nameplate"].is_null()
            ? Json(nullptr)
            : selected(value["collectibles"]["nameplate"], {"sku_id", "asset", "label", "palette"}, 512);
      break;
    case Section::Sessions:
      if (request.verb == Verb::Get) {
        result.data["user_sessions"] = array(value.at("user_sessions"), [&](const Json& item) {
          auto row = selected(item, {"id_hash", "approx_last_used_time", "current_session"}, 256);
          if (item.contains("client_info"))
            row["client_info"] = selected(item["client_info"], {"os", "platform", "location", "browser"}, 256);
          return row;
        });
      }
      break;
    case Section::AuthorizedApps:
      if (request.verb == Verb::Get)
        result.data = array(value, [&](const Json& item) {
          auto row = selected(item, {"id", "created_at", "expires", "two_way_link"}, 256);
          if (item.contains("application"))
            row["application"] = selected(
                item["application"],
                {"id", "name", "description", "icon", "bot_public", "bot_require_code_grant", "terms_of_service_url",
                 "privacy_policy_url"}
            );
          if (item.contains("scopes") && item["scopes"].is_array() && item["scopes"].size() <= 128) {
            row["scopes"] = Json::array();
            for (const auto& scope : item["scopes"])
              if (string(scope, 128))
                row["scopes"].push_back(scope);
          }
          return row;
        });
      break;
    case Section::Connections:
      if (request.verb == Verb::Get)
        result.data = array(value, [&](const Json& item) {
          return selected(
              item,
              {"id", "type", "name", "verified", "revoked", "visibility", "metadata_visibility", "friend_sync",
               "show_activity", "two_way_link"},
              1024
          );
        });
      else
        result.data = selected(
            value,
            {"id", "type", "name", "verified", "revoked", "visibility", "metadata_visibility", "friend_sync",
             "show_activity", "two_way_link"}
        );
      break;
    case Section::ConnectionAuthorize:
      if (value.contains("url") && value["url"].is_string()) {
        const auto url = value["url"].get<std::string>();
        if (url.starts_with("https://") && url.size() <= 8192 && url.find_first_of("\r\n") == std::string::npos)
          result.data["url"] = url;
      }
      break;
    case Section::Relationships:
      if (request.verb == Verb::Get)
        result.data = array(value, [&](const Json& item) {
          auto row = selected(item, {"id", "type", "nickname", "since", "user_ignored"}, 256);
          if (item.contains("user"))
            row["user"] = account(item["user"]);
          return row;
        });
      else {
        result.data = selected(value, {"id", "type", "nickname", "since"}, 256);
        if (value.contains("user"))
          result.data["user"] = account(value["user"]);
      }
      break;
    case Section::Subscriptions:
      if (request.verb == Verb::Get)
        result.data = array(value, [&](const Json& item) {
          auto row = selected(
              item,
              {"id", "type", "status", "currency", "payment_source_id", "payment_gateway", "payment_gateway_plan_id",
               "created_at", "current_period_start", "current_period_end", "canceled_at", "ended_at", "trial_id",
               "pause_ends_at", "country_code"},
              256
          );
          if (item.contains("items"))
            row["items"] = array(item["items"], [&](const Json& entry) {
              return selected(entry, {"id", "plan_id", "quantity"}, 256);
            });
          return row;
        });
      break;
    case Section::Payments:
      result.data = array(value, payment);
      result.more = result.data.size() == request.limit;
      if (result.more && !result.data.empty() && result.data.back().contains("id") && id(result.data.back()["id"]))
        result.cursor = result.data.back()["id"];
      break;
    case Section::PurchaseHistory:
      result.data["receipts"] = array(value.at("receipts"), [&](const Json& item) {
        auto row = selected(
            item,
            {"id", "created_at", "currency", "status", "icon", "description", "total", "net", "amount_refunded",
             "subtotal", "tax", "has_invoice_url", "has_refund_invoice_urls"},
            1024
        );
        if (item.contains("sku")) {
          row["sku"] = selected(item["sku"], {"id", "name", "application_id", "type"}, 256);
          if (item["sku"].contains("application"))
            row["sku"]["application"] = selected(item["sku"]["application"], {"id", "name", "icon"}, 256);
        }
        if (item.contains("payments"))
          row["payments"] = array(item["payments"], payment);
        if (item.contains("refund") && item["refund"].is_object()) {
          row["refund"] = selected(item["refund"], {"eligible", "policy"}, 256);
          if (item["refund"].contains("window"))
            row["refund"]["window"] = item["refund"]["window"].is_null()
                ? Json(nullptr)
                : selected(item["refund"]["window"], {"days", "days_since_purchase"}, 256);
        }
        return row;
      });
      if (value.contains("cursor") && value["cursor"].is_string() && cursorToken(value["cursor"].get<std::string>())) {
        result.cursor = value["cursor"].get<std::string>();
        result.more = true;
        result.data["cursor"] = result.cursor;
      } else
        result.data["cursor"] = nullptr;
      break;
    case Section::PaymentInvoice:
      if (value.contains("invoiceLink") && httpsUrl(value["invoiceLink"]))
        result.data["invoiceLink"] = value["invoiceLink"];
      result.data["refundInvoiceLinks"] = Json::array();
      if (value.contains("refundInvoiceLinks")
          && value["refundInvoiceLinks"].is_array()
          && value["refundInvoiceLinks"].size() <= 32)
        for (const auto& link : value["refundInvoiceLinks"])
          if (httpsUrl(link))
            result.data["refundInvoiceLinks"].push_back(link);
      break;
    case Section::GiftCode:
      if (request.verb == Verb::Create) {
        result.data = entitlement(value);
        result.data["redeemed"] = true;
      } else {
        result.data = selected(
            value,
            {"sku_id", "application_id", "uses", "max_uses", "expires_at", "redeemed", "subscription_plan_id", "flags",
             "gift_style"},
            256
        );
        if (value.contains("flags") && number(value["flags"], 0, INT32_MAX))
          result.data["requires_payment_source"] = (value["flags"].get<std::uint32_t>() & 1) != 0;
        if (value.contains("user"))
          result.data["user"] = account(value["user"]);
        if (value.contains("application"))
          result.data["application"] = selected(value["application"], {"id", "name", "icon", "description"}, 1024);
        if (value.contains("store_listing") && value["store_listing"].is_object()) {
          result.data["store_listing"] = selected(value["store_listing"], {"id"}, 256);
          if (value["store_listing"].contains("sku"))
            result.data["store_listing"]["sku"] =
                selected(value["store_listing"]["sku"], {"id", "name", "type", "application_id"}, 256);
        }
        for (const auto* key : {"subscription_plan", "subscription_trial"})
          if (value.contains(key))
            result.data[key] = value[key].is_null()
                ? Json(nullptr)
                : selected(value[key], {"id", "name", "sku_id", "interval", "interval_count", "premium_type"}, 256);
      }
      break;
    case Section::PaymentSources:
      if (request.verb == Verb::Get)
        result.data = array(value, [&](const Json& item) {
          return selected(
              item,
              {"id", "type", "brand", "last_4", "expires_month", "expires_year", "email", "default", "invalid",
               "country", "payment_gateway"},
              320
          );
        });
      else
        result.data = selected(
            value,
            {"id", "type", "brand", "last_4", "expires_month", "expires_year", "email", "default", "invalid", "country",
             "payment_gateway"},
            320
        );
      break;
    case Section::Entitlements:
      result.data = array(value, entitlement);
      break;
    case Section::Collectibles:
      // Only owned products, not purchase credentials or processor metadata.
      result.data = array(value, [&](const Json& item) { return cosmetic(item); });
      break;
    case Section::DataPackage:
      result.data = value.is_null()
          ? Json(nullptr)
          : selected(value, {"id", "status", "created_at", "completed_at", "retry_after", "harvest_id"}, 256);
      break;
    case Section::BackupCodes:
      if (request.verb == Verb::Create)
        result.data["challenge_ready"] = true;
      else {
        result.data["backup_codes"] = Json::array();
        if (value.contains("backup_codes") && value["backup_codes"].is_array() && value["backup_codes"].size() <= 32)
          for (const auto& code : value["backup_codes"]) {
            if (code.is_object())
              result.data["backup_codes"].push_back(selected(code, {"code", "consumed"}, 64));
            else if (string(code, 64))
              result.data["backup_codes"].push_back(code);
          }
      }
      break;
    case Section::Authenticator:
      if (value.contains("backup_codes")) {
        auto copy = request;
        copy.section = Section::BackupCodes;
        copy.verb = Verb::Update;
        result = parseUserSettings(value, copy);
      }
      break;
    case Section::SecurityKeys:
      if (request.verb == Verb::Get)
        result.data = array(value, [&](const Json& item) {
          return selected(item, {"id", "name", "type", "last_used", "created_at"}, 256);
        });
      else
        result.data = selected(value, {"id", "name", "type", "last_used", "created_at"}, 256);
      break;
    case Section::EmailChange:
    case Section::PhoneVerification:
      result.data["verified"] = request.verb == Verb::Update || request.section == Section::PhoneVerification;
      break;
    default:
      break;
    }
    return result;
  }

  UserSettingsRequest publicUserSettingsRequest(UserSettingsRequest request) {
    for (auto* field :
         {&request.password, &request.newPassword, &request.verificationCode, &request.authenticatorSecret,
          &request.mfaCode, &request.giftCode}) {
      std::fill(field->begin(), field->end(), '\0');
      field->clear();
    }
    return request;
  }

  bool sameUserSettingsAction(const UserSettingsRequest& original, const UserSettingsRequest& retry) {
    if (!retry.giftCode.empty() && retry.giftCode != original.giftCode)
      return false;
    auto first = publicUserSettingsRequest(original), second = publicUserSettingsRequest(retry);
    first.request = second.request = 0;
    first.mfaChallenge = second.mfaChallenge = 0;
    first.mfaType.clear();
    second.mfaType.clear();
    return first == second;
  }

  std::string generateAuthenticatorSecret() {
    std::array<unsigned char, 20> bytes{};
    if (::getentropy(bytes.data(), bytes.size()) != 0)
      throw std::runtime_error("Could not generate an authenticator secret");
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string result;
    unsigned accumulator = 0, bits = 0;
    for (const auto byte : bytes) {
      accumulator = (accumulator << 8) | byte;
      bits += 8;
      while (bits >= 5) {
        bits -= 5;
        result += alphabet[(accumulator >> bits) & 31];
      }
    }
    return result;
  }

  std::string userSettingsBrowserUrl(UserSettingsSection section) {
    switch (section) {
    case Section::SecurityKeys:
      return "https://discord.com/settings/account";
    case Section::Account:
    case Section::PasswordRecovery:
      return "https://discord.com/settings/account";
    case Section::Subscriptions:
      return "https://discord.com/settings/subscriptions";
    case Section::Payments:
    case Section::PaymentSources:
      return "https://discord.com/settings/billing";
    case Section::Entitlements:
    case Section::Collectibles:
      return "https://discord.com/shop";
    default:
      return {};
    }
  }

  std::string userPaymentRefundUrl() {
    return "https://support.discord.com/hc/en-us/requests/new?ticket_form_id=360000118612";
  }

  std::string userGiftCodeBrowserUrl(const std::string& code) {
    return giftCode(code) ? "https://discord.gift/" + code : std::string{};
  }

} // namespace api
