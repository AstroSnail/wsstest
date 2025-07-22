# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{
  flake.overlays =

    let
      stub = _final: _prev: { };

    in
    {
      inherit stub;
      default = stub;
    };
}
