# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

project('wayland-client-protocols', 'c')

pkgconfig = import('pkgconfig')

client_dep = dependency('wayland-client')

protocols_dep = dependency('wayland-protocols')
protocols_data = protocols_dep.get_variable(pkgconfig: 'pkgdatadir')

scanner_dep = dependency('wayland-scanner')
scanner_prog = find_program(scanner_dep.get_variable(pkgconfig: 'wayland_scanner'))

header_install_dir = get_option('includedir') / 'wayland-client-protocols'

extensions = get_option('extensions')
codes = []
headers = []
foreach protocol : extensions
  protocol_file = '@0@.xml'.format(protocol)
  protocol_path_glob = join_paths(protocols_data, '*', '*', protocol_file)
  protocol_path = run_command('./glob.sh', protocol_path_glob, check : true).stdout()

  code_path = '@0@.c'.format(protocol)
  code = custom_target(
    code_path,
    command: [
      scanner_prog,
      'public-code',
      '@INPUT@',
      '@OUTPUT@',
    ],
    input: protocol_path,
    output: code_path,
  )
  codes += [code]

  header_path = '@0@.h'.format(protocol)
  header = custom_target(
    header_path,
    command: [
      scanner_prog,
      'client-header',
      '@INPUT@',
      '@OUTPUT@',
    ],
    input: protocol_path,
    output: header_path,
    install: true,
    install_dir: header_install_dir,
  )
  headers += [header]
endforeach

wayland_protocols_client_lib = library(
  'wayland-client-protocols',
  sources: codes,
  dependencies: [ client_dep ],
  install: true,
)

pkgconfig.generate(
  wayland_protocols_client_lib,
  name: 'Wayland Protocols Client',
  description: 'Wayland protocol extensions client side library',
  version: protocols_dep.version(),
  filebase: 'wayland-client-protocols',
)
