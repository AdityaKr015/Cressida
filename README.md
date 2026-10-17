# Cressida

A native Discord desktop client for Linux and Wayland, written in C++23.

Real-time conversations, server folders, direct messages and a native interface
rendered with OpenGL ES. The client uses your Discord account and signs in through
your default browser.

**Early development.** Text chat is usable, and the client is growing through
regular testing. Voice and video are not available yet. Named after one of
Uranus's moons, Cressida builds and installs as `cressida`.

## Features

- **Real-time chat:** new messages, edits and deletions through Discord's Gateway;
  Markdown, code blocks, mentions, Unicode/custom emoji and embed cards.
- **Your servers:** saved server order and folders, icons, channel categories,
  collapsible sections and a member sidebar.
- **Direct messages:** personal/group conversations, avatars, unread styling and
  incoming DM shortcuts above the server list.
- **Native editing:** multiline drafts, mouse/keyboard selection, word navigation,
  copy/cut/paste and undo/redo. Enter sends; Shift+Enter inserts a line.
- **Images:** paste PNG/JPEG/WebP attachments, review before sending, and expand
  received images in a fit/zoom/pan viewer. Video links have thumbnails and open
  in your browser.
- **Settings:** a dedicated in-client view with search, grouped navigation, native
  switches, profile editing, account preferences and local appearance controls.
- **Presence:** Online, Idle, Do Not Disturb, Invisible and custom-status text.
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

The client also accepts a token in its masked login field, through
`CRESSIDA_TOKEN`, or with `--token-file PATH`. Credentials remain in memory;
the application does not save tokens, passwords or browser cookies. Browser login
uses Discord's authentication handoff and requires no embedded browser engine.

## Using the client

Open **Direct messages** from the speech-bubble icon above your servers. Click
folder grids to expand them, category headers to collapse them, and the people
icon in the conversation header to toggle the member list.

Unread channels have brighter, bold names and an edge marker. Incoming DMs appear
as avatar shortcuts above servers. Read state follows login and acknowledgements
from other clients; this client acknowledges messages when their latest timeline
is visible in the focused window.

The composer grows with your draft. Its clipboard glyph pastes text or an image;
the smile glyph opens a quick emoji picker. Review pasted image previews and
remove unwanted attachments with × before sending. A draft accepts up to ten
images and 20 MiB combined; Discord may impose further account/channel limits.

Click an image to expand it. Scroll to zoom, drag to pan, use Left/Right to move
through visible images, and press Escape or click the backdrop to close. Ctrl+C
copies the viewed PNG/JPEG/WebP. **Open original** opens its source in the browser.
Code blocks and messages offer Copy on hover; message text also supports selection.

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
| Escape | Close settings, an image or the emoji picker |
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
navigation; it contains no credentials or profile text.

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

Initial history loads the latest 50 messages; the live timeline retains up to 200.
Recently visited conversations and member lists use a bounded session cache.
REST/CDN connections are reused, and media completion wakes the UI immediately.

Voice/video, starting new DM conversations, older-history pagination, arbitrary
file uploads, drag-and-drop, syntax highlighting and persistent login remain
future work. Animated media currently uses a static frame. Muted-channel unread
filtering and detailed mention-count badges are also pending.

## Development

```sh
just test                # Offline regression suite
just build release       # Optimized build with LTO
just test asan           # Address/undefined-behaviour sanitizers
just format              # Format project C++ and Nix
just check               # Build and test the locked Nix package
```

Run `just` for the full recipe list. Re-enter `nix develop` after dependency or
flake-lock changes. The project requires libcurl with WebSocket support; the Nix
package supplies that configuration. Native controls, font metadata and assets
are regular project files with no dependency on another checkout.

Tests use synthetic data and loopback peers, with no Discord credentials or real
account mutations. GPU smoke checks use the offline demo. Live account testing
is still needed for newly added settings, uploads and read acknowledgements.

[`AGENTS.md`](AGENTS.md) documents architecture, development rules, protocol
research and remaining implementation work.

## License

Project code is available under the [MIT license](LICENSE). Bundled
[Tabler icons](assets/fonts/tabler-icons-license.txt) and
[Wuffs](third_party/wuffs) retain their own license notices.
