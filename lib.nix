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
