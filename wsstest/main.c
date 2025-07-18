#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
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

int main(int argc, char **argv) {
  char *screensaver_path = NULL;

  if (argc == 2) {
    screensaver_path = argv[1];
  } else {
    fprintf(stderr, "Missing screensaver\n");
    return EXIT_FAILURE;
  }

  int connection_error = 0, flush_error = 0;
  CLEANUP(generic_error) xcb_generic_error_t *generic_error = NULL;

  int screen_preferred_n = 0;
  CLEANUP(connection)
  xcb_connection_t *connection = xcb_connect(NULL, &screen_preferred_n);
  connection_error = xcb_connection_has_error(connection);
  if (connection_error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", connection_error);
    return EXIT_FAILURE;
  }

  const xcb_setup_t *setup = xcb_get_setup(connection);

  xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
  xcb_screen_t *screen_preferred = screen_iter.data;
  for (int screen_i = 0; screen_iter.rem > 0;
       screen_i++, xcb_screen_next(&screen_iter)) {
    if (screen_i == screen_preferred_n) {
      screen_preferred = screen_iter.data;
      break;
    }
  }

  xcb_window_t window = xcb_generate_id(connection);
  xcb_void_cookie_t cookie_create_window = xcb_create_window_checked(
      connection, XCB_COPY_FROM_PARENT, window, screen_preferred->root, 0, 0,
      400, 400, 10, XCB_WINDOW_CLASS_INPUT_OUTPUT,
      screen_preferred->root_visual, 0, NULL);
  xcb_void_cookie_t cookie_map_window =
      xcb_map_window_checked(connection, window);

  flush_error = xcb_flush(connection);
  if (flush_error <= 0) {
    fprintf(stderr, "xcb_flush: %d\n", -flush_error);
    return EXIT_FAILURE;
  }

  generic_error = xcb_request_check(connection, cookie_create_window);
  if (generic_error != NULL) {
    fprintf(stderr, "xcb_create_window_checked\n");
    return EXIT_FAILURE;
  }
  generic_error = xcb_request_check(connection, cookie_map_window);
  if (generic_error != NULL) {
    fprintf(stderr, "xcb_map_window_checked\n");
    return EXIT_FAILURE;
  }

  pid_t screensaver_pid = 1;
  // pid_t screensaver_pid = fork();
  if (screensaver_pid < 0) {
    perror("fork");
    return EXIT_FAILURE;
  }
  if (screensaver_pid == 0) {
    char window_id_string[11] = {0};
    snprintf(window_id_string, 11, "0x%08.8" PRIx32, window);
    setenv("XSCREENSAVER_WINDOW", window_id_string, 1);
    execl(screensaver_path, screensaver_path, "-root");
    perror("execl");
    return EXIT_FAILURE;
  }

  sleep(5);

  return EXIT_SUCCESS;
}
