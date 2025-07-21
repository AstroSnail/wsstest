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
#include <xcb/xcb_event.h>

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))

static void cleanup_connection(xcb_connection_t **connection) {
  if (*connection != NULL) {
    xcb_disconnect(*connection);
    *connection = NULL;
  }
}

static void cleanup_event(xcb_generic_event_t **event) {
  if (*event != NULL) {
    free(*event);
    *event = NULL;
  }
}

static int connect(xcb_connection_t **out_connection,
                   xcb_screen_t **out_screen_preferred) {
  xcb_connection_t *connection = NULL;
  xcb_screen_t *screen_preferred = NULL;

  int screen_preferred_n = 0;
  int connection_error = 0;
  const xcb_setup_t *setup = NULL;
  xcb_screen_iterator_t screen_iter = {0};
  int screen_i = 0;

  connection = xcb_connect(NULL, &screen_preferred_n);
  connection_error = xcb_connection_has_error(connection);
  if (connection_error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", connection_error);
    return -1;
  }

  setup = xcb_get_setup(connection);

  screen_iter = xcb_setup_roots_iterator(setup);
  screen_preferred = screen_iter.data;
  for (screen_i = 0; screen_iter.rem > 0;
       screen_i++, xcb_screen_next(&screen_iter)) {
    if (screen_i == screen_preferred_n) {
      screen_preferred = screen_iter.data;
      break;
    }
  }

  *out_connection = connection;
  *out_screen_preferred = screen_preferred;
  return 0;
}

static xcb_window_t window_create(xcb_connection_t *connection,
                                  const xcb_screen_t *screen) {
  xcb_window_t window = 0;

  window = xcb_generate_id(connection);
  xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0,
                    0, screen->width_in_pixels, screen->height_in_pixels, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0,
                    NULL);
  xcb_map_window(connection, window);

  return window;
}

static pid_t screensaver_launch(xcb_window_t window,
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

static int screensaver_kill(pid_t screensaver_pid) {
  int error = 0;
  long child_pid = 0; /* pid_t fits in a signed long */
  int screensaver_status = 0;
  const char *signal_desc = NULL;

  error = kill(screensaver_pid, SIGTERM);
  /* zombie processes count as existing, no need to exempt ESRCH */
  if (error != 0) {
    perror("kill");
    return -1;
  }

  child_pid = waitpid(screensaver_pid, &screensaver_status, 0);
  if (child_pid < 0) {
    perror("waitpid");
    return -1;
  }
  if (child_pid != screensaver_pid) {
    fprintf(stderr, "Whose child is this? %ld\n", child_pid);
    return -1;
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

  return 0;
}

static int event_handle(xcb_connection_t *connection) {
  CLEANUP(event) xcb_generic_event_t *event = NULL;
  uint8_t event_type = 0;
  const char *event_label = NULL;

  event = xcb_poll_for_event(connection);
  if (event == NULL) {
    return 0;
  }

  event_type = XCB_EVENT_RESPONSE_TYPE(event);
  event_label = xcb_event_get_label(event_type);
  fprintf(stdout, "Unknown event: %d %s\n", (int)event_type, event_label);

  return 1;
}

static int connection_poll(xcb_connection_t *connection) {
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
  pid_t screensaver_pid = 0;
  int poll_ready = 1; /* enter the loop */

  if (argc == 2) {
    screensaver_path = argv[1];
  } else {
    fprintf(stderr, "Missing screensaver\n");
    return EXIT_FAILURE;
  }

  error = connect(&connection, &screen_preferred);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  window = window_create(connection, screen_preferred);

  error = xcb_flush(connection);
  if (error <= 0) {
    fprintf(stderr, "xcb_flush: %d\n", -error);
    return EXIT_FAILURE;
  }
  /* unsure what positive error values mean, besides success */

  screensaver_pid = screensaver_launch(window, screensaver_path);
  if (screensaver_pid <= 0) {
    return EXIT_FAILURE;
  }

  /* xcb_wait_for_event can't timeout, use poll instead */
  /* make sure to handle all pending events before polling the connection */
  while (poll_ready > 0) {
    error = event_handle(connection);
    if (error < 0) {
      return EXIT_FAILURE;
    }
    if (error == 0) {
      poll_ready = connection_poll(connection);
    }
  }
  if (poll_ready < 0) {
    return EXIT_FAILURE;
  }

  error = screensaver_kill(screensaver_pid);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
