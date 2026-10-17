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
backend = static_library('cressida-backend', 'src/discord/models.cpp', 'src/discord/client.cpp',
  'src/discord/settings.cpp', 'src/discord/read_state.cpp', 'src/discord/assets.cpp', 'src/discord/gateway.cpp', 'src/discord/uploads.cpp',
  include_directories: include_directories('src'),
  dependencies: backend_deps,
)
backend_dep = declare_dependency(link_with: backend,
  include_directories: include_directories('src'), dependencies: backend_deps)

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
message_layout = static_library('message-layout', 'src/app/message_layout.cpp', dependencies: [render_dep, backend_dep, markdown_dep])
message_layout_dep = declare_dependency(link_with: message_layout, dependencies: [render_dep, backend_dep, markdown_dep])
client = executable('cressida',
  'src/main.cpp', 'src/app/window.cpp', 'src/app/media_view.cpp', 'src/app/user_settings.cpp', 'src/app/native_controls.cpp', 'src/app/client_application.cpp', 'src/app/session_store.cpp', xdg_sources, protocol_sources[0], protocol_sources[1],
  dependencies: [render_dep, backend_dep, browser_dep, markdown_dep],
  link_with: message_layout,
  cpp_args: client_args,
  install: true,
)

install_subdir('src',
  install_dir: get_option('includedir') / 'cressida',
  strip_directory: true,
  exclude_files: render_source_paths + control_source_paths + ['meson.build', 'main.cpp', 'app/window.cpp', 'app/media_view.cpp', 'app/user_settings.cpp', 'app/native_controls.cpp', 'app/client_application.cpp', 'app/session_store.cpp', 'app/message_layout.cpp', 'discord/client.cpp', 'discord/models.cpp', 'discord/settings.cpp', 'discord/read_state.cpp', 'discord/assets.cpp', 'discord/gateway.cpp', 'discord/uploads.cpp', 'auth/browser_login.cpp'],
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
layout_test = executable('message_layout_test', 'tests/message_layout_test.cpp', dependencies: message_layout_dep)
test('message_layout', layout_test)
gateway_test = executable('gateway_test', 'tests/gateway_test.cpp', dependencies: backend_dep)
test('gateway', find_program('python3'), args: [files('tests/gateway_fixture.py'), gateway_test], timeout: 15)
session_test = executable('session_store_test', 'tests/session_store_test.cpp', 'src/app/session_store.cpp', dependencies: backend_dep)
test('session_store', session_test)

install_data('assets/dev.lysec.Cressida.desktop', install_dir: get_option('datadir') / 'applications')
editor_test = executable('text_edit_test', 'tests/text_edit_test.cpp', dependencies: render_dep)
test('text_edit', editor_test)

uploads_test = executable('uploads_test', 'tests/uploads_test.cpp', dependencies: backend_dep)
test('uploads', find_program('python3'), args: [files('tests/uploads_fixture.py'), uploads_test], timeout: 12)

presence_test = executable('presence_test', 'tests/presence_test.cpp', dependencies: backend_dep)
test('presence', presence_test)

read_state_test = executable('read_state_test', 'tests/read_state_test.cpp', dependencies: backend_dep)
test('read_state', read_state_test)
control_test = executable('native_controls_test', 'tests/native_controls_test.cpp', dependencies: render_dep)
test('native_controls', control_test)
