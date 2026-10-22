project('gitcusers', 'c', version: '1.0.0', meson_version: '>=1.1.0',
  default_options: ['c_std=c11', 'buildtype=release', 'warning_level=3', 'b_lto=true'])

cc = meson.get_compiler('c')
# Compile cJSON with the selected libc rather than mixing host and musl objects.
args = cc.get_supported_arguments('-ffunction-sections', '-fdata-sections')
links = cc.get_supported_link_arguments('-Wl,--gc-sections')
gitcusers = executable('gitcusers',
  'src/main.c', 'src/util.c', 'src/config.c', 'src/status.c',
  'vendor/cjson/cJSON.c',
  include_directories: include_directories('src', 'vendor/cjson'),
  c_args: args, link_args: links, install: true)

python = find_program('python3', required: get_option('tests'))
if get_option('tests')
  test('integration', python, args: [files('tests/integration.py'), gitcusers], timeout: 120)
endif
