/*
 * SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

/*
 * the only c99 feature used in the header seems to be inline. and, at that,
 * only in wayland-util.h for the wl_fixed_(to|from)_(double|int) functions,
 * which are also marked static and defined in the header. to my knowledge, this
 * means no linkage risk should arise from #defining it to the empty string, and
 * the compiler should be able to inline it anyway whenever it decides best.
 */
#define inline
#include <wayland-client-core.h>
#undef inline

#include <xcb/xcb.h>
#include <xcb/xcb_util.h>

extern char **environ;

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))

static int connect_x11(xcb_connection_t **out_connection_x11,
                       xcb_screen_t **out_screen_preferred) {
  xcb_connection_t *connection_x11 = NULL;
  xcb_screen_t *screen_preferred = NULL;

  int screen_preferred_n = 0;
  int connection_error = 0;

  connection_x11 = xcb_connect(NULL, &screen_preferred_n);
  connection_error = xcb_connection_has_error(connection_x11);
  if (connection_error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", connection_error);
    return -1;
  }

  screen_preferred = xcb_aux_get_screen(connection_x11, screen_preferred_n);

  *out_connection_x11 = connection_x11;
  *out_screen_preferred = screen_preferred;
  return 0;
}

static void cleanup_connection_x11(xcb_connection_t **connection_x11) {
  if (*connection_x11 == NULL) {
    return;
  }
  xcb_disconnect(*connection_x11);
  *connection_x11 = NULL;
}

static xcb_window_t create_window(xcb_connection_t *connection_x11,
                                  const xcb_screen_t *screen) {
  xcb_window_t window = 0;

  window = xcb_generate_id(connection_x11);
  xcb_create_window(connection_x11, XCB_COPY_FROM_PARENT, window, screen->root,
                    0, 0, screen->width_in_pixels, screen->height_in_pixels, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0,
                    NULL);
  xcb_map_window(connection_x11, window);

  fprintf(stderr, "Window: 0x%" PRIx32 "\n", window);

  return window;
}

static pid_t launch_screensaver(xcb_window_t window,
                                const char *screensaver_path) {
  enum { window_id_len = sizeof(xcb_window_t) * 2 + 3 };
  char window_id_string[window_id_len] = {0};
  int error = 0;
  pid_t screensaver_pid = 0;
  enum { screensaver_argv_len = 3 }; /* space for argv[0] and ending NULL */
  const char *screensaver_argv[screensaver_argv_len] = {NULL};

  /* lazy, ideally i'd make a copy of environ and work on that */
  snprintf(window_id_string, window_id_len, "0x%" PRIx32, window);
  setenv("XSCREENSAVER_WINDOW", window_id_string, 1);

  screensaver_argv[0] = screensaver_path;
  screensaver_argv[1] = "--root";
  screensaver_argv[2] = NULL;

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

static void cleanup_event(xcb_generic_event_t **event) {
  if (*event == NULL) {
    return;
  }
  free(*event);
  *event = NULL;
}

static int handle_event(xcb_connection_t *connection_x11) {
  CLEANUP(event) xcb_generic_event_t *event = NULL;
  uint8_t event_type = 0;
  xcb_generic_error_t *event_error = NULL;

  event = xcb_poll_for_event(connection_x11);
  if (event == NULL) {
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
    fprintf(stderr, "  Resource ID:   0x%" PRIx32 "\n",
            event_error->resource_id);
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

static int poll_connection_x11(xcb_connection_t *connection_x11) {
  int connection_error = 0;
  struct pollfd connection_poll = {0};
  int poll_ready = 0;

  connection_error = xcb_connection_has_error(connection_x11);
  if (connection_error == XCB_CONN_ERROR) {
    /* server closed the connection, perhaps the user closed the window */
    return 0;
  }
  if (connection_error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", connection_error);
    return -1;
  }

  connection_poll.fd = xcb_get_file_descriptor(connection_x11);
  connection_poll.events = POLLIN;

  poll_ready = poll(&connection_poll, 1, 5000);
  if (poll_ready < 0) {
    perror("poll");
  }

  return poll_ready;
}

static void cleanup_connection_wl(struct wl_display **connection_wl) {
  if (*connection_wl == NULL) {
    return;
  }
  wl_display_disconnect(*connection_wl);
  *connection_wl = NULL;
}

int main(int argc, char **argv) {
  const char *screensaver_path = NULL;
  CLEANUP(connection_wl) struct wl_display *connection_wl = NULL;
  int error = 0;
  CLEANUP(connection_x11) xcb_connection_t *connection_x11 = NULL;
  xcb_screen_t *screen_preferred = NULL;
  xcb_window_t window = 0;
  CLEANUP(screensaver) pid_t screensaver_pid = 0;
  int poll_ready = 0;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return EXIT_FAILURE;
  }
  screensaver_path = argv[1];

  connection_wl = wl_display_connect(NULL);
  if (connection_wl == NULL) {
    perror("wl_display_connect");
    return EXIT_FAILURE;
  }

  error = connect_x11(&connection_x11, &screen_preferred);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  window = create_window(connection_x11, screen_preferred);

  error = xcb_flush(connection_x11);
  if (error <= 0) {
    fprintf(stderr, "xcb_flush: %d\n", -error);
    return EXIT_FAILURE;
  }
  /* unsure what positive error values mean, besides success */

  screensaver_pid = launch_screensaver(window, screensaver_path);
  if (screensaver_pid <= 0) {
    return EXIT_FAILURE;
  }

  /* xcb_wait_for_event can't timeout, use poll instead */
  /* make sure to handle all pending events before polling the connection */
  poll_ready = 1;
  while (poll_ready > 0) {
    error = handle_event(connection_x11);
    if (error < 0) {
      break;
    }
    if (error == 0) {
      poll_ready = poll_connection_x11(connection_x11);
    }
  }
  if (error < 0 || poll_ready < 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
