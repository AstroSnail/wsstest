#!/bin/sh

# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: MIT
#
# take a glob in the first parameter, match it against files, and print the
# first match. fail if the glob doesn't match a file.

set -eu
IFS=
# word splitting has been suppressed, we intentionally want pathname expansion
# shellcheck disable=SC2086
set -- $1
[ -e "$1" ] && printf %s "$1" && exit 0
exit 1
