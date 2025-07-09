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
