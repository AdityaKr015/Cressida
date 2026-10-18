project('cressida', ['c', 'cpp'],
  version: '0.1.0',
  default_options: ['cpp_std=c++23', 'cpp_extensions=false', 'warning_level=1'],
)

# Rendering and Discord networking remain separate libraries.
render_deps = []
foreach name : [
  'wayland-client', 'wayland-egl', 'egl', 'glesv2',
  'cairo', 'cairo-ft', 'pango', 'pangocairo', 'pangoft2',
  'freetype2', 'fontconfig', 'harfbuzz', 'librsvg-2.0',
  'libwebp', 'libwebpdemux', 'libjxl', 'libjxl_threads',
  'xkbcommon', 'nlohmann_json', 'threads',
]
  render_deps += dependency(name)
endforeach
stb_dep = dependency('stb', required: false)
if stb_dep.found()
  render_deps += stb_dep
endif
cpp = meson.get_compiler('cpp')
if not cpp.has_header('stb/stb_image_resize2.h', dependencies: render_deps) and not cpp.has_header('stb_image_resize2.h', dependencies: render_deps)
  error('A recent stb with stb_image_resize2.h is required (see shell.nix).')
endif

scanner = find_program('wayland-scanner')
protocol_sources = []
foreach name : ['cursor-shape-v1', 'text-input-unstable-v3']
  xml = 'protocols/' + name + '.xml'
  protocol_sources += custom_target(name + '-header',
    input: xml,
    output: name + '-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  protocol_sources += custom_target(name + '-code',
    input: xml,
    output: name + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

wayland_protocols = dependency('wayland-protocols')
xdg_xml = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
xdg_sources = [
  custom_target('xdg-shell-header', input: xdg_xml, output: 'xdg-shell-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target('xdg-shell-code', input: xdg_xml, output: 'xdg-shell-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

decoration_xml = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir') / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'
xdg_sources += [
  custom_target('xdg-decoration-header', input: decoration_xml, output: 'xdg-decoration-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target('xdg-decoration-code', input: decoration_xml, output: 'xdg-decoration-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

subdir('src')
render_includes = include_directories('src', 'third_party/wuffs')
render_lib = shared_library('cressida-render', render_sources + control_sources, protocol_sources,
  include_directories: render_includes,
  dependencies: render_deps,
  cpp_args: [
    '-DCRESSIDA_INSTALL_PREFIX="' + get_option('prefix') + '"',
    '-DCRESSIDA_INSTALL_DATADIR="' + get_option('datadir') + '"',
  ],
  install: true,
)
render_dep = declare_dependency(
  link_with: render_lib,
  include_directories: render_includes,
  dependencies: render_deps,
)

media_deps = [dependency('libavformat'), dependency('libavcodec'), dependency('libavutil'), dependency('libswscale'), dependency('libwebpdemux')]
media_library = static_library('cressida-media', 'src/media/animation.cpp',
  include_directories: include_directories('src'), dependencies: media_deps)
media_dep = declare_dependency(link_with: media_library, include_directories: include_directories('src'), dependencies: media_deps)
video_library = static_library('cressida-video', 'src/media/video_player.cpp',
  include_directories: include_directories('src'), dependencies: [dependency('mpv', version: '>=2.3'), dependency('threads')])
video_dep = declare_dependency(link_with: video_library, include_directories: include_directories('src'), dependencies: dependency('threads'))
backend_deps = [dependency('libcurl', version: '>=8.16'), dependency('nlohmann_json'), dependency('threads')]
if meson.can_run_host_binaries()
  websocket_check = cpp.run('''
    #include <curl/curl.h>
    #include <cstring>
    int main() {
      for (const char* const* protocol = curl_version_info(CURLVERSION_NOW)->protocols; *protocol; ++protocol)
        if (std::strcmp(*protocol, "wss") == 0) return 0;
      return 1;
    }
  ''', dependencies: backend_deps, name: 'libcurl secure WebSockets')
  if not websocket_check.compiled() or websocket_check.returncode() != 0
    error('libcurl needs WebSocket support. Re-enter nix develop to use the project curl build, then reconfigure.')
  endif
endif
credential_deps = [dependency('libsecret-1'), dependency('threads')]
credentials = static_library('credentials', 'src/auth/credentials.cpp', 'src/auth/secret_service.cpp',
  include_directories: include_directories('src'), dependencies: credential_deps)
credential_dep = declare_dependency(link_with: credentials,
  include_directories: include_directories('src'), dependencies: credential_deps)
backend = static_library('cressida-backend', 'src/api/models.cpp', 'src/api/client.cpp',
  'src/api/settings.cpp', 'src/api/gifs.cpp', 'src/api/emoji.cpp', 'src/api/read_state.cpp', 'src/api/role_styles.cpp', 'src/api/profile.cpp', 'src/api/threads.cpp', 'src/api/channel_features.cpp', 'src/api/user_directory.cpp', 'src/api/actions.cpp', 'src/api/assets.cpp', 'src/api/gateway.cpp', 'src/api/uploads.cpp', 'src/api/video_source.cpp',
  include_directories: include_directories('src'),
  dependencies: backend_deps + [credential_dep, media_dep],
)
backend_dep = declare_dependency(link_with: backend,
  include_directories: include_directories('src'), dependencies: backend_deps + [credential_dep, media_dep])

browser_opener = find_program('xdg-open', required: false)
browser_args = []
if browser_opener.found()
  browser_args += '-DCRESSIDA_OPEN_URI="' + browser_opener.full_path() + '"'
endif
browser_login = static_library('browser-login', 'src/auth/browser_login.cpp',
  include_directories: include_directories('src'),
  dependencies: [dependency('nlohmann_json'), dependency('threads')],
  cpp_args: browser_args,
)
browser_dep = declare_dependency(link_with: browser_login,
  include_directories: include_directories('src'), dependencies: dependency('threads'))

client_args = []
if get_option('emoji_font') != ''
  client_args += '-DCRESSIDA_EMOJI_FONT="' + get_option('emoji_font') + '"'
endif
markdown_dep = dependency('md4c')
message_layout = static_library('message-layout', ['src/app/message_layout.cpp', 'src/app/syntax_highlight.cpp'], dependencies: [render_dep, backend_dep, markdown_dep])
message_layout_dep = declare_dependency(link_with: message_layout, dependencies: [render_dep, backend_dep, markdown_dep])
emoji_catalog = static_library('emoji-catalog', 'src/app/emoji_catalog.cpp', dependencies: [backend_dep, markdown_dep])
client = executable('cressida',
  'src/main.cpp', 'src/app/window.cpp', 'src/app/font_catalog.cpp', 'src/app/autocomplete.cpp', 'src/app/media_view.cpp', 'src/app/sidebar_view.cpp', 'src/app/video_view.cpp', 'src/app/role_view.cpp', 'src/app/system_message_view.cpp', 'src/app/message_log_view.cpp', 'src/app/message_dialog.cpp', 'src/app/profile_view.cpp', 'src/app/name_badges.cpp', 'src/app/guild_profile_view.cpp', 'src/app/thread_view.cpp', 'src/app/forum_view.cpp', 'src/app/channel_view.cpp', 'src/app/component_view.cpp', 'src/app/channel_demo.cpp', 'src/app/gif_picker.cpp', 'src/app/emoji_picker.cpp', 'src/app/user_settings.cpp', 'src/app/native_controls.cpp', 'src/app/context_actions.cpp', 'src/app/client_application.cpp', 'src/app/session_store.cpp', xdg_sources, protocol_sources[0], protocol_sources[1],
  dependencies: [render_dep, backend_dep, browser_dep, markdown_dep, video_dep],
  link_with: [message_layout, emoji_catalog],
  cpp_args: client_args,
  install: true,
)

install_subdir('src',
  install_dir: get_option('includedir') / 'cressida',
  strip_directory: true,
  exclude_files: ['app/channel_demo.cpp', 'app/channel_view.cpp', 'app/component_view.cpp', 'api/channel_features.cpp', 'api/threads.cpp', 'app/thread_view.cpp', 'app/forum_view.cpp'] + render_source_paths + control_source_paths + ['app/emoji_picker.cpp', 'app/emoji_catalog.cpp', 'api/emoji.cpp', 'api/role_styles.cpp', 'api/profile.cpp', 'app/profile_view.cpp', 'app/name_badges.cpp', 'app/guild_profile_view.cpp', 'app/role_view.cpp', 'app/system_message_view.cpp', 'app/message_log_view.cpp', 'app/message_dialog.cpp', 'app/video_view.cpp', 'media/video_player.cpp', 'api/video_source.cpp'] + ['meson.build', 'main.cpp', 'app/font_catalog.cpp', 'app/window.cpp', 'app/media_view.cpp', 'app/sidebar_view.cpp', 'app/gif_picker.cpp', 'app/user_settings.cpp', 'app/native_controls.cpp', 'app/context_actions.cpp', 'app/client_application.cpp', 'app/session_store.cpp', 'app/message_layout.cpp', 'api/client.cpp', 'api/models.cpp', 'api/settings.cpp', 'api/gifs.cpp', 'api/read_state.cpp', 'api/user_directory.cpp', 'api/actions.cpp', 'api/assets.cpp', 'api/gateway.cpp', 'api/uploads.cpp', 'auth/browser_login.cpp', 'auth/credentials.cpp', 'auth/secret_service.cpp', 'media/animation.cpp'],
)
install_subdir('assets',
  install_dir: get_option('datadir') / 'cressida',
)
install_data('LICENSE', 'third_party/wuffs/LICENSE-APACHE', 'third_party/wuffs/LICENSE-MIT',
  install_dir: get_option('datadir') / 'licenses/cressida',
)

# Reuse upstream regression coverage for the copied text and image paths.
foreach name : ['cairo_text_renderer', 'image_orientation', 'image_file_loader_data_uri']
  check = executable(name + '_test',
    'tests/' + name + '_test.cpp',
    dependencies: render_dep,
  )
  test(name, check,
    env: ['CRESSIDA_ASSETS_DIR=' + meson.project_source_root() / 'assets'],
  )
endforeach

glyph_gradient_test = executable('glyph_gradient_test', 'tests/glyph_gradient_test.cpp', dependencies: render_dep)
test('glyph_gradient', glyph_gradient_test)

model_test = executable('discord_models_test', 'tests/discord_models_test.cpp', dependencies: backend_dep)
test('discord_models', model_test)
client_test = executable('discord_client_test', 'tests/discord_client_test.cpp', dependencies: backend_dep)
test('discord_client', client_test)
test('client_help', client, args: ['--help'])
test('client_version', client, args: ['--version'])
browser_test = executable('browser_login_test', 'tests/browser_login_test.cpp', dependencies: browser_dep)
test('browser_login', browser_test)
state_test = executable('discord_state_test', 'tests/discord_state_test.cpp', dependencies: backend_dep)
test('discord_state', state_test)
layout_test = executable('message_layout_test', 'tests/message_layout_test.cpp', dependencies: message_layout_dep, link_with: emoji_catalog)
test('message_layout', layout_test, args: [meson.project_source_root() / 'assets'])
completion_test = executable('autocomplete_test', 'tests/autocomplete_test.cpp', include_directories: include_directories('src'))
test('autocomplete', completion_test)
gateway_test = executable('gateway_test', 'tests/gateway_test.cpp', dependencies: backend_dep)
test('gateway', find_program('python3'), args: [files('tests/gateway_fixture.py'), gateway_test], timeout: 15)
session_test = executable('session_store_test', 'tests/session_store_test.cpp', 'src/app/session_store.cpp', dependencies: backend_dep)
test('session_store', session_test)
font_catalog_test = executable('font_catalog_test', 'tests/font_catalog_test.cpp', 'src/app/font_catalog.cpp',
  include_directories: include_directories('src'), dependencies: dependency('fontconfig'))
test('font_catalog', font_catalog_test)

install_data('assets/dev.lysec.Cressida.desktop', install_dir: get_option('datadir') / 'applications')
editor_test = executable('text_edit_test', 'tests/text_edit_test.cpp', dependencies: render_dep)
test('text_edit', editor_test)

uploads_test = executable('uploads_test', 'tests/uploads_test.cpp', dependencies: backend_dep)
test('uploads', find_program('python3'), args: [files('tests/uploads_fixture.py'), uploads_test], timeout: 12)

presence_test = executable('presence_test', 'tests/presence_test.cpp', dependencies: backend_dep)
test('presence', presence_test)

read_state_test = executable('read_state_test', 'tests/read_state_test.cpp', dependencies: backend_dep)
test('read_state', read_state_test)
role_style_test = executable('role_style_test', 'tests/role_style_test.cpp', dependencies: backend_dep)
test('role_style', role_style_test)
profile_test = executable('profile_test', 'tests/profile_test.cpp', dependencies: backend_dep)
test('profile', profile_test)
thread_test = executable('threads_test', 'tests/threads_test.cpp', dependencies: backend_dep)
test('threads', thread_test)
channel_features_test = executable('channel_features_test', 'tests/channel_features_test.cpp', dependencies: backend_dep)
test('channel_features', channel_features_test)
control_test = executable('native_controls_test', 'tests/native_controls_test.cpp', dependencies: render_dep)
test('native_controls', control_test)

directory_test = executable('user_directory_test', 'tests/user_directory_test.cpp', dependencies: backend_dep)
test('user_directory', directory_test)

interactions_test = executable('interactions_test', 'tests/interactions_test.cpp', dependencies: backend_dep)
test('interactions', interactions_test)

context_menu_test = executable('context_menu_test', 'tests/context_menu_test.cpp', dependencies: render_dep)
test('context_menu', context_menu_test)

credentials_test = executable('credentials_test', 'tests/credentials_test.cpp', dependencies: credential_dep)
test('credentials', credentials_test)
secret_service_test = executable('secret_service_test', 'tests/secret_service_test.cpp', dependencies: credential_dep)
test('secret_service', secret_service_test,
  env: ['DBUS_SESSION_BUS_ADDRESS=unix:path=/cressida-test-no-session-bus'], timeout: 10)
credential_client_test = executable('credential_client_test', 'tests/credential_client_test.cpp', dependencies: backend_dep)
test('credential_client', credential_client_test)

gifs_test = executable('gifs_test', 'tests/gifs_test.cpp', dependencies: backend_dep)
test('gifs', gifs_test)
animation_test = executable('animation_test', 'tests/animation_test.cpp', dependencies: media_dep)
test('animation', find_program('python3'), args: [files('tests/animation_fixture.py'), find_program('ffmpeg'), animation_test], timeout: 60)
scroll_test = executable('scroll_motion_test', 'tests/scroll_motion_test.cpp', dependencies: render_dep)
test('scroll_motion', scroll_test)
emoji_test = executable('emoji_catalog_test', 'tests/emoji_catalog_test.cpp', link_with: emoji_catalog, dependencies: [backend_dep, markdown_dep])
test('emoji_catalog', emoji_test, args: [meson.project_source_root() / 'assets'])
unread_timeline_test = executable('unread_timeline_test', 'tests/unread_timeline_test.cpp', dependencies: backend_dep)
test('unread_timeline', unread_timeline_test)
message_history_test = executable('message_history_test', 'tests/message_history_test.cpp', dependencies: backend_dep)
test('message_history', message_history_test)
message_group_test = executable('message_group_test', 'tests/message_group_test.cpp', dependencies: backend_dep)
test('message_group', message_group_test)
message_log_test = executable('message_log_test', 'tests/message_log_test.cpp', dependencies: backend_dep)
test('message_log', message_log_test)
system_message_test = executable('system_message_test', 'tests/system_message_test.cpp', dependencies: backend_dep)
test('system_message', system_message_test)
message_mentions_test = executable('message_mentions_test', 'tests/message_mentions_test.cpp', dependencies: backend_dep)
test('message_mentions', message_mentions_test)
video_test = executable('video_player_test', 'tests/video_player_test.cpp', dependencies: [video_dep, dependency('egl'), dependency('glesv2')])
test('video_player', find_program('python3'), args: [files('tests/video_fixture.py'), find_program('ffmpeg'), video_test], timeout: 40)
