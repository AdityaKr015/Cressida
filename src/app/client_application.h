#pragma once

#include "app/emoji_catalog.h"
#include "app/message_history.h"
#include "app/message_layout.h"
#include "app/scroll_motion.h"
#include "app/session_store.h"
#include "app/text_edit.h"
#include "app/timeline_anchor.h"
#include "app/unread_timeline.h"
#include "app/window.h"
#include "auth/browser_login.h"
#include "core/text_clipboard.h"
#include "core/timer_manager.h"
#include "discord/assets.h"
#include "discord/client.h"
#include "media/video_player.h"
#include "render/backend/gles_framebuffer.h"
#include "render/core/color.h"
#include "render/core/texture_handle.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "ui/controls/context_menu.h"
#include "ui/controls/input.h"

#include <EGL/egl.h>
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
      JumpUnread,
      MarkRead,
      RetryHistory,
      JumpLatest,
      SetPresence,
      TogglePresence,
      SaveCustomStatus,
      None,
      ImagePan,
      PlayVideo,
      PauseVideo,
      ExpandVideo,
      CloseVideo,
      SeekVideo,
      VolumeVideo,
      MuteVideo,
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
      RetryLogin,
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
      PickEmoji,
      FavoriteEmoji,
      EmojiSection,
      EmojiRetry,
      CloseEmoji,
      ToggleGif,
      CloseGif,
      GifTab,
      GifRetry,
      SendGif,
      FavoriteGif,
      Composer,
      RemoveAttachment,
      CopyMessage,
      CopyCode,
      OpenUrl,
      RevealSpoiler,
      SelectMessage,
      SignOut,
      MessageMenu,
      UserMenu,
      ConversationMenu,
      UserProfile,
      Reply,
      CancelReply,
      ToggleReplyMention,
      AddReaction,
      ToggleReaction,
      EditMessage,
      DeleteMessage,
      CopyLink,
      CopyId,
      MentionUser,
      OpenDm,
      BlockUser,
      UnblockUser,
      AddFriend,
      RemoveFriend,
      SetNickname,
      MuteConversation,
      UnmuteConversation,
      SubmitDialog,
      CloseDialog,
      JumpReply,
      RevealBlocked,
      QuickReaction
    };
    struct Hit {
      float x, y, width, height;
      Action action;
      std::string id;
      bool contains(float px, float py) const { return px >= x && py >= y && px < x + width && py < y + height; }
    };
    void draw();
    void closeVideo();
    void openVideo(const std::string& source);
    void drawVideo(
        Node& parent, float x, float y, float width, float height, const std::string& source, const std::string& poster,
        bool expanded = false
    );
    void drawVideoViewer(Node& root, float width, float height);
    bool videoAction(const Hit& hit);
    bool videoKey(const Key& key);
    void videoMotion(float x, float y);
    void videoRelease();
    void toggleVideo();
    void setVideoExpanded(bool enabled);
    void demoVideoStep();
    void tick();
    void onKey(const Key& key);
    void insert(std::string text);
    void activate(const Hit& hit);
    void connect();
    void beginLogin();
    void restoreLogin();
    void demo();
    void prepareDemo();
    void selectGuild(const std::string& id);
    void showDirectMessages();
    void selectChannel(const std::string& id);
    void refresh();
    void loadHistory(discord::HistoryDirection direction, bool retry = false);
    void applyHistory(discord::Event& event);
    void demoHistoryStep();
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
    bool picture(
        Node& parent, float x, float y, float width, float height, const std::string& path, float radius,
        bool placeholder = false, bool videoPoster = false
    );
    void glyph(Node& parent, float x, float y, float size, const std::string& name, Color color);
    void iconButton(Node& parent, Hit hit, const std::string& icon, const std::string& tooltip, bool selected = false);
    void drawEmojiPicker(Node& root, float w, float h);
    void prepareEmojiPicker();
    void closeEmojiPicker();
    void clearEmojiState();
    void emojiEvent(discord::Event& event);
    void pickEmoji(const std::string& key);
    void favoriteEmoji(const std::string& key);
    std::string favoriteEmojiKey(const discord::Emoji& emoji) const;
    void drawGifPicker(Node& root, float w, float h);
    void searchGifs();
    void closeGifPicker();
    void clearGifs();
    void gifEvent(discord::Event& event);
    void toggleGifFavorite(const discord::Gif& gif);
    void demoGifStep();
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
    void openContext(float x, float y);
    void closeContext();
    void drawContext(Node& root, float w, float h);
    void contextAction(Action action);
    bool overlayKey(const Key& key);
    bool overlayClick(float x, float y);
    discord::Account lookupUser(const std::string& id) const;
    bool startInteraction(discord::Interaction action);
    void interactionResult(const discord::Event& event);
    void showProfile(const std::string& user);
    void drawDialog(Node& root, float w, float h);
    void submitDialog();
    void closeDialog();
    float reactionStrip(Node* parent, const discord::Message& message, float x, float y, float width);
    bool blockedMessage(const discord::Message& message) const;
    bool interactionAction(const Hit& hit);
    void clearInteractions();
    void contextMotion(float x, float y);
    void scrollContext(float delta);
    void chooseReaction(const std::string& emoji);
    void openReactionSubmenu(float rowCenter);
    void hoverReaction(const discord::Message& message, const discord::Reaction& reaction, const Hit& chip);
    void requestReactors(discord::Interaction action);
    void invalidateReactors(const std::string& channel, const std::string& message);
    void demoInteractionStep();

    Window m_window;
    ScrollMotion m_scrolling;
    discord::Client m_client;
    discord::Assets m_assets;
    media::VideoPlayer m_video;
    EGLContext m_videoContext = EGL_NO_CONTEXT;
    GlesFramebuffer m_videoTarget;
    std::string m_videoSource;
    bool m_videoExpanded = false, m_videoVisible = false, m_videoWasPlaying = false, m_videoPlayRequested = false;
    bool m_videoPreviousFullscreen = false;
    int m_videoDrag = 0;
    float m_videoScrub = 0;
    Hit m_videoSeek{}, m_videoVolume{}, m_videoBounds{};
    std::map<std::string, std::string> m_videoPosters;
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
    EmojiCatalog m_emojiCatalog;
    discord::EmojiPreferences m_emojiPreferences;
    std::vector<std::string> m_recentEmojis;
    std::set<std::string> m_loadedEmojiGuilds, m_loadingEmojiGuilds, m_pendingEmojiFavorites;
    std::map<std::string, std::string> m_emojiGuildErrors;
    bool m_emojiPreferencesLoaded = false, m_loadingEmojiPreferences = false, m_emojiPreferencesDirty = false,
         m_emojiOpened = false;
    std::string m_emojiQuery, m_emojiSection = "all", m_emojiError, m_emojiHovered, m_emojiJump;
    Hit m_emojiBounds{};
    float m_emojiScroll = 0, m_emojiRailScroll = 0;
    bool m_gifMenu = false, m_gifFavoritesTab = false, m_loadingGifs = false, m_loadingGifFavorites = false,
         m_gifFavoritesLoaded = false, m_gifFavoritesDirty = false, m_sentGif = false;
    std::string m_gifQuery, m_gifRequestedQuery, m_gifError, m_gifFavoriteError;
    std::vector<discord::Gif> m_gifs, m_gifFavorites;
    std::map<std::string, discord::Gif> m_gifTargets;
    std::set<std::string> m_pendingGifFavorites;
    Hit m_gifBounds{};
    float m_gifScroll = 0, m_gifMaxScroll = 0;
    Timer m_gifSearchTimer;
    std::string m_tooltip;
    std::vector<Hit> m_contextTargets;
    std::vector<ContextMenuControlEntry> m_contextEntries;
    ContextMenuControl* m_contextControl = nullptr;
    ContextMenuControl* m_reactionControl = nullptr;
    std::vector<std::string> m_frequentReactions{"❤️", "👍", "😂", "🔥"};
    std::vector<std::pair<std::string, std::string>> m_reactionChoices;
    bool m_reactionSubmenu = false, m_reactionSubmenuDirty = false, m_reactionSubmenuFocus = false;
    float m_reactionSubmenuAnchor = 0, m_reactionSubmenuScroll = 0, m_reactionSubmenuHeight = 0;
    Hit m_reactionSubmenuBounds{};
    bool m_contextOpen = false, m_contextDirty = false;
    int m_contextQuickIndex = -1;
    float m_contextX = 0, m_contextY = 0;
    Hit m_contextBounds{}, m_dialogBounds{};
    std::string m_contextMessage, m_contextUser, m_contextChannel;
    std::string m_dialogKind, m_dialogTitle, m_dialogText, m_dialogError;
    std::optional<discord::Interaction> m_dialogAction;
    discord::Account m_dialogAccount;
    bool m_dialogPending = false;
    std::uint64_t m_dialogGeneration = 0, m_pasteDialogGeneration = 0;
    std::set<std::string> m_pendingInteractions, m_revealedBlocked;
    std::map<std::string, discord::MessageReference> m_replies;
    std::optional<discord::MessageReference> m_pendingReply;
    std::string m_reactionMessage, m_reactionChannel;
    struct ReactorPage {
      std::vector<discord::Account> users;
      std::string error;
      bool pending = false, loaded = false;
      std::uint64_t revision = 0;
      std::chrono::steady_clock::time_point touched{};
    };
    std::map<std::string, ReactorPage> m_reactorPages;
    std::uint64_t m_reactorSerial = 0;
    std::string m_hoverReactionKey;
    Timer m_reactorHoverTimer;
    bool m_reactionHoveredThisFrame = false;
    std::map<std::string, float> m_messageTops;
    float m_tooltipX = 0, m_tooltipY = 0, m_composerTop = 0;
    bool m_demo = false, m_demoPrepared = false;
    bool m_traceScroll = false, m_animateWheel = true;
    std::string m_demoOverlay;
    bool m_demoOverlayApplied = false;
    unsigned m_demoInteractionStep = 0;
    std::optional<discord::HistoryQuery> m_demoHistoryQuery;
    std::optional<TimelineAnchor::Row> m_demoHistoryAnchor;
    std::optional<TimelineAnchor::Row> m_demoMotionAnchor;
    unsigned m_demoHistoryPages = 0, m_demoHistoryPhase = 0, m_demoHistoryMotion = 0;
    bool m_connected = false;
    bool m_connecting = false, m_restoringLogin = false, m_retrySavedLogin = false;
    std::string m_loginStorageStatus;
    bool m_showTokenEntry = false;
    bool m_loadingChannels = false;
    bool m_loadingMessages = false;
    MessageHistory m_history;
    TimelineAnchor m_timelineAnchor;
    bool m_keepHistoryAnchor = false;
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
    UnreadTimeline m_unreadTimeline;
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
    bool markChannelRead(bool explicitAction = false);
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
      std::shared_ptr<const media::Animation> animation;
      std::chrono::steady_clock::time_point animationStart{};
      std::size_t animationFrame = 0;
      std::uint64_t visibleFrame = 0, playingDraw = 0;
    };
    std::map<std::string, CachedImage> m_images;
    std::set<std::string> m_pendingImages;
    std::uint64_t m_imageSerial = 0;
    std::uint64_t m_animationDraw = 0;
    std::chrono::milliseconds m_animationDelay = std::chrono::milliseconds::max();
    Timer m_animationTimer;
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
      float width = 0, height = 0;
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
    float m_messageViewportTop = 122;
  };
} // namespace app
