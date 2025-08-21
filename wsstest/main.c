/*
 * SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-protocols-client/ext-session-lock-v1.h>
#include <xcb/xcb.h>
#include <xcb/xcb_util.h>

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))
#define COUNTOF(array) (sizeof(array) / sizeof(array)[0])

static const char shm_name[] = "/wsstest_shm";
enum {
  width = 1024,
  height = 768,
  stride = width * sizeof(uint32_t),
  shm_pool_size = height * stride * 2,
};

struct state
{
  int error;
  /* shm setup */
  int shm_fd;
  uint8_t *shm_data;
  /* wl globals */
  struct wl_compositor *compositor;
  size_t n_outputs;
  struct wl_output *outputs[3]; /* TODO: sensible dynamic allocation */
  struct wl_shm *shm;
  struct ext_session_lock_manager_v1 *session_lock_manager;
  /* wl_compositor */
  struct wl_surface *surface;
  /* wl_shm */
  struct wl_shm_pool *shm_pool;
  struct wl_buffer *buffer;
};

static void
init_state(struct state *state)
{
  /*
   * assume it has been zero-initialized already, so number fields are 0 and
   * pointer fields are NULL. only fields that should be initialized another way
   * are changed here.
   */
  state->shm_fd = -1;
  state->shm_data = MAP_FAILED;
}

static void
cleanup_state(struct state *state)
{
  int error = 0;

  /* wl_shm */
  if (state->buffer != NULL) {
    wl_buffer_destroy(state->buffer);
    state->buffer = NULL;
  }

  if (state->shm_pool != NULL) {
    wl_shm_pool_destroy(state->shm_pool);
    state->shm_pool = NULL;
  }

  /* wl_compositor */
  if (state->surface != NULL) {
    wl_surface_destroy(state->surface);
    state->surface = NULL;
  }

  /* wl globals */
  if (state->session_lock_manager != NULL) {
    ext_session_lock_manager_v1_destroy(state->session_lock_manager);
    state->session_lock_manager = NULL;
  }

  if (state->shm != NULL) {
    wl_shm_release(state->shm);
    state->shm = NULL;
  }

  for (size_t i = 0; i < state->n_outputs; i++) {
    wl_output_release(state->outputs[i]);
    state->outputs[i] = NULL;
  }
  state->n_outputs = 0;

  if (state->compositor != NULL) {
    wl_compositor_destroy(state->compositor);
    state->compositor = NULL;
  }

  /* shm setup */
  if (state->shm_data != MAP_FAILED) {
    error = munmap(state->shm_data, shm_pool_size);
    if (error != 0) {
      perror("munmap");
    }
    state->shm_data = MAP_FAILED;
  }

  if (state->shm_fd >= 0) {
    error = close(state->shm_fd);
    if (error != 0) {
      perror("close");
    }
    state->shm_fd = -1;
  }
}

static pid_t
launch_screensaver(xcb_window_t window, const char *screensaver_path)
{
  int error = 0;

  /* lazy, ideally i'd make a copy of environ and work on that */
  /* * 2 for nybbles (halves of bytes), + 3 for "0x" and NUL terminator */
  char window_id_string[sizeof window * 2 + 3] = { 0 };
  snprintf(window_id_string, COUNTOF(window_id_string), "%#" PRIx32, window);
  setenv("XSCREENSAVER_WINDOW", window_id_string, 1);

  /*
   * wl, x11 and shm_fd are cloexec, no need to close explicitly.
   *
   * argv is specified to not be modified by posix_spawn (described in the
   * manual for the exec family of functions, explained under Rationale) so the
   * const-discarding cast is safe in theory.
   */
  pid_t screensaver_pid = 0;
  const char *const screensaver_argv[] = { screensaver_path, "--root", NULL };
  error = posix_spawn(
      /*          pid */ &screensaver_pid,
      /*         path */ screensaver_path,
      /* file_actions */ NULL,
      /*        attrp */ NULL,
      /*         argv */ (char *const *)screensaver_argv,
      /*         envp */ environ);
  if (error != 0) {
    perror("posix_spawn");
    return -1;
  }
  fprintf(stderr, "screensaver_pid: %ld\n", (long)screensaver_pid);

  return screensaver_pid;
}

static void
cleanup_screensaver(pid_t *screensaver_pid)
{
  int error = 0;

  if (*screensaver_pid <= 0) {
    return;
  }

  error = kill(*screensaver_pid, SIGTERM);
  /* zombie processes count as existing, no need to exempt ESRCH */
  if (error != 0) {
    perror("kill");
    return;
  }

  siginfo_t screensaver_info = { 0 };
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

static int
setup_shm(struct state *state)
{
  int error = 0;

  state->shm_fd =
      shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (state->shm_fd < 0) {
    perror("shm_open");
    return -1;
  }

  error = shm_unlink(shm_name);
  if (error != 0) {
    perror("shm_unlink");
    /*
     * not fatal, but may cause problems with O_CREAT | O_EXCL in shm_open next
     * time we run. NOTE: "fixing" it by removing O_EXCL opens up a race
     * condition if multiple instances of this program are started
     * simultaneously.
     */
  }

  error = ftruncate(state->shm_fd, shm_pool_size);
  if (error != 0) {
    perror("ftruncate");
    return -1;
  }

  state->shm_data = mmap(
      /*   addr */ NULL,
      /* length */ shm_pool_size,
      /*   prot */ PROT_READ | PROT_WRITE,
      /*  flags */ MAP_SHARED,
      /*     fd */ state->shm_fd,
      /* offset */ 0);
  if (state->shm_data == MAP_FAILED) {
    perror("mmap");
    return -1;
  }

  return 0;
}

static int
flush_wl(struct wl_display *wl)
{
  int error = 0;
  struct pollfd flush_poll[1] = {
    { .fd = wl_display_get_fd(wl), .events = POLLOUT },
  };

  do {
    error = poll(flush_poll, COUNTOF(flush_poll), -1);
    if (error <= 0) {
      perror("poll");
      break;
    }
    /* doesn't block, instead errors with EAGAIN */
    /* TODO: how does it behave after a partial flush? is it possible? */
    error = wl_display_flush(wl);
    if (error < 0 && errno != EAGAIN && errno != EPIPE) {
      perror("wl_display_flush");
      break;
    }
  } while (error < 0 && errno == EAGAIN);

  /* if the connection was closed, continue and try to read the error later */
  if (error < 0 && errno != EPIPE) {
    return -1;
  }
  fprintf(stderr, "wl_display_flush: %d\n", error);

  return 0;
}

static int
read_wl_events(struct wl_display *wl)
{
  int error = 0;

  error = wl_display_prepare_read(wl);
  if (error != 0) {
    fputs("wl_display_prepare_read: Pending queue\n", stderr);
    /* unexpectedly pending queue is not fatal */
    return 0;
  }

  error = wl_display_read_events(wl);
  if (error != 0) {
    perror("wl_display_read_events");
    return -1;
  }

  return 0;
}

static void
handle_wl_shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
  (void)data;
  (void)wl_shm;

  char fourcc[4] = { 0 };
  switch (format) {
    case WL_SHM_FORMAT_ARGB8888: {
      memcpy(fourcc, "AR24", 4);
      break;
    }
    case WL_SHM_FORMAT_XRGB8888: {
      memcpy(fourcc, "XR24", 4);
      break;
    }
    default: {
      fourcc[0] = format;
      fourcc[1] = format >> 8;
      fourcc[2] = format >> 16;
      fourcc[3] = format >> 24;
      break;
    }
  }

  fprintf(
      stderr,
      "Wayland shm_format\n"
      "  format: %#" PRIx32 "\n"
      "  fourCC: %.4s\n",
      format,
      fourcc);
}

static const struct wl_shm_listener shm_listener = {
  .format = handle_wl_shm_format,
};

static void
handle_wl_registry_global(
    void *data,
    struct wl_registry *wl_registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
  struct state *state = data;
  int error = 0;
  (void)version;

  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    state->compositor =
        wl_registry_bind(wl_registry, name, &wl_compositor_interface, 1);
    if (state->compositor == NULL) {
      perror(interface);
      state->error = -1;
      return;
    }

    state->surface = wl_compositor_create_surface(state->compositor);
    if (state->compositor == NULL) {
      perror("wl_compositor_create_surface");
      state->error = -1;
      return;
    }

    return;
  } /* wl_compositor_interface */

  if (strcmp(interface, wl_output_interface.name) == 0) {
    size_t n = state->n_outputs;
    if (n >= 3) {
      return;
    }

    state->outputs[n] =
        wl_registry_bind(wl_registry, name, &wl_output_interface, 3);
    if (state->outputs[n] == NULL) {
      perror(interface);
      state->error = -1;
      return;
    }

    state->n_outputs = n + 1;

    return;
  } /* wl_output_interface */

  if (strcmp(interface, wl_shm_interface.name) == 0) {
    state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 2);
    if (state->shm == NULL) {
      perror(interface);
      state->error = -1;
      return;
    }

    error = wl_shm_add_listener(state->shm, &shm_listener, state);
    if (error != 0) {
      fputs("wl_shm_add_listener: listener already set\n", stderr);
      state->error = -1;
      return;
    }

    state->shm_pool =
        wl_shm_create_pool(state->shm, state->shm_fd, shm_pool_size);
    if (state->shm_pool == NULL) {
      perror("wl_shm_create_pool");
      state->error = -1;
      return;
    }

    state->buffer = wl_shm_pool_create_buffer(
        /* wl_shm_pool */ state->shm_pool,
        /*      offset */ 0,
        /*       width */ width,
        /*      height */ height,
        /*      stride */ stride,
        /*      format */ WL_SHM_FORMAT_XRGB8888);
    if (state->buffer == NULL) {
      perror("wl_shm_pool_create_buffer");
      state->error = -1;
      return;
    }

    return;
  } /* wl_shm_interface */

  if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
    state->session_lock_manager = wl_registry_bind(
        wl_registry,
        name,
        &ext_session_lock_manager_v1_interface,
        1);
    if (state->session_lock_manager == NULL) {
      perror(interface);
      state->error = -1;
      return;
    }

    return;
  } /* ext_session_lock_manager_v1_interface */
}

static void
handle_wl_registry_global_remove(
    void *data,
    struct wl_registry *wl_registry,
    uint32_t name)
{
  (void)data;
  (void)wl_registry;

  fprintf(
      stderr,
      "Wayland global_remove\n"
      "  name: %" PRIu32 "\n",
      name);
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_wl_registry_global,
  .global_remove = handle_wl_registry_global_remove,
};

static void
cleanup_x11_event(xcb_generic_event_t **event)
{
  if (*event != NULL) {
    free(*event);
    *event = NULL;
  }
}

static int
handle_x11_event(xcb_connection_t *x11)
{
  CLEANUP(x11_event) xcb_generic_event_t *event = NULL;
  event = xcb_poll_for_event(x11);
  if (event == NULL) {
    fputs("xcb_poll_for_event: No events\n", stderr);
    return 0;
  }

  uint8_t event_type = XCB_EVENT_RESPONSE_TYPE(event);
  fprintf(
      stderr,
      "X Event: %" PRIu8 " (%s)\n",
      event_type,
      xcb_event_get_label(event_type));

  switch (event_type) {
    case 0: /* X_Error */ {
      /* ideally i could just use XmuPrintDefaultErrorMessage, but that wants an
       * Xlib Display while i only have an xcb_connection_t */
      xcb_generic_error_t *event_error = (xcb_generic_error_t *)event;
      fprintf(
          stderr,
          "  Error code:    %" PRIu8 " (%s)\n"
          "  Major opcode:  %" PRIu8 " (%s)\n"
          "  Resource ID:   %#" PRIx32 "\n"
          /* Xlib also shows the "current" serial, but xcb doesn't seem to
           * expose this for us at all */
          "  Serial number: %" PRIu16 "\n",
          event_error->error_code,
          xcb_event_get_error_label(event_error->error_code),
          event_error->major_code,
          xcb_event_get_request_label(event_error->major_code),
          event_error->resource_id,
          event_error->sequence);
      /*
       * break the event loop on any X_Error. Xlib makes an exception for
       * error_code 17 BadImplementation (server does not implement operation)
       * but i don't care.
       */
      return -1;
    }

    default: {
      fprintf(stderr, "  Serial number: %" PRIu16 "\n", event->sequence);
      break;
    }
  } /* switch (event_type) */

  return 1;
}

static void
cleanup_wl_display(struct wl_display **wl)
{
  if (*wl != NULL) {
    flush_wl(*wl);

    wl_display_disconnect(*wl);
    *wl = NULL;
  }
}

static void
cleanup_wl_registry(struct wl_registry **wl_registry)
{
  if (*wl_registry != NULL) {
    wl_registry_destroy(*wl_registry);
    *wl_registry = NULL;
  }
}

static void
cleanup_x11_connection(xcb_connection_t **x11)
{
  int error = 0;

  if (*x11 != NULL) {
    error = xcb_flush(*x11);
    fprintf(stderr, "xcb_flush: %d\n", error);

    xcb_disconnect(*x11);
    *x11 = NULL;
  }
}

int
main(int argc, char **argv)
{
  int error = 0;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return EXIT_FAILURE;
  }
  const char *screensaver_path = argv[1];

  CLEANUP(wl_display) struct wl_display *wl = NULL;
  wl = wl_display_connect(NULL);
  if (wl == NULL) {
    perror("wl_display_connect");
    return EXIT_FAILURE;
  }

  CLEANUP(wl_registry) struct wl_registry *wl_registry = NULL;
  wl_registry = wl_display_get_registry(wl);
  if (wl_registry == NULL) {
    perror("wl_display_get_registry");
    return EXIT_FAILURE;
  }

  CLEANUP(state) struct state state = { 0 };
  init_state(&state);
  error = wl_registry_add_listener(wl_registry, &registry_listener, &state);
  if (error != 0) {
    fputs("wl_registry_add_listener: listener already set\n", stderr);
    return EXIT_FAILURE;
  }

  error = flush_wl(wl);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  CLEANUP(x11_connection) xcb_connection_t *x11 = NULL;
  int screen_preferred_n = 0;
  x11 = xcb_connect(NULL, &screen_preferred_n);
  error = xcb_connection_has_error(x11);
  if (error != 0) {
    fprintf(stderr, "xcb_connection_has_error: %d\n", error);
    return EXIT_FAILURE;
  }

  xcb_screen_t *screen_preferred = xcb_aux_get_screen(x11, screen_preferred_n);
  if (screen_preferred == NULL) {
    fputs("xcb_aux_get_screen\n", stderr);
    return EXIT_FAILURE;
  }

  xcb_window_t window = xcb_generate_id(x11);
  fprintf(stderr, "xcb_generate_id: %#" PRIx32 "\n", window);
  if (window == (xcb_window_t)-1) {
    return EXIT_FAILURE;
  }

  /* these requests error asynchronously, and are handled in the event loop */
  xcb_create_window(
      /*            c */ x11,
      /*        depth */ XCB_COPY_FROM_PARENT,
      /*          wid */ window,
      /*       parent */ screen_preferred->root,
      /*            x */ 0,
      /*            y */ 0,
      /*        width */ screen_preferred->width_in_pixels,
      /*       height */ screen_preferred->height_in_pixels,
      /* border_width */ 0,
      /*       _class */ XCB_WINDOW_CLASS_INPUT_OUTPUT,
      /*       visual */ screen_preferred->root_visual,
      /*   value_mask */ 0,
      /*   value_list */ NULL);
  xcb_map_window(x11, window);

  error = xcb_flush(x11);
  fprintf(stderr, "xcb_flush: %d\n", error);
  if (error <= 0) {
    return EXIT_FAILURE;
  }
  /* unsure what positive error values mean, besides success */
  /* i suspect that the only success value is 1 */

  CLEANUP(screensaver) pid_t screensaver_pid = 0;
  screensaver_pid = launch_screensaver(window, screensaver_path);
  if (screensaver_pid <= 0) {
    return EXIT_FAILURE;
  }

  /* set other things up while requests are in flight */
  error = setup_shm(&state);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  /*
   * wl_display_dispatch and xcb_wait_for_event can't timeout (and since we're
   * looping over two event domains we can't use blocking calls anyway), use
   * poll instead. make sure to handle all pending events before polling the
   * connection, otherwise we might leave events stuck in a queue for a while.
   */
  bool got_x11_error = false;
  int poll_ready = 1;
  struct pollfd connection_poll[2] = {
    { .fd = wl_display_get_fd(wl), .events = POLLIN },
    { .fd = xcb_get_file_descriptor(x11), .events = POLLIN },
  };
  while (poll_ready > 0) {
    /* xcb_poll_for_event processes one event at a time, handle it first so we
     * can use continue to loop it quickly */
    error = handle_x11_event(x11);
    if (error < 0) {
      /* keep reading error events */
      got_x11_error = true;
      continue;
    }
    if (error > 0) {
      continue;
    }
    if (got_x11_error) {
      error = -1;
      break;
    }

    /* flush requests that an event handler might have made */
    error = xcb_flush(x11);
    fprintf(stderr, "xcb_flush: %d\n", error);
    /* ignore flush errors for now, we check connection errors further down */

    /* xcb_poll_for_event also checks the connection for new events, but
     * wl_display_dispatch_pending doesn't, so we need to read for it first */
    error = read_wl_events(wl);
    if (error != 0) {
      break;
    }
    /* however, it dispatches all pending events in one go */
    error = wl_display_dispatch_pending(wl);
    if (error < 0) {
      perror("wl_display_dispatch_pending");
      break;
    }
    fprintf(stderr, "wl_display_dispatch_pending: %d\n", error);

    error = flush_wl(wl);

    /* avoid getting stuck waiting for failed callbacks */
    error = state.error;
    if (error != 0) {
      break;
    }

    /* check for errors *after* trying to handle events, because errors are only
     * noticed after reading events */
    error = wl_display_get_error(wl);
    if (error != 0) {
      fprintf(stderr, "wl_display_get_error: %s\n", strerror(error));
      break;
    }

    error = xcb_connection_has_error(x11);
    if (error == XCB_CONN_ERROR) {
      /* server closed the connection, perhaps the user closed the window */
      fputs("xcb_connection_has_error: Connection closed\n", stderr);
      error = 0;
      break;
    }
    if (error != 0) {
      fprintf(stderr, "xcb_connection_has_error: %d\n", error);
      break;
    }

    poll_ready = poll(connection_poll, COUNTOF(connection_poll), 5000);
    if (poll_ready < 0) {
      perror("poll");
      break;
    }
    fprintf(stderr, "poll: %d\n", poll_ready);
  } /* while (poll_ready > 0) */

  if (error != 0 || poll_ready < 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
