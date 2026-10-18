# Cressida

A native Discord desktop client for Linux and Wayland.

**Early development:** text chat is available; voice and video calls are not yet
supported. Expect missing features and occasional rough edges.

A few highlights:

- Rich profiles and animated server role colors.
- Threads and forum posts alongside server chat and DMs.
- Built-in image/video viewing, GIFs and custom emoji.

## Run

With Nix installed, run from this checkout:

```sh
nix run path:.
```

A Wayland desktop and working graphics drivers are required.

## Sign in

Choose **Sign in with Discord** and finish signing in through your browser.
A desktop keyring, such as GNOME Keyring or KeePassXC with Secret Service enabled,
can remember your sign-in. Signing out removes the saved login.

## Current limitations

Voice/video calls, arbitrary file uploads, drag-and-drop and desktop notifications
are not available yet. Image uploads are supported. Some Discord controls and
settings remain incomplete.

## License

[MIT](LICENSE). Bundled [Tabler icons](assets/fonts/tabler-icons-license.txt),
[emoji alias data](assets/emoji-data-license.txt) and [Wuffs](third_party/wuffs)
retain their own license notices.
