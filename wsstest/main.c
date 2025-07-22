#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_util.h>

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))

static int connect(xcb_connection_t **out_connection,
                   xcb_screen_t **out_screen_preferred) {
  xcb_connection_t *connection = NULL;
  xcb_screen_t *screen_preferred = NULL;

  int screen_preferred_n = 0;
  int connection_error = 0;

  connection = xcb_connect(NULL, &screen_preferred_n);
  connection_error = xcb_connection_has_error(connection);
  if (connection_error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", connection_error);
    return -1;
  }

  screen_preferred = xcb_aux_get_screen(connection, screen_preferred_n);

  *out_connection = connection;
  *out_screen_preferred = screen_preferred;
  return 0;
}

static void cleanup_connection(xcb_connection_t **connection) {
  if (*connection == NULL) {
    return;
  }
  xcb_disconnect(*connection);
  *connection = NULL;
}

static xcb_window_t create_window(xcb_connection_t *connection,
                                  const xcb_screen_t *screen) {
  xcb_window_t window = 0;

  window = xcb_generate_id(connection);
  xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0,
                    0, screen->width_in_pixels, screen->height_in_pixels, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0,
                    NULL);
  xcb_map_window(connection, window);

  fprintf(stdout, "Window: 0x%" PRIx32 "\n", window);

  return window;
}

static pid_t launch_screensaver(xcb_window_t window,
                                const char *screensaver_path) {
  pid_t screensaver_pid = 0;
  enum { window_id_len = sizeof(xcb_window_t) * 2 + 3 };
  char window_id_string[window_id_len] = {0};

  screensaver_pid = fork();
  if (screensaver_pid < 0) {
    perror("fork");
    return -1;
  }

  if (screensaver_pid > 0) {
    return screensaver_pid;
  }

  snprintf(window_id_string, window_id_len, "0x%" PRIx32, window);
  setenv("XSCREENSAVER_WINDOW", window_id_string, 1);

  /* TODO: any cleanup before exec'ing the screensaver? */
  execl(screensaver_path, screensaver_path, "--root", NULL);
  perror("execl");
  return -1;
}

static void cleanup_screensaver(pid_t *screensaver_pid) {
  int error = 0;
  long child_pid = 0; /* pid_t fits in a signed long */
  int screensaver_status = 0;
  const char *signal_desc = NULL;

  if (*screensaver_pid <= 0) {
    return;
  }

  error = kill(*screensaver_pid, SIGTERM);
  /* zombie processes count as existing, no need to exempt ESRCH */
  if (error != 0) {
    perror("kill");
    return;
  }

  child_pid = waitpid(*screensaver_pid, &screensaver_status, 0);
  if (child_pid < 0) {
    perror("waitpid");
    return;
  }
  if (child_pid != *screensaver_pid) {
    fprintf(stderr, "Whose child is this? %ld\n", child_pid);
    return;
  }

  if (WIFEXITED(screensaver_status)) {
    error = WEXITSTATUS(screensaver_status);
    fprintf(stdout, "Screensaver exited normally: %d\n", error);
  }

  if (WIFSIGNALED(screensaver_status)) {
    error = WTERMSIG(screensaver_status);
    signal_desc = strsignal(error);
    fprintf(stdout, "Screensaver exited by an uncaught signal: %d %s\n", error,
            signal_desc);
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

static int handle_event(xcb_connection_t *connection) {
  CLEANUP(event) xcb_generic_event_t *event = NULL;
  uint8_t event_type = 0;
  const char *event_label = NULL;
  xcb_generic_error_t *event_error = NULL;
  const char *event_error_label = NULL;
  const char *event_request_label = NULL;

  event = xcb_poll_for_event(connection);
  if (event == NULL) {
    return 0;
  }

  event_type = XCB_EVENT_RESPONSE_TYPE(event);
  event_label = xcb_event_get_label(event_type);
  fprintf(stdout, "X Event: %" PRId8 " (%s)\n", event_type, event_label);

  switch (event_type) {
  case 0: /* X_Error */
    /* ideally i could just use XmuPrintDefaultErrorMessage, but that wants an
     * Xlib Display while i only have an xcb_connection_t */
    event_error = (xcb_generic_error_t *)event;
    event_error_label = xcb_event_get_error_label(event_error->error_code);
    event_request_label = xcb_event_get_request_label(event_error->major_code);
    fprintf(stdout, "  Error code:    %" PRId8 " (%s)\n",
            event_error->error_code, event_error_label);
    fprintf(stdout, "  Major opcode:  %" PRId8 " (%s)\n",
            event_error->major_code, event_request_label);
    fprintf(stdout, "  Resource ID:   0x%" PRIx32 "\n",
            event_error->resource_id);
    /* Xlib also shows the "current" serial, but xcb doesn't seem to expose
     * this for us at all */
    fprintf(stdout, "  Serial number: %" PRId16 "\n", event_error->sequence);
    /* break the event loop on any X_Error. Xlib makes an exception for
     * error_code 17 BadImplementation (server does not implement operation) but
     * i don't care */
    return -1;
  default:
    fprintf(stdout, "  Unhandled\n");
    break;
  }

  return 1;
}

static int poll_connection(xcb_connection_t *connection) {
  int connection_error = 0;
  struct pollfd connection_poll = {0};
  int poll_ready = 0;

  connection_error = xcb_connection_has_error(connection);
  if (connection_error == XCB_CONN_ERROR) {
    /* server closed the connection, perhaps the user closed the window */
    return 0;
  }
  if (connection_error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", connection_error);
    return -1;
  }

  connection_poll.fd = xcb_get_file_descriptor(connection);
  connection_poll.events = POLLIN;

  poll_ready = poll(&connection_poll, 1, 5000);
  if (poll_ready < 0) {
    perror("poll");
  }

  return poll_ready;
}

int main(int argc, char **argv) {
  const char *screensaver_path = NULL;
  int error = 0;
  CLEANUP(connection) xcb_connection_t *connection = NULL;
  xcb_screen_t *screen_preferred = NULL;
  xcb_window_t window = 0;
  CLEANUP(screensaver) pid_t screensaver_pid = 0;
  int poll_ready = 0;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return EXIT_FAILURE;
  }
  screensaver_path = argv[1];

  error = connect(&connection, &screen_preferred);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  window = create_window(connection, screen_preferred);

  error = xcb_flush(connection);
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
    error = handle_event(connection);
    if (error < 0) {
      break;
    }
    if (error == 0) {
      poll_ready = poll_connection(connection);
    }
  }
  if (poll_ready < 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
