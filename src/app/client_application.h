#pragma once

#include "app/message_layout.h"
#include "app/session_store.h"
#include "app/text_edit.h"
#include "app/window.h"
#include "auth/browser_login.h"
#include "core/text_clipboard.h"
#include "discord/assets.h"
#include "discord/client.h"
#include "render/core/color.h"
#include "render/core/texture_handle.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "ui/controls/input.h"

#include <chrono>
#include <map>
#include <set>

namespace app {

  class ClientApplication {
  public:
    ClientApplication(bool demo, std::string token);
    ~ClientApplication();
    int run(bool smokeTest = false);

  private:
    enum class Action {
      Settings,
      CloseSettings,
      SettingsPage,
      AccountPreference,
      SaveAccountSettings,
      ResetAccountSettings,
      SaveProfile,
      ResetProfile,
      Preference,
      SetPresence,
      TogglePresence,
      SaveCustomStatus,
      None,
      ImagePan,
      ImageZoom,
      Close,
      Minimize,
      Maximize,
      ViewImage,
      CloseImage,
      ImageNext,
      ImagePrevious,
      Connect,
      DiscordLogin,
      ToggleToken,
      CancelLogin,
      Demo,
      Token,
      Guild,
      Home,
      DirectMessage,
      Category,
      Channel,
      Folder,
      ToggleMembers,
      Send,
      PasteClipboard,
      ToggleEmoji,
      InsertEmoji,
      Composer,
      RemoveAttachment,
      CopyMessage,
      CopyCode,
      OpenUrl,
      RevealSpoiler,
      SelectMessage,
      SignOut
    };
    struct Hit {
      float x, y, width, height;
      Action action;
      std::string id;
      bool contains(float px, float py) const { return px >= x && py >= y && px < x + width && py < y + height; }
    };
    void draw();
    void tick();
    void onKey(const Key& key);
    void insert(std::string text);
    void activate(const Hit& hit);
    void connect();
    void beginLogin();
    void demo();
    void prepareDemo();
    void selectGuild(const std::string& id);
    void showDirectMessages();
    void selectChannel(const std::string& id);
    void refresh();
    void send();
    void rectangle(Node& parent, float x, float y, float w, float h, Color fill, float radius = 0);
    float label(
        Node& parent, float x, float y, float width, std::string text, float size, Color color,
        FontWeight weight = FontWeight::Normal, int lines = 1, bool markup = false
    );
    void
    button(Node& parent, Hit hit, std::string text, bool primary = false, bool selected = false, bool fill = false);
    bool image(Node& parent, float x, float y, float size, const std::string& path, float radius);
    void avatar(Node& parent, float x, float y, float size, const std::string& path, const std::string& name);
    bool picture(Node& parent, float x, float y, float width, float height, const std::string& path, float radius);
    void glyph(Node& parent, float x, float y, float size, const std::string& name, Color color);
    void iconButton(Node& parent, Hit hit, const std::string& icon, const std::string& tooltip, bool selected = false);
    void drawEmojiPicker(Node& root, float w, float h);
    void drawTooltip(Node& root, float w, float h);
    void drawSettings(Node& root, float w, float h);
    Input& inputControl(
        Node& parent, float x, float y, float width, float height, const std::string& id, const std::string& value,
        const std::string& placeholder = {}, bool multiline = false, bool password = false
    );
    void beginControls();
    void finishControls();
    void focusControl(const std::string& id);
    void pasteControl(const std::string& id, std::string text);
    void drawPresenceMenu(Node& root, float h);
    void openSettings();
    void selectSettingsPage(const std::string& page);
    void clearAccountSettings();
    void resetProfile();
    void settingsInput(Node& parent, float x, float y, float width, float height, std::string caption, std::string id);
    void settingsKey(const Key& key);
    void savePreferences();
    void setPresence(const std::string& status, bool custom = false);
    void drawImageViewer(Node& root, float w, float h);
    void changeImage(int direction);
    bool hovered(float x, float y, float width, float height) const;
    void updateCursor();
    void positionCaret(float x, float y, bool extend);
    void positionChatSelection(float x, float y);
    void upsertMessage(discord::Message message);
    void saveSelection();
    std::string userName(const std::string& id, const std::string& fallback) const;
    std::string userStatus(const std::string& id, const std::string& fallback = "offline") const;
    std::string channelName(const discord::Channel& channel) const;
    void updateDmMembers();
    void statusBadge(Node& parent, float x, float y, const std::string& status, Color backdrop);

    Window m_window;
    discord::Client m_client;
    discord::Assets m_assets;
    auth::BrowserLogin m_login;
    SessionStore m_sessionStore;
    std::string m_restoreChannel;
    Preferences m_preferences;
    discord::Presence m_presence;
    bool m_presenceMenu = false, m_settingsOpen = false, m_savingProfile = false, m_loadingProfile = false,
         m_profileDirty = false;
    std::string m_settingsPage = "account", m_settingsField, m_settingsStatus, m_desiredPresence;
    std::map<std::string, std::string> m_settingsFields;
    float m_settingsScroll = 0, m_settingsNavScroll = 0, m_settingsNavRight = 0;
    discord::SettingValues m_accountSettings, m_settingsDraft;
    bool m_loadingSettings = false, m_savingSettings = false, m_settingsLoaded = false;
    std::map<std::string, std::vector<discord::SettingsEntry>> m_settingsLists;
    std::map<std::string, std::string> m_settingsListErrors;
    std::set<std::string> m_loadingSettingsLists;
    std::unique_ptr<Node> m_scene;
    std::map<std::string, Input*> m_inputs;
    std::map<std::string, Node*> m_widgets;
    std::map<std::string, std::unique_ptr<Node>> m_detachedInputs;
    InputDispatcher m_controlInput;
    std::unique_ptr<TextClipboard> m_controlClipboard;
    std::string m_focusedInput, m_pasteInput;
    bool m_buildingScene = false, m_emojiMenu = false;
    std::string m_tooltip;
    float m_tooltipX = 0, m_tooltipY = 0, m_composerTop = 0;
    bool m_demo = false, m_demoPrepared = false;
    bool m_connected = false;
    bool m_connecting = false;
    bool m_showTokenEntry = false;
    bool m_loadingChannels = false;
    bool m_loadingMessages = false;
    bool m_sending = false;
    bool m_live = false;
    bool m_showMembers = true;
    bool m_dmMode = false;
    bool m_loadingPrivate = false;
    std::string m_gatewayStatus = "Connecting…";
    bool m_fieldFocused = true;
    bool m_selectAll = false;
    bool m_pasteConnected = false;
    std::uint64_t m_pasteSession = 0;
    std::string m_pasteChannel;
    std::uint64_t m_session = 0;
    discord::Account m_account;
    discord::UserDirectory m_userDirectory;
    std::vector<discord::Guild> m_guilds;
    std::vector<discord::Channel> m_channels;
    std::vector<discord::Channel> m_privateChannels;
    std::set<std::string> m_collapsedCategories;
    std::set<std::string> m_unreadDMs, m_unreadChannels;
    std::map<std::string, std::string> m_unreadGuilds, m_localRead;
    std::map<std::string, std::vector<discord::Channel>> m_guildChannels;
    std::map<std::string, std::vector<discord::Message>> m_channelMessages;
    std::vector<std::string> m_channelCacheOrder;
    struct MemberSnapshot {
      std::vector<discord::MemberRow> rows;
      int count = 0;
    };
    std::map<std::string, MemberSnapshot> m_channelMembers;
    std::map<std::string, std::string> m_lastChannels;
    void cacheChannel();
    bool readingChannel(const std::string& channel) const;
    std::vector<discord::Message> m_messages;
    struct CachedMessageLayout {
      MessageLayout layout;
      float width = -1, scale = -1;
      std::uint64_t fonts = 0;
    };
    std::map<std::string, CachedMessageLayout> m_messageLayouts;
    std::vector<discord::GuildFolder> m_folders;
    std::set<std::string> m_openFolders;
    std::vector<discord::MemberRow> m_members;
    int m_memberCount = 0;
    int m_memberPage = 0;
    std::map<std::string, std::optional<discord::Message>> m_liveChanges;
    struct CachedImage {
      TextureHandle texture;
      std::vector<unsigned char> bytes;
      std::uint64_t touched = 0;
      std::chrono::steady_clock::time_point retry{};
    };
    std::map<std::string, CachedImage> m_images;
    std::set<std::string> m_pendingImages;
    std::uint64_t m_imageSerial = 0;
    std::string m_token;
    std::string m_guildId;
    std::string m_channelId;
    std::string m_status;
    std::string m_pendingDraft;
    std::map<std::string, std::string> m_drafts;
    struct DraftImage {
      discord::Upload upload;
      TextureHandle texture;
      std::string key;
    };
    std::map<std::string, std::vector<DraftImage>> m_attachments;
    std::vector<std::string> m_pendingAttachments;
    struct ViewerImage {
      std::string path, url;
    };
    std::vector<ViewerImage> m_viewerImages;
    std::string m_viewImage;
    std::string m_viewUrl;
    float m_viewZoom = 1, m_viewPanX = 0, m_viewPanY = 0, m_imageDragX = 0, m_imageDragY = 0;
    bool m_dragImage = false;
    TextEdit m_edit, m_chatEdit;
    bool m_dragDraft = false, m_dragMessage = false;
    std::string m_selectedMessage, m_selectedText;
    std::set<std::string> m_revealedSpoilers;
    struct TextArea {
      MessageRun run;
      std::string message;
      float x, y;
    };
    std::vector<TextArea> m_textAreas;
    std::map<std::string, std::string> m_messageText;
    std::vector<std::size_t> m_draftOffsets;
    std::vector<TextCursorStop> m_draftStops;
    float m_attachmentScroll = 0, m_attachmentX = 0, m_attachmentY = 0, m_attachmentWidth = 0;
    float m_draftX = 0, m_draftY = 0, m_draftWidth = 0, m_draftHeight = 0;
    std::uint64_t m_attachmentSerial = 0;
    std::vector<Hit> m_hits;
    float m_pointerX = 0, m_pointerY = 0;
    float m_guildScroll = 0, m_channelScroll = 0, m_messageScroll = 0, m_messageMaxScroll = 0, m_memberScroll = 0;
  };
} // namespace app
