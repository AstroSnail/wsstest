// vim: set expandtab shiftwidth=2 softtabstop=2:

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <xcb/xcb.h>

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))

static void cleanup_connection(xcb_connection_t **conn) {
  if (*conn != NULL) {
    xcb_disconnect(*conn);
    *conn = NULL;
  }
}

int main(void) {
  int screen_preferred_n = 0;
  CLEANUP(connection)
  xcb_connection_t *conn = xcb_connect(NULL, &screen_preferred_n);
  int conn_error = xcb_connection_has_error(conn);
  if (conn_error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", conn_error);
    return EXIT_FAILURE;
  }

  const xcb_setup_t *setup = xcb_get_setup(conn);

  xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
  xcb_screen_t *screen_preferred = screen_iter.data;
  for (int screen_i = 0; screen_iter.rem > 0;
       screen_i++, xcb_screen_next(&screen_iter)) {
    if (screen_i == screen_preferred_n) {
      screen_preferred = screen_iter.data;
      break;
    }
  }

  printf("Informations of screen %" PRIu32 ":\n", screen_preferred->root);
  printf("  width.........: %" PRIu16 "\n", screen_preferred->width_in_pixels);
  printf("  height........: %" PRIu16 "\n", screen_preferred->height_in_pixels);
  printf("  white pixel...: %08" PRIX32 "\n", screen_preferred->white_pixel);
  printf("  black pixel...: %08" PRIX32 "\n", screen_preferred->black_pixel);

  return EXIT_SUCCESS;
}
