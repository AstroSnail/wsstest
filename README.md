<!--
SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>

SPDX-License-Identifier: Apache-2.0
-->

# Wayland ScreenSaver Test

Wayland screen locker that displays XScreenSaver hacks.

## The context

### XScreenSaver

- XScreenSaver supplies a lot of screensaver programs ('hacks' as
  they're called).
- When XScreenSaver locks the screen, it spawns a hack for each monitor,
  and lets them run until the screen is unlocked or enough time passes
  for XScreenSaver to cycle them out for different hacks.
- The hacks themselves are simple programs that draw pretty graphics,
  and aren't part of XScreenSaver's security; if they crash, the
  corresponding monitor stops being drawn to, but the screen remains
  safely locked and can be unlocked as usual.

### Wayland

- XWayland is a full X11 server that can be used to run X11 applications
  in a Wayland display.
- The XScreenSaver hacks, by themselves, run perfectly fine under
  XWayland. XScreenSaver as a whole, however, does not (at this time,
  version 6.12 has special support for Wayland, but it only blanks the
  screen, and doesn't try to lock it).
- Wayland defines a Session lock protocol (ext-session-lock-v1), and
  several screen lockers exist that implement it.
- ext-session-lock-v1 provides a surface for each monitor, to which only
  the screen locker can draw.

## The idea

What if we make an ext-session-lock-v1 screen locker that spawns
XScreenSaver hacks and copies their window contents to the lock screen
surfaces?
