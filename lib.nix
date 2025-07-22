# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{ lib, ... }:

{
  lib.util.appOutputs = lib.mapAttrs (
    _name: drv: {
      type = "app";
      program = lib.getExe drv;
      meta.description = drv.meta.description;
    }
  );
}
