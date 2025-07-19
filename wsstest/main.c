#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <xcb/xcb.h>

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))

static void cleanup_connection(xcb_connection_t **connection) {
  if (*connection != NULL) {
    xcb_disconnect(*connection);
    *connection = NULL;
  }
}

static void cleanup_generic_error(xcb_generic_error_t **generic_error) {
  if (*generic_error != NULL) {
    free(*generic_error);
    *generic_error = NULL;
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

static void window_request(xcb_connection_t *connection,
                           const xcb_screen_t *screen, xcb_window_t *out_window,
                           xcb_void_cookie_t (*cookies)[2]) {
  xcb_window_t window = 0;
  xcb_void_cookie_t *cookiestring = *cookies;

  window = xcb_generate_id(connection);

  cookiestring[0] = xcb_create_window_checked(
      connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0,
      screen->width_in_pixels, screen->height_in_pixels, 0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);

  cookiestring[1] = xcb_map_window_checked(connection, window);

  *out_window = window;
}

static int window_check(xcb_connection_t *connection,
                        const xcb_void_cookie_t (*cookies)[2]) {
  const xcb_void_cookie_t *cookiestring = *cookies;
  CLEANUP(generic_error) xcb_generic_error_t *generic_error = NULL;

  generic_error = xcb_request_check(connection, cookiestring[0]);
  if (generic_error != NULL) {
    fprintf(stderr, "xcb_create_window_checked\n");
    return -1;
  }

  generic_error = xcb_request_check(connection, cookiestring[1]);
  if (generic_error != NULL) {
    fprintf(stderr, "xcb_map_window_checked\n");
    return -1;
  }

  return 0;
}

static int screensaver(xcb_window_t window, const char *screensaver_path) {
  enum { window_id_len = sizeof(xcb_window_t) * 2 + 3 };
  char window_id_string[window_id_len] = {0};
  pid_t screensaver_pid = 0;
  long error = 0; /* compatible with int and pid_t */
  int screensaver_status = 0;
  const char *signal_desc = NULL;

  snprintf(window_id_string, window_id_len, "0x%" PRIx32, window);

  screensaver_pid = fork();
  if (screensaver_pid < 0) {
    perror("fork");
    return -1;
  }

  if (screensaver_pid == 0) {
    setenv("XSCREENSAVER_WINDOW", window_id_string, 1);
    execl(screensaver_path, screensaver_path, "--root", NULL);
    perror("execl");
    return -1;
  }

  sleep(5);

  error = kill(screensaver_pid, SIGTERM);
  /* zombie processes count as existing */
  if (error != 0) {
    perror("kill");
    return -1;
  }

  error = waitpid(screensaver_pid, &screensaver_status, 0);
  if (error < 0) {
    perror("waitpid");
    return -1;
  }
  if (error != screensaver_pid) {
    fprintf(stderr, "whose child is this? %ld\n", error);
    return -1;
  }

  if (WIFEXITED(screensaver_status)) {
    error = WEXITSTATUS(screensaver_status);
    fprintf(stdout, "screensaver exited normally: %ld\n", error);
  }

  if (WIFSIGNALED(screensaver_status)) {
    error = WTERMSIG(screensaver_status);
    signal_desc = strsignal(error);
    fprintf(stdout, "screensaver exited by an uncaught signal: %ld %s\n", error,
            signal_desc);
  }

  return 0;
}

int main(int argc, char **argv) {
  const char *screensaver_path = NULL;
  int error = 0;
  CLEANUP(connection) xcb_connection_t *connection = NULL;
  xcb_screen_t *screen_preferred = NULL;
  xcb_window_t window = 0;
  xcb_void_cookie_t cookies[2] = {0};

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

  window_request(connection, screen_preferred, &window, &cookies);

  error = xcb_flush(connection);
  if (error <= 0) {
    fprintf(stderr, "xcb_flush: %d\n", -error);
    return EXIT_FAILURE;
  }

  error = window_check(connection, &cookies);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  error = screensaver(window, screensaver_path);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
