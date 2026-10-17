# Cressida

A native Discord desktop client for Linux and Wayland, written in C++23.

Real-time conversations, server folders, direct messages and a native interface
rendered with OpenGL ES. The client uses your Discord account and signs in through
your default browser.

**Early development.** Text chat is usable, and the client is growing through
regular testing. Voice and video calls are not available yet. Named after one of
Uranus's moons, Cressida builds and installs as `cressida`.

## Features

- **Real-time chat:** new messages, edits and deletions through Discord's Gateway;
  Markdown, code blocks, mentions, Unicode/custom emoji and embed cards.
  Muted message placeholders appear while a conversation first loads; cached
  messages stay visible during refreshes.
- **Conversation history:** older messages load as you scroll up, with a bounded
  sliding window, newer-page catch-up, retry controls and a jump to the present.
- **Message actions:** native context menus with icons, replies, editing/deleting
  your own messages, copy text/link/ID, and a hover emoji submenu. Four quick
  reactions adapt to successful local usage; hover a reaction to see who reacted.
- **People:** compact profile cards with banners, overlapping avatars, presence,
  badges, expandable bios, mutual counts and server role chips. Open DMs, edit
  your profile, manage friend nicknames and relationships, block/unblock people,
  and mute existing conversations.
  Chat names show server tags and role icons; clicking a tag previews its server
  and offers Join or Open Server.
- **Your servers:** saved server order and folders, icons, channel categories,
  collapsible sections and a member sidebar with role group counts. Mention badges
  appear on channels, servers and folders; a floating badge reveals mentioned
  channels further down the list.
- **Channel details:** topics in the header and at the beginning of the conversation,
  access-role chips, and permission-aware channel editing and access management.
  The header provides Threads, Mute, Pins, Members and native message search.
  Search and pinned results open the original message, loading surrounding history
  when necessary. Press Ctrl+F to search.
  Deleted channels disappear directly from Gateway updates, including an open
  channel's timeline, without waiting for a channel-list reload.
- **Message buttons:** bot action buttons and browser links below messages,
  with disabled/pending states and application response errors. Follow-up buttons
  can open native text forms with required fields and multiline editing.
  Ephemeral replies show **Only you can see this** and a local **Dismiss message**
  action. Select menus, premium buttons and non-text form fields remain unsupported.
- **Direct messages:** personal/group conversations, avatars, unread styling and
  incoming DM shortcuts with unread counts above the server list. Presence badges update live in
  DM rows, conversation headers and the participant list.
- **Threads:** open conversations beneath their parent channel or use the header's
  **Threads** button to browse active and archived threads, including joined private
  threads. Create a public thread from a message's context menu or the browser,
  join/leave threads, and reopen archived threads before replying.
- **Forums:** post cards in the main channel area, with thumbnails, previews,
  reaction counts, search, sorting and tag filters. **New Post** creates a named
  post with text and a tag; opening a post enters its thread, and Back returns to
  the forum. New-post attachments and private-thread creation are still pending.
- **Personal nicknames:** names you assigned to friends appear in conversations,
  message authors, mentions and member lists, including live nickname changes.
- **Role colors:** server role colors, gradients and holographic palettes on names,
  with animation on hover. **Settings → Cressida → General → Names & Roles** lets you disable
  role colors or keep gradients static. These preferences are saved per account.
- **Native editing:** multiline drafts, mouse/keyboard selection, word navigation,
  copy/cut/paste and undo/redo. Enter sends; Shift+Enter inserts a line.
- **Images:** paste PNG/JPEG/WebP attachments, review before sending, and expand
  received images in a fit/zoom/pan viewer. Soft image placeholders reserve space while media loads;
  failed downloads show an unavailable state.
- **Videos:** native playback for attached MP4/WebM videos and supported media
  embeds, with audio, play/pause, a draggable timeline, volume/mute and fullscreen.
  Videos require an explicit Play action and start at 25% volume. Playback keeps
  the source resolution rather than using the reduced GIF animation frames.
  YouTube and other web players open in your browser.
- **GIFs:** looping GIF/WebP images and GIF video embeds, animated custom emoji,
  a native GIF picker with trending/search, and favorites synced to Discord.
- **Emoji picker:** a server rail, Unicode categories, search, account favorites
  and frequently used emoji, including static and animated server emoji.
  Unicode and custom emoji share inline sizing; emoji-only messages get larger
  emoji, with up to 27 per message. Compact mode keeps them inline.
- **Cressida settings:** one **General** page groups name/tag/role preferences
  and expressions. Optional FakeNitro emoji lives under **Cressida → General → Expressions**.
  Unsupported server emoji send as named image links, including animated emoji.
  Other clients may display links or image previews. Reactions keep Discord's
  normal restrictions; channel embed permissions still apply. The option defaults
  off and is remembered locally per account.
- **Settings:** a dedicated in-client view with search, grouped navigation, native
  switches, profile editing, account preferences and local appearance controls.
- **Presence:** Online, Idle, Do Not Disturb, Invisible and custom-status text.
- **Persistent sign-in:** your desktop keyring remembers the last account and
  restores it automatically on launch.
- **Remembered navigation:** the last channel in each server and the DM list,
  your last selection at login, interface zoom and display preferences.

The UI uses the project's native renderer and controls. The copied Tabler font
and glyph registry supply interface icons. Networking lives separately in
[`src/discord`](src/discord), with independent REST, Gateway and media workers.

## Build and run

The Nix flake provides a development shell and package. Meson builds the client;
`just` supplies the development commands.

```sh
nix develop
just build
just run
```

Or build/run the packaged application:

```sh
nix build path:.
nix run path:.
```

A Wayland session and a working EGL/OpenGL ES driver are required to open the UI.
The flake exposes `x86_64-linux` and `aarch64-linux`; the current validation was run
on `x86_64-linux`.

For an offline preview:

```sh
just demo
just smoke
```

Demo mode uses sample conversations and generated media. It ignores account
credentials, makes no Discord requests and does not read or write account state.
`smoke` renders one frame and exits.

## Sign in

Choose **Sign in with Discord**. Complete login, MFA and any CAPTCHA in your
browser. Discord hands a one-use ticket back to the client through a temporary
loopback listener. Allow local network access if your browser asks. You can
cancel the attempt; it expires after five minutes.

Successful sign-in saves the account token in your desktop keyring through
Secret Service. Subsequent launches restore it automatically, showing a loading
view until your account is ready. The login form appears when no saved login is
available or restoration fails. Cancel stops restoration without removing the
saved login; a temporary connection failure offers **Retry saved sign-in**.

Remembering sign-in requires a running Secret Service provider, such as GNOME
Keyring or KeePassXC with Secret Service integration enabled. Your desktop may
ask you to unlock the keyring. If it is unavailable, the current session still
works, and Account settings show the storage error. Re-enter `nix develop` after
updating to pick up the new libsecret dependency.

The client also accepts a token in its masked login field, through
`CRESSIDA_TOKEN`, or with `--token-file PATH`. Explicit credentials take precedence
over the saved login. Only the last successful account is remembered; there is
no account switcher yet. Passwords and browser cookies are never saved, and there
is no plaintext token fallback. Browser login uses Discord's authentication
handoff and requires no embedded browser engine.

Signing out removes the saved login. A credential-free, owner-only
`$XDG_STATE_HOME/cressida/login-disabled` marker prevents automatic sign-in even
if keyring deletion fails or the client closes immediately. A subsequent
successful sign-in saves a new token and clears that marker. Closing the window
normally keeps your saved login.

## Using the client

Open **Direct messages** from the speech-bubble icon above your servers. Click
folder grids to expand them, category headers to collapse them, and the people
icon in the conversation header to toggle the member list.

Unread channels have brighter, bold names and an edge marker. Incoming DMs appear
as avatar shortcuts above servers. Read state follows login and acknowledgements
from other clients; this client acknowledges messages when their latest timeline
is visible in the focused window.
An accent divider marks new messages within the conversation. A wide banner at
its top shows the unread count and time; click it to jump to the first loaded
unread message, or choose **Mark as Read**. Both indicators disappear when you
catch up, mark the conversation read, or read it on another device. Counts use
**+** when older unread history falls outside the loaded messages. A draggable
scrollbar on the right lets you move through the chat. Mousewheel movement eases
continuously across chat and the GIF/emoji pickers, with a faster wheel response
and fractional text positioning; touchpad scrolling stays direct.

The composer grows with your draft. Its clipboard glyph pastes text or an image;
the smile glyph opens the emoji picker, and **GIF** opens the GIF picker.
The emoji picker's left rail jumps to favorites, frequent choices, servers or
Unicode categories. Search names, aliases or keywords; hover an emoji to see its
name and server, then click to insert it. Its star adds/removes an account
favorite. Local selections are remembered per account and combined with
Discord's frequent emoji. Server emoji availability updates live.

Review pasted image previews and remove unwanted attachments with × before sending. A draft accepts up to ten
images and 20 MiB combined; Discord may impose further account/channel limits.

Search GIFs or browse trending results, then click a result to send it. This
keeps any text or attachments already in your draft. Hover a GIF in the picker
or chat and click its star to add/remove it from **Favorites**. Favorites load
from your Discord account and changes sync there; errors leave the existing list
intact. Search and preview downloads use public endpoints without account tokens.

GIFs loop in visible chat messages and the image viewer while the window is
focused. Disable **Automatically play GIFs** in **Text & Images** for playback
on hover. Animated custom emoji follow the same preference. Standalone image/GIF links, including links with
labels, display just their media preview. Transparent images have no added
background; captions remain visible.

Click Play on a video to start it; videos never autoplay. Drag the timeline to
seek, use the speaker control to mute or adjust volume, and use the fullscreen
control to expand it. Space toggles playback, Left/Right seek five seconds and M
mutes while the player has focus or the pointer is over it. Escape exits
fullscreen and restores the previous window state. Scrolling a video out of view
pauses it. Leaving the conversation or opening settings closes playback.

Wheel scrolling animates between positions at the compositor's refresh rate.
Touchpad scrolling follows the gesture directly.

Click an image to expand it. Scroll to zoom, drag to pan, use Left/Right to move
through visible images, and press Escape or click the backdrop to close. Ctrl+C
copies the viewed PNG/JPEG/WebP. **Open original** opens its source in the browser.
Code blocks and messages offer Copy on hover; message text also supports selection.

Right-click a message for its actions. The top row holds your four most-used
reactions in this client, initially ❤️ 👍 😂 🔥. Hover **Add Reaction** for a
scrollable Unicode emoji list, or use Up/Down, Right to open it, Left to go back,
and Enter to choose. Tab focuses the quick row; Left/Right selects a quick emoji.
Click a reaction chip to add/remove your reaction; hover it
to load the people who reacted, using your personal nicknames. Tooltips show up to
eight names and summarize additional fetched people. Each normal/super-reaction
lookup loads at most 100 users; this is not a complete paginated reaction browser.

**Reply** adds a per-channel reply banner above your draft. The author mention is
off initially and can be enabled explicitly. Enter sends the reply; Shift+Enter
inserts a line. Edit uses a native multiline editor with an explicit Save action.
Deletion, blocking and removing friends have confirmation dialogs. Server
permission errors leave the dialog open with an explanation.

Right-click a username, avatar, member or DM row for profile, message, mention,
friend and block actions. Conversation mute controls apply to existing DMs/groups.
Blocked messages collapse locally and can be revealed without unblocking.

Click an avatar or message author to open their profile card. Server profiles
show server-specific details when Discord supplies them. **View Full Bio**
expands longer biographies; Markdown, custom emoji and Discord timestamps render
inside the card. **Message** opens a conversation, and your own card's
**Edit Profile** opens profile settings. Role chips are read-only.

Server tags include their badge icon in profiles and beside chat names. Hide chat
tags in **Settings → Cressida → General → Names & Roles**. Role icons remain visible when tags
are hidden. Click a tag to preview its server; joining requires an explicit click.
Private servers need an invite, and recruitment or server verification may require
continuing in your default browser.

| Shortcut | Action |
| --- | --- |
| Enter / Shift+Enter | Send / insert a new line |
| Ctrl+A / C / X / V | Select all / copy / cut / paste in an input |
| Ctrl+Shift+V | Prefer plain-text paste |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+Left / Right | Move by word; add Shift to select |
| Ctrl+Backspace / Delete | Delete the previous / next word |
| Ctrl+plus / minus / 0 | Zoom in / out / reset |
| Ctrl+wheel | Change interface scale |
| Ctrl+comma | Open user settings |
| Escape | Close a menu, dialog, settings, image or emoji picker |
| Ctrl+Q | Quit |

## User settings

Click the account gear or press Ctrl+comma. Availability controls are also
available directly from your account chip.

**Profiles** edits display name, pronouns and About Me with explicit Save Changes
and Reset controls. **Privacy & Safety**, **Content & Social**, **Friend Requests**,
**Notifications** and **Activity Privacy** edit supported Discord account
preferences. Those changes are saved explicitly, preserving unrelated account
settings. Devices, Connections and Authorized Apps currently provide read-only
account lists.

**Appearance** offers dark, light and midnight themes, message font size,
interface scale and compact spacing. **Text & Images** controls embed and image
previews. These display preferences apply locally and save per account in
`$XDG_STATE_HOME/cressida/session.json`, falling back to
`~/.local/state/cressida/session.json`. The same owner-only file remembers
navigation and local reaction usage; it contains no credentials or profile text.

Existing `discord-cpp/session.json` state is read automatically until the first
save migrates it to the Cressida directory. Other accounts and per-server channel
history are preserved; the original file stays intact. Environment overrides use
`CRESSIDA_*`; legacy `DISCORD_CPP_*` names remain accepted when the corresponding
new name is unset.

Settings that still need implementation say so. Account security, billing,
translation, game detection/overlay and further accessibility options remain
incomplete. Notification preferences sync to Discord, but this client does not
yet deliver desktop notifications or play notification sounds.

## Current limits

Initial history loads the latest 100 messages. Scrolling near the top fetches
older pages automatically; returning toward the bottom fetches newer pages when
needed. A 1,000-message sliding window bounds memory while allowing continued
browsing. Loading keeps your visible messages in place, and failed pages offer
**Retry**. **Jump to latest** returns directly to the current conversation.
Recently visited conversations and member lists use a bounded session cache.
REST/CDN connections are reused, and media completion wakes the UI immediately.

Voice/video calls, arbitrary file uploads, drag-and-drop
and syntax highlighting remain future work. GIF animation decoding has bounded size,
frame count, memory and time; longer clips may use a smaller resolution, and
clips exceeding those bounds show a still frame. Message forwarding, pin/unpin actions and bookmarks,
reminders, apps, speech and moderation menus are not implemented. Reactions can
be sent normally; sending super reactions and sticker selection remain pending.
Quick-reaction usage is local and does not import Discord's
frequently used list. Explicit channel mute flags filter unread indicators;
inherited guild mutes and timed-mute expiry still need fuller handling. Direct
mentions remain visible in muted conversations.

## Development

```sh
just test                # Offline regression suite
just run asan            # Run with symbolized Address/UndefinedBehaviorSanitizer reports
just debug               # Run normally in GDB; print thread backtraces on a crash
just build release       # Optimized build with LTO
just test asan           # Address/undefined-behaviour sanitizers
just format              # Format project C++ and Nix
just check               # Build and test the locked Nix package
```

To capture a crash, re-enter `nix develop` after updating the checkout, then run:

```sh
just run asan 2>&1 | tee /tmp/cressida-asan.log
```

Reproduce the problem and share the first sanitizer error, including its stack
and allocation/free traces. The ASan build uses `-O1`, full debug information,
frame pointers, and ASan/UBSan; runtime reports use the shell's `llvm-symbolizer`
and stop at the first error. Existing ASan directories are reconfigured with these
flags. User-supplied `ASAN_OPTIONS`/`UBSAN_OPTIONS` override the defaults.
GUI runs disable exit-time leak checks by default because driver/font globals
can obscure the crash report; `ASAN_OPTIONS=detect_leaks=1 just run asan` enables
them. `just test asan` retains the runtime's default leak checks.

If the issue only reproduces without ASan, use
`just debug 2>&1 | tee /tmp/cressida-debug.log` instead. It runs the debug build
under GDB and prints backtraces when it stops, including caught `std::bad_alloc`
exceptions before the top-level handler exits. Frame arguments and local variables
are omitted. A desktop-only offline replay is available with
`CRESSIDA_DEMO_VIEW=interaction-cycle just run debug --smoke-test`; this exercises
picker/menu transitions over 64 frames without account access.

For scrolling diagnostics, run
`CRESSIDA_TRACE_SCROLL=1 just run 2>&1 | tee /tmp/cressida-scroll.log`.
The trace contains raw Wayland wheel values and layout offsets, without message
text, IDs or credentials. `CRESSIDA_SCROLL_ANIMATION=0 just run` compares direct
wheel steps with the animated path.

If startup reports a stale renderer library, run `just rebuild` to compile the
client and shared controls together, then `just run`.

Run `just` for the full recipe list. Re-enter `nix develop` after dependency or
flake-lock changes. The project requires libcurl with WebSocket support; the Nix
package supplies that configuration. Native controls, font metadata and assets
are regular project files with no dependency on another checkout.

Tests use synthetic data and loopback peers, with no Discord credentials or real
account mutations. GPU smoke checks use the offline demo. Live account testing
is still needed for newly added settings, uploads, context actions, reaction
lookups, history pagination and read acknowledgements.
Channel edits, access changes, pins and message search also need live account
testing; their automated checks use synthetic responses. Main and follow-up bot
buttons, text forms and ephemeral reply handling have been confirmed in live use.

[`AGENTS.md`](AGENTS.md) documents architecture, development rules, protocol
research and remaining implementation work.

## License

Project code is available under the [MIT license](LICENSE). Bundled
[Tabler icons](assets/fonts/tabler-icons-license.txt),
[Unicode emoji alias data](assets/emoji-data-license.txt) and
[Wuffs](third_party/wuffs) retain their own license notices.
