#include "api/cosmetics.h"
#include "api/models.h"
#include "api/profile.h"
#include "api/user_directory.h"
#include "app/layout_metrics.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
} // namespace
int main() {
  using nlohmann::json;
  using namespace std::chrono_literals;
  try {
    const app::layout::Frame card{100, 200, 300, 480};
    const auto frameBody = app::layout::Cosmetics::profileFrameBody(card, 12);
    const auto frameOuter = app::layout::Cosmetics::profileFrameOuter(frameBody, 32, 24);
    require(
        frameBody.y == card.y && frameBody.height == card.height && frameOuter.y == 168 && frameOuter.height == 536,
        "Side layers stay within the card while staples retain their declared overflow"
    );
    require(
        app::layout::Cosmetics::responsiveFrameRail(card)
            && !app::layout::Cosmetics::responsiveFrameRail({100, 200, 300, 400}),
        "Responsive rails appear only on sufficiently tall cards"
    );
    require(
        api::nameplateBackground("violet", false) == 0x730BC8
            && api::nameplateBackground("violet", true) == 0x972FED
            && !api::nameplateBackground("none", false)
            && !api::nameplateBackground("unknown", true),
        "Known nameplate palettes retain separate light and dark colors with a neutral fallback"
    );
    api::UserCosmetics paletteState;
    api::mergeCosmetics(
        paletteState,
        api::parseUserCosmetics(json{{"collectibles", {{"nameplate", {{"sku_id", "22"}, {"palette", "violet"}}}}}})
    );
    api::mergeCosmetics(
        paletteState, api::parseUserCosmetics(json{{"collectibles", {{"nameplate", {{"sku_id", "22"}}}}}})
    );
    require(paletteState.nameplatePalette == "violet", "Partial metadata for the same nameplate preserves its palette");
    api::mergeCosmetics(
        paletteState, api::parseUserCosmetics(json{{"collectibles", {{"nameplate", {{"sku_id", "23"}}}}}})
    );
    require(
        paletteState.nameplatePalette.value_or("") == "",
        "Replacing artwork without a palette cannot retain the previous item's colors"
    );
    api::UserDirectory palettes;
    palettes.consume(
        "USER_UPDATE", json{{"id", "11"}, {"collectibles", {{"nameplate", {{"sku_id", "22"}, {"palette", "violet"}}}}}}
    );
    palettes.consume(
        "GUILD_MEMBER_UPDATE",
        json{
            {"guild_id", "33"},
            {"user", {{"id", "11"}}},
            {"collectibles", {{"nameplate", {{"sku_id", "23"}, {"palette", "sky"}}}}}
        }
    );
    require(
        palettes.cosmetics("11", "33").nameplatePalette == "sky"
            && palettes.cosmetics("11").nameplatePalette == "violet",
        "Server nameplates replace artwork and palette together without changing the global appearance"
    );
    palettes.consume(
        "GUILD_MEMBER_UPDATE", json{{"guild_id", "33"}, {"user", {{"id", "11"}}}, {"collectibles", nullptr}}
    );
    require(
        palettes.cosmetics("11", "33").nameplatePalette == "violet",
        "Cleared server nameplates inherit the current global palette"
    );
    palettes.consume("USER_UPDATE", json{{"id", "11"}, {"collectibles", nullptr}});
    require(palettes.cosmetics("11").nameplatePalette == "", "Clearing a nameplate clears its background palette too");
    const std::string hash = "a_11111111111111111111111111111111";
    const json user{
        {"id", "11"},
        {"username", "maya"},
        {"avatar_decoration_data", {{"asset", hash}}},
        {"collectibles", {{"nameplate", {{"sku_id", "22"}}}}}
    };
    const auto parsed = api::parseUserCosmetics(user);
    require(
        parsed.decoration && parsed.decoration->contains(hash) && parsed.nameplate == api::collectibleAsset("22"),
        "Avatar and nameplate metadata resolve to fixed public asset routes"
    );
    require(
        api::mediaUrl(*parsed.nameplate).starts_with("https://cdn.discordapp.com/media/v1/collectibles-shop/"),
        "Cosmetic routes pass the public media whitelist"
    );
    require(
        api::collectibleAsset("22", "../secret").empty() && api::collectibleAsset("bad").empty(),
        "Catalog IDs and layer IDs cannot change the requested origin or route"
    );
    require(
        !api::parseUserCosmetics(json{{"avatar_decoration_data", {{"asset", "../invalid"}}}}).decoration,
        "Malformed decoration patches do not erase a valid snapshot"
    );
    api::UserDirectory directory, projected;
    projected.apply(directory.consume("USER_UPDATE", user));
    require(projected.cosmetics("11") == parsed, "Typed cosmetics cross the event loop boundary");
    projected.apply(directory.consume("USER_UPDATE", json{{"id", "11"}, {"global_name", "Maya updated"}}));
    require(projected.cosmetics("11") == parsed, "Omitted cosmetics preserve their latest state");
    projected.apply(directory.consume(
        "GUILD_MEMBER_UPDATE",
        json{
            {"guild_id", "33"},
            {"user", user},
            {"avatar_decoration_data", {{"asset", "22222222222222222222222222222222"}}}
        }
    ));
    require(
        projected.cosmetics("11", "33").decoration != parsed.decoration && projected.cosmetics("11") == parsed,
        "Guild decorations override their scope without changing global or DM appearances"
    );
    projected.apply(directory.consume(
        "GUILD_MEMBER_UPDATE", json{{"guild_id", "33"}, {"user", {{"id", "11"}}}, {"avatar_decoration_data", nullptr}}
    ));
    require(projected.cosmetics("11", "33") == parsed, "Cleared guild cosmetics inherit the current global appearance");
    projected.apply(directory.consume(
        "USER_UPDATE", json{{"id", "11"}, {"avatar_decoration_data", nullptr}, {"collectibles", nullptr}}
    ));
    require(
        projected.cosmetics("11").decoration == "" && projected.cosmetics("11").nameplate == "",
        "Explicit clears remove cosmetics without requiring a username field"
    );
    projected.apply(directory.consume("READY", json::object()));
    require(!projected.cosmetics("11", "33").decoration, "Account reset drops global and server appearances");

    const auto response = json{
        {"user", user},
        {"user_profile",
         {{"collectibles", json::array({{{"sku_id", "44"}, {"type", 1}}, {{"sku_id", "55"}, {"type", 3}}})}}}
    };
    const auto profile = api::parseUserProfile(response);
    require(
        profile.effect && profile.effect->sku == "44" && profile.frame && profile.frame->sku == "55",
        "Equipped profile effects and frames resolve independently of avatar decorations"
    );
    auto expired = response;
    expired["user_profile"]["collectibles"][0]["expires_at"] = "2000-01-01T00:00:00Z";
    require(
        !api::parseUserProfile(expired).effect && api::parseUserProfile(expired).frame,
        "Expired effects do not hide separately equipped valid frames"
    );
    auto guild = response;
    guild["guild_member_profile"] = {{"guild_id", "33"}, {"collectibles", json::array()}};
    require(
        api::parseUserProfile(guild, "33").effect && api::parseUserProfile(guild, "33").frame,
        "Empty server cosmetics inherit the globally equipped effect and frame"
    );
    guild["guild_member_profile"]["collectibles"] = json::array({{{"type", 1}, {"sku_id", "66"}}});
    require(
        api::parseUserProfile(guild, "33").effect->sku == "66" && api::parseUserProfile(guild, "33").frame->sku == "55",
        "A server effect override preserves an independently inherited global frame"
    );
    const auto skuDecoration =
        api::parseUserCosmetics(json{{"avatar_decoration_data", {{"sku_id", "77"}, {"asset", "not-a-legacy-hash"}}}});
    require(
        skuDecoration.decoration == api::collectibleAsset("77")
            && api::cosmeticAssetVariant(*skuDecoration.decoration, true) == api::collectibleAsset("77", {}, true),
        "SKU-backed decorations use the full animated asset without depending on legacy hash formats"
    );
    require(
        api::cosmeticAssetVariant(*parsed.decoration, false) != api::cosmeticAssetVariant(*parsed.decoration, true),
        "Legacy decorations isolate their still and animated texture identities"
    );
    const json product{
        {"sku_id", "44"},
        {"items",
         json::array(
             {{{"type", 1},
               {"reducedMotionSrc", "https://cdn.discordapp.com/assets/profile_effects/test/still.png"},
               {"effects",
                json::array(
                    {{{"src", "https://cdn.discordapp.com/assets/profile_effects/test/intro.png"},
                      {"start", 100},
                      {"duration", 200},
                      {"loop", true},
                      {"loopDelay", 50},
                      {"zIndex", 1}},
                     {{"src", "https://attacker.invalid/image.png"}, {"duration", 100}}}
                )}}}
         )}
    };
    const auto effect = api::parseCosmeticProduct(product, "44");
    require(
        effect.effect
            && effect.effect->layers.size() == 1
            && effect.effect->still.starts_with("/assets/profile_effects/"),
        "Only bounded layer data and fixed CDN origins reach the renderer"
    );
    require(
        !api::parseCosmeticProduct(product, "45").effect, "A mismatched product cannot hydrate another equipped SKU"
    );
    const auto& layer = effect.effect->layers.front();
    require(
        !api::sampleProfileEffect(layer, 99ms).visible && api::sampleProfileEffect(layer, 99ms).next == 1ms,
        "Delayed layers schedule their first visible frame"
    );
    require(
        api::sampleProfileEffect(layer, 100ms).visible && api::sampleProfileEffect(layer, 100ms).elapsed == 0ms,
        "A layer starts at its configured time"
    );
    require(
        !api::sampleProfileEffect(layer, 300ms).visible && api::sampleProfileEffect(layer, 300ms).next == 50ms,
        "Loop delays hide the layer and schedule the next loop"
    );
    require(
        api::sampleProfileEffect(layer, 350ms).visible && api::sampleProfileEffect(layer, 350ms).elapsed == 0ms,
        "Layer animation restarts at each loop boundary"
    );
    auto once = layer;
    once.loop = false;
    require(
        !api::sampleProfileEffect(once, 1000ms).visible
            && api::sampleProfileEffect(once, 1000ms).next == std::chrono::milliseconds::max(),
        "One-shot intros finish without scheduling permanent animation work"
    );
    const auto currentProduct = json::parse(
        R"CATALOG({"sku_id":"1314020996776329279","items":[{"type":1,"sku_id":"1314020996776329279","animationType":2,"reducedMotionSrc":"https://cdn.discordapp.com/assets/content/fe3c94e9a28bee999a9e11ca3843dcd6b17042f359d5bd763e68a7e66d8f549a","effects":[{"src":"https://cdn.discordapp.com/assets/content/dec88ce5f94ddf08a272ad2c71c19678306d4ea90342079ba3cea3fb0115d760","loop":false,"height":880,"width":450,"duration":4980,"start":0,"loopDelay":0,"position":{"x":0,"y":0},"zIndex":100,"randomizedSources":[]},{"src":"https://cdn.discordapp.com/assets/content/abcb4ebefc37d4b6877035b2df0decc318a42032fed3b80880d51d87feba11b8","loop":true,"height":880,"width":450,"duration":5000,"start":6500,"loopDelay":1500,"position":{"x":0,"y":0},"zIndex":101,"randomizedSources":[]}]}]})CATALOG"
    );
    const auto currentEffect = api::parseCosmeticProduct(currentProduct, "1314020996776329279");
    require(
        currentEffect.effect
            && currentEffect.effect->layers.size() == 2
            && currentEffect.effect->layers[0].zIndex == 100
            && currentEffect.effect->layers[1].zIndex == 101
            && !api::mediaUrl(currentEffect.effect->still).empty()
            && !api::mediaUrl(currentEffect.effect->layers[0].source).empty(),
        "Current public catalog responses retain content-addressed assets, layer order and reduced-motion artwork"
    );
    const json frameProduct{
        {"sku_id", "55"},
        {"items",
         json::array(
             {{{"type", 3},
               {"inner_width", 320},
               {"overflow_top", 16},
               {"overflow_bottom", 16},
               {"overflow_horizontal", 8},
               {"layers",
                json::array(
                    {{{"id", "top"}, {"type", "staple"}, {"anchor", "top"}, {"order", "front"}},
                     {{"id", "sides"}, {"type", "border"}, {"anchor", "center"}, {"order", "back"}},
                     {{"id", "../bad"}, {"type", "rail"}, {"anchor", "center"}, {"order", "front"}}}
                )}}}
         )}
    };
    const auto frame = api::parseCosmeticProduct(frameProduct, "55");
    require(
        frame.frame && frame.frame->layers.size() == 2 && frame.frame->innerWidth == 320,
        "Frames retain their layer order, anchors and dimensions while rejecting invalid IDs"
    );
    const auto realFrameProduct = json::parse(
        R"FRAME({"sku_id":"1524512601827180624","items":[{"type":3,"sku_id":"1524512601827180624","layers":[{"id":"1524557375024009436","type":"border","order":"front","anchor":"center","responsive":false},{"id":"1524557380090593321","type":"staple","order":"front","anchor":"top","responsive":false},{"id":"1524557384922697981","type":"staple","order":"front","anchor":"bottom","responsive":false}],"inner_width":1200,"overflow_top":127,"overflow_bottom":123,"overflow_horizontal":38}]})FRAME"
    );
    const auto realFrame = api::parseCosmeticProduct(realFrameProduct, "1524512601827180624");
    require(
        realFrame.frame
            && realFrame.frame->layers.size() == 3
            && realFrame.frame->innerWidth == 1200
            && !api::mediaUrl(realFrame.frame->layers.front().source).empty(),
        "Current public profile-frame products retain every loadable border and staple layer"
    );
    auto oversized = frameProduct;
    oversized["items"][0]["inner_width"] = 0.001;
    require(
        !api::parseCosmeticProduct(oversized, "55").frame,
        "Invalid reference sizes cannot create unbounded frame geometry"
    );
    std::cout << "Cosmetic projection, scope, validation and layer timing checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
