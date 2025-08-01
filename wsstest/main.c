/*
 * SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <xcb/xcb.h>
#include <xcb/xcb_util.h>

extern char **environ;

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))
#define COUNTOF(array) (sizeof(array) / sizeof(array)[0])

static int connect_wl(
    struct wl_display **out_wl, struct wl_registry **out_wl_registry) {
  struct wl_display *wl = NULL;
  struct wl_registry *wl_registry = NULL;

  wl = wl_display_connect(NULL);
  if (wl == NULL) {
    perror("wl_display_connect");
    return -1;
  }

  wl_registry = wl_display_get_registry(wl);
  if (wl_registry == NULL) {
    perror("wl_display_get_registry");
    wl_display_disconnect(wl);
    return -1;
  }

  *out_wl = wl;
  *out_wl_registry = wl_registry;
  return 0;
}

static void cleanup_wl_display(struct wl_display **wl) {
  if (*wl == NULL) {
    return;
  }
  wl_display_disconnect(*wl);
  *wl = NULL;
}

static void cleanup_wl_registry(struct wl_registry **wl_registry) {
  if (*wl_registry == NULL) {
    return;
  }
  wl_registry_destroy(*wl_registry);
  *wl_registry = NULL;
}

static int connect_x11(
    xcb_connection_t **out_x11, xcb_screen_t **out_screen_preferred) {
  xcb_connection_t *x11 = NULL;
  xcb_screen_t *screen_preferred = NULL;

  int screen_preferred_n = 0;
  int error = 0;

  x11 = xcb_connect(NULL, &screen_preferred_n);
  error = xcb_connection_has_error(x11);
  if (error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", error);
    return -1;
  }

  screen_preferred = xcb_aux_get_screen(x11, screen_preferred_n);

  *out_x11 = x11;
  *out_screen_preferred = screen_preferred;
  return 0;
}

static void cleanup_x11_connection(xcb_connection_t **x11) {
  if (*x11 == NULL) {
    return;
  }
  xcb_disconnect(*x11);
  *x11 = NULL;
}

static xcb_window_t create_window(
    xcb_connection_t *x11, const xcb_screen_t *screen) {
  xcb_window_t window = 0;

  window = xcb_generate_id(x11);
  xcb_create_window(x11, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0,
      screen->width_in_pixels, screen->height_in_pixels, 0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
  xcb_map_window(x11, window);

  return window;
}

static pid_t launch_screensaver(
    xcb_window_t window, const char *screensaver_path) {
  /* * 2 for nybbles (halves of bytes), + 3 for "0x" and NUL terminator */
  char window_id_string[sizeof(xcb_window_t) * 2 + 3] = {0};
  int error = 0;
  pid_t screensaver_pid = 0;
  const char *const screensaver_argv[] = {screensaver_path, "--root", NULL};

  /* lazy, ideally i'd make a copy of environ and work on that */
  snprintf(window_id_string, COUNTOF(window_id_string), "0x%" PRIx32, window);
  setenv("XSCREENSAVER_WINDOW", window_id_string, 1);

  /*
   * wl and x11 sockets are cloexec, no need to close explicitly.
   *
   * argv is specified to not be modified by posix_spawn (described in the
   * manual for the exec family of functions, explained under Rationale) so the
   * const-discarding cast is safe in theory.
   */
  error = posix_spawn(&screensaver_pid, screensaver_path, NULL, NULL,
      (char *const *)screensaver_argv, environ);
  if (error != 0) {
    perror("posix_spawn");
    return -1;
  }

  return screensaver_pid;
}

static void cleanup_screensaver(pid_t *screensaver_pid) {
  int error = 0;
  siginfo_t screensaver_info = {0};

  if (*screensaver_pid <= 0) {
    return;
  }

  error = kill(*screensaver_pid, SIGTERM);
  /* zombie processes count as existing, no need to exempt ESRCH */
  if (error != 0) {
    perror("kill");
    return;
  }

  error = waitid(P_PID, *screensaver_pid, &screensaver_info, WEXITED);
  if (error != 0) {
    perror("waitid");
    return;
  }

  psiginfo(&screensaver_info, NULL);

  if (screensaver_info.si_code == CLD_EXITED) {
    fprintf(stderr, "Child exited normally: %d\n", screensaver_info.si_status);
  } else {
    psignal(screensaver_info.si_status, "Child exited by an uncaught signal");
  }

  *screensaver_pid = 0;
}

static int read_wl_events(struct wl_display *wl) {
  int error = 0;

  error = wl_display_prepare_read(wl);
  if (error != 0) {
    /* queue not being empty is not an error */
    fputs("wl_display_prepare_read: Pending queue\n", stderr);
    return 0;
  }

  error = wl_display_read_events(wl);
  if (error != 0) {
    perror("wl_display_read_events");
    return -1;
  }

  return 0;
}

static void handle_wl_registry_global(void *data,
    struct wl_registry *wl_registry, uint32_t name, const char *interface,
    uint32_t version) {
  (void)data;
  (void)wl_registry;
  fputs("Wayland global\n", stderr);
  fprintf(stderr, "  name: %" PRIu32 "\n", name);
  fprintf(stderr, "  interface: %s\n", interface);
  fprintf(stderr, "  version: %" PRIu32 "\n", version);
}

static void handle_wl_registry_global_remove(
    void *data, struct wl_registry *wl_registry, uint32_t name) {
  (void)data;
  (void)wl_registry;
  fputs("Wayland global_remove\n", stderr);
  fprintf(stderr, "  name: %" PRIu32 "\n", name);
}

static const struct wl_registry_listener registry_listener = {
    handle_wl_registry_global, handle_wl_registry_global_remove};

static void cleanup_x11_event(xcb_generic_event_t **event) {
  if (*event == NULL) {
    return;
  }
  free(*event);
  *event = NULL;
}

static int handle_x11_event(xcb_connection_t *x11) {
  CLEANUP(x11_event) xcb_generic_event_t *event = NULL;
  uint8_t event_type = 0;
  xcb_generic_error_t *event_error = NULL;

  event = xcb_poll_for_event(x11);
  if (event == NULL) {
    fputs("xcb_poll_for_event: No events\n", stderr);
    return 0;
  }

  event_type = XCB_EVENT_RESPONSE_TYPE(event);
  fprintf(stderr, "X Event: %" PRId8 " (%s)\n", event_type,
      xcb_event_get_label(event_type));

  switch (event_type) {
  case 0: /* X_Error */
    /* ideally i could just use XmuPrintDefaultErrorMessage, but that wants an
     * Xlib Display while i only have an xcb_connection_t */
    event_error = (xcb_generic_error_t *)event;
    fprintf(stderr, "  Error code:    %" PRId8 " (%s)\n",
        event_error->error_code,
        xcb_event_get_error_label(event_error->error_code));
    fprintf(stderr, "  Major opcode:  %" PRId8 " (%s)\n",
        event_error->major_code,
        xcb_event_get_request_label(event_error->major_code));
    fprintf(
        stderr, "  Resource ID:   0x%" PRIx32 "\n", event_error->resource_id);
    /* Xlib also shows the "current" serial, but xcb doesn't seem to expose
     * this for us at all */
    fprintf(stderr, "  Serial number: %" PRId16 "\n", event_error->sequence);
    /* break the event loop on any X_Error. Xlib makes an exception for
     * error_code 17 BadImplementation (server does not implement operation) but
     * i don't care */
    return -1;

  default:
    fprintf(stderr, "  Serial number: %" PRId16 "\n", event->sequence);
    break;
  }

  return 1;
}

static int poll_connections(struct wl_display *wl, xcb_connection_t *x11) {
  int error = 0;
  struct pollfd connection_poll[2] = {0};
  int poll_ready = 0;

  error = wl_display_get_error(wl);
  if (error != 0) {
    fprintf(stderr, "wl_display_get_error: %s\n", strerror(error));
    return -1;
  }

  error = xcb_connection_has_error(x11);
  if (error == XCB_CONN_ERROR) {
    /* server closed the connection, perhaps the user closed the window */
    fputs("xcb_connection_has_error: Connection closed\n", stderr);
    return 0;
  }
  if (error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", error);
    return -1;
  }

  connection_poll[0].fd = wl_display_get_fd(wl);
  connection_poll[0].events = POLLIN;
  connection_poll[1].fd = xcb_get_file_descriptor(x11);
  connection_poll[1].events = POLLIN;

  poll_ready = poll(connection_poll, COUNTOF(connection_poll), 5000);
  if (poll_ready < 0) {
    perror("poll");
    return -1;
  }

  /* simplification: if any connection is ready for reading (or error), go ahead
   * and try reading from both */
  return poll_ready;
}

static int flush_wl(struct wl_display *wl) {
  struct pollfd flush_poll[1] = {0};
  int error = 0;

  flush_poll[0].fd = wl_display_get_fd(wl);
  flush_poll[0].events = POLLOUT;

  do {
    /* lazy, ideally check poll status */
    poll(flush_poll, COUNTOF(flush_poll), -1);
    /* doesn't block, instead errors with EAGAIN */
    error = wl_display_flush(wl);
  } while (error < 0 && errno == EAGAIN);
  /* if the connection was closed, continue and try to read the error later */
  if (error < 0 && errno != EPIPE) {
    perror("wl_display_flush");
    return -1;
  }
  fprintf(stderr, "wl_display_flush: %d\n", error);

  return 0;
}

int main(int argc, char **argv) {
  const char *screensaver_path = NULL;
  int error = 0;
  CLEANUP(wl_display) struct wl_display *wl = NULL;
  CLEANUP(wl_registry) struct wl_registry *wl_registry = NULL;
  CLEANUP(x11_connection) xcb_connection_t *x11 = NULL;
  xcb_screen_t *screen_preferred = NULL;
  xcb_window_t window = 0;
  CLEANUP(screensaver) pid_t screensaver_pid = 0;
  int poll_ready = 0;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return EXIT_FAILURE;
  }
  screensaver_path = argv[1];

  error = connect_wl(&wl, &wl_registry);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  error = wl_registry_add_listener(wl_registry, &registry_listener, NULL);
  if (error != 0) {
    fputs("wl_registry_add_listener: listener already set\n", stderr);
    return EXIT_FAILURE;
  }

  error = flush_wl(wl);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  error = connect_x11(&x11, &screen_preferred);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  window = create_window(x11, screen_preferred);
  fprintf(stderr, "create_window: 0x%" PRIx32 "\n", window);

  error = xcb_flush(x11);
  if (error <= 0) {
    fprintf(stderr, "xcb_flush: %d\n", -error);
    return EXIT_FAILURE;
  }
  /* unsure what positive error values mean, besides success */
  /* i suspect that the only success value is 1 */
  fprintf(stderr, "xcb_flush: %d\n", error);

  screensaver_pid = launch_screensaver(window, screensaver_path);
  if (screensaver_pid <= 0) {
    return EXIT_FAILURE;
  }
  fprintf(stderr, "launch_screensaver: %ld\n", (long)screensaver_pid);

  /*
   * wl_display_dispatch and xcb_wait_for_event can't timeout (and since we're
   * looping over two event domains we can't use blocking calls anyway), use
   * poll instead. make sure to handle all pending events before polling the
   * connection, otherwise we might leave events stuck in a queue for a while.
   */
  poll_ready = 1;
  while (poll_ready > 0) {
    /* handle x11 first because it processes one event at a time */
    error = handle_x11_event(x11);
    if (error < 0) {
      break;
    }
    if (error > 0) {
      continue;
    }

    /* xcb_poll_for_event also checks the connection for new events, but
     * wl_display_dispatch_pending doesn't, so we need to read for it first */
    error = read_wl_events(wl);
    if (error != 0) {
      break;
    }
    /* however, it dispatches all pending events in one go (i think!) */
    error = wl_display_dispatch_pending(wl);
    if (error < 0) {
      perror("wl_display_dispatch_pending");
      break;
    }
    fprintf(stderr, "wl_display_dispatch_pending: %d\n", error);
    if (error > 0) {
      error = 0;
    }

    poll_ready = poll_connections(wl, x11);
    fprintf(stderr, "poll_connections: %d\n", poll_ready);
  }
  if (error != 0 || poll_ready < 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
