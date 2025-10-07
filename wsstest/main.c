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
#include <wayland-client-protocols/ext-session-lock-v1.h>
#include <wayland-client-protocols/xdg-shell.h>
#include <xcb/xcb.h>
#include <xcb/xcb_util.h>
#define XCB_ERROR 0
#define XCB_REPLY 1

#define CLEANUP(how) __attribute__((cleanup(cleanup_##how)))
#define COUNTOF(array) (sizeof(array) / sizeof(array)[0])

static const char app_id[] = "wsstest";
static const char instance_class[] = "wsstest\0Wsstest";
static const char shm_name[] = "/wsstest_shm";

/* TODO: find these values dynamically for each output (search: TODO-SHM) */
/* TODO: more than 2 buffers? (search: TODO-BUFFER) */
enum {
  width = 1024,
  height = 768,
  stride = sizeof(uint32_t) * width,
  buffer_size = stride * height,
  shm_pool_size = buffer_size * 2,
};

struct names
{
  uint32_t compositor;
  /* TODO: sensible dynamic allocation (search: TODO-OUTPUT) */
  uint32_t outputs[3];
  size_t outputs_num;
  uint32_t shm;
  uint32_t wm_base;
  uint32_t session_lock_manager;
};

enum {
  compositor_version = 4, /* latest: 6 */
  output_version = 3,     /* latest: 4 */
  shm_version = 2,
  wm_base_version = 1, /* latest: 7 */
  session_lock_manager_version = 1,
};

struct messages
{
  uint32_t ping;
  uint32_t configure;
  uint32_t frame_time;
};

struct outputs
{
  size_t num;
  struct wl_output *outputs[3]; /* TODO-OUTPUT */
};

struct shm_region
{
  void *addr;
  size_t len;
};

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
  /* fprintf(stderr, "wl_display_flush: %d\n", error); */

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
  case WL_SHM_FORMAT_ARGB8888:
    memcpy(fourcc, "AR24", 4);
    break;
  case WL_SHM_FORMAT_XRGB8888:
    memcpy(fourcc, "XR24", 4);
    break;
  default:
    fourcc[0] = format;
    fourcc[1] = format >> 8;
    fourcc[2] = format >> 16;
    fourcc[3] = format >> 24;
    break;
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
  struct names *names = data;
  (void)wl_registry;
  (void)version;

  if (names == NULL) {
    fputs("handle_wl_registry_global: Missing names\n", stderr);
    return;
  }

  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    names->compositor = name;
    return;
  }

  if (strcmp(interface, wl_output_interface.name) == 0) {
    size_t n = names->outputs_num;
    /* TODO-OUTPUT */
    if (n >= 3) {
      return;
    }
    names->outputs[n] = name;
    names->outputs_num = n + 1;
    return;
  }

  if (strcmp(interface, wl_shm_interface.name) == 0) {
    names->shm = name;
    return;
  }

  if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    names->wm_base = name;
    return;
  }

  if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
    names->session_lock_manager = name;
    return;
  }
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
handle_xdg_wm_base_ping(
    void *data,
    struct xdg_wm_base *xdg_wm_base,
    uint32_t serial)
{
  struct messages *messages = data;
  (void)xdg_wm_base;

  if (messages == NULL) {
    fputs("handle_xdg_wm_base_ping: Missing messages\n", stderr);
    return;
  }

  messages->ping = serial;
}

static const struct xdg_wm_base_listener wm_base_listener = {
  .ping = handle_xdg_wm_base_ping,
};

static void
handle_xdg_surface_configure(
    void *data,
    struct xdg_surface *xdg_surface,
    uint32_t serial)
{
  struct messages *messages = data;
  (void)xdg_surface;

  if (messages == NULL) {
    fputs("handle_xdg_surface_configure: Missing messages\n", stderr);
    return;
  }

  messages->configure = serial;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  .configure = handle_xdg_surface_configure,
};

static void
handle_wl_callback_done(
    void *data,
    struct wl_callback *wl_callback,
    uint32_t callback_data)
{
  struct messages *messages = data;
  (void)wl_callback;

  if (messages == NULL) {
    fputs("handle_wl_callback_done: Missing messages\n", stderr);
    return;
  }

  messages->frame_time = callback_data;
}

static const struct wl_callback_listener frame_callback_listener = {
  .done = handle_wl_callback_done,
};

static int
bind_compositor(
    struct wl_registry *registry,
    uint32_t name,
    struct wl_compositor **compositor,
    struct wl_surface **surface)
{
  *compositor = wl_registry_bind(
      registry,
      name,
      &wl_compositor_interface,
      compositor_version);
  if (*compositor == NULL) {
    perror(wl_compositor_interface.name);
    return -1;
  }

  *surface = wl_compositor_create_surface(*compositor);
  if (*surface == NULL) {
    perror("wl_compositor_create_surface");
    return -1;
  }

  return 0;
}

static int
bind_outputs(
    struct wl_registry *registry,
    size_t outputs_num,
    uint32_t *names,
    struct outputs *outputs)
{
  /* TODO-OUTPUT */
  for (size_t i = outputs->num; i < outputs_num && i < 3; i++) {
    outputs->outputs[i] = wl_registry_bind(
        registry,
        names[i],
        &wl_output_interface,
        output_version);
    if (outputs->outputs[i] == NULL) {
      perror(wl_output_interface.name);
      return -1;
    }

    outputs->num = i + 1;
  }

  return 0;
}

/* TODO-SHM TODO-BUFFER */
static int
bind_shm(
    struct wl_registry *registry,
    uint32_t name,
    int shm_fd,
    struct wl_shm **shm,
    struct wl_shm_pool **shm_pool,
    struct wl_buffer *(*buffers)[2])
{
  int error = 0;

  *shm = wl_registry_bind(registry, name, &wl_shm_interface, shm_version);
  if (*shm == NULL) {
    perror(wl_shm_interface.name);
    return -1;
  }

  error = wl_shm_add_listener(*shm, &shm_listener, NULL);
  if (error != 0) {
    fputs("wl_shm_add_listener: listener already set\n", stderr);
    return -1;
  }

  *shm_pool = wl_shm_create_pool(*shm, shm_fd, shm_pool_size);
  if (*shm_pool == NULL) {
    perror("wl_shm_create_pool");
    return -1;
  }

  (*buffers)[0] = wl_shm_pool_create_buffer(
      /* wl_shm_pool */ *shm_pool,
      /*      offset */ buffer_size * 0,
      /*       width */ width,
      /*      height */ height,
      /*      stride */ stride,
      /*      format */ WL_SHM_FORMAT_XRGB8888);
  if ((*buffers)[0] == NULL) {
    perror("wl_shm_pool_create_buffer");
    return -1;
  }

  (*buffers)[1] = wl_shm_pool_create_buffer(
      /* wl_shm_pool */ *shm_pool,
      /*      offset */ buffer_size * 1,
      /*       width */ width,
      /*      height */ height,
      /*      stride */ stride,
      /*      format */ WL_SHM_FORMAT_XRGB8888);
  if ((*buffers)[1] == NULL) {
    perror("wl_shm_pool_create_buffer");
    return -1;
  }

  return 0;
}

static int
bind_wm_base(
    struct wl_registry *registry,
    uint32_t name,
    struct messages *messages,
    struct wl_surface *surface,
    struct xdg_wm_base **wm_base,
    struct xdg_surface **xdg_surface,
    struct xdg_toplevel **toplevel)
{
  int error = 0;

  *wm_base =
      wl_registry_bind(registry, name, &xdg_wm_base_interface, wm_base_version);
  if (*wm_base == NULL) {
    perror(xdg_wm_base_interface.name);
    return -1;
  }

  error = xdg_wm_base_add_listener(*wm_base, &wm_base_listener, messages);
  if (error != 0) {
    fputs("xdg_wm_base_add_listener: listener already set\n", stderr);
    return -1;
  }

  *xdg_surface = xdg_wm_base_get_xdg_surface(*wm_base, surface);
  if (*xdg_surface == NULL) {
    perror("xdg_wm_base_get_xdg_surface");
    return -1;
  }

  error =
      xdg_surface_add_listener(*xdg_surface, &xdg_surface_listener, messages);
  if (error != 0) {
    fputs("xdg_surface_add_listener: listener already set\n", stderr);
    return -1;
  }

  *toplevel = xdg_surface_get_toplevel(*xdg_surface);
  if (*toplevel == NULL) {
    perror("xdg_surface_get_toplevel");
    return -1;
  }

  xdg_toplevel_set_app_id(*toplevel, app_id);

  /* commit the unattached surface to prompt the server to configure it */
  wl_surface_commit(surface);

  return 0;
}

static int
bind_session_lock_manager(
    struct wl_registry *registry,
    uint32_t name,
    struct ext_session_lock_manager_v1 **session_lock_manager)
{
  *session_lock_manager = wl_registry_bind(
      registry,
      name,
      &ext_session_lock_manager_v1_interface,
      session_lock_manager_version);
  if (*session_lock_manager == NULL) {
    perror(ext_session_lock_manager_v1_interface.name);
    return -1;
  }

  return 0;
}

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
    /* fputs("xcb_poll_for_event: No events\n", stderr); */
    return 0;
  }

  uint8_t event_type = XCB_EVENT_RESPONSE_TYPE(event);
  fprintf(
      stderr,
      "X Event: %" PRIu8 " (%s)\n",
      event_type,
      xcb_event_get_label(event_type));

  xcb_generic_error_t *event_error = NULL;
  switch (event_type) {
  case XCB_ERROR:
    /* ideally i could just use XmuPrintDefaultErrorMessage, but that wants an
     * Xlib Display while i only have an xcb_connection_t */
    event_error = (xcb_generic_error_t *)event;
    fprintf(
        stderr,
        "  Error code:    %" PRIu8 " (%s)\n"
        "  Major opcode:  %" PRIu8 " (%s)\n"
        "  Resource ID:   %#" PRIx32 "\n"
        /* Xlib also shows the "current" serial, but xcb doesn't seem to expose
         * this for us at all */
        "  Serial number: %" PRIu16 "\n",
        event_error->error_code,
        xcb_event_get_error_label(event_error->error_code),
        event_error->major_code,
        xcb_event_get_request_label(event_error->major_code),
        event_error->resource_id,
        event_error->sequence);

    /*
     * break the event loop on any XCB_ERROR. Xlib makes an exception for
     * error_code 17 BadImplementation (server does not implement operation) but
     * i don't care.
     *
     * if the error is a result of the initial GetImage request, carry on. this
     * is a workaround!
     * TODO: figure out what's wrong with it (search: TODO-GETIMAGE)
     */
    if (event_error->major_code == XCB_GET_IMAGE &&
        event_error->sequence == 4) {
      break;
    }
    return -1;

  default:
    fprintf(stderr, "  Serial number: %" PRIu16 "\n", event->sequence);
    break;
  } /* switch (event_type) */

  return 1;
}

static void
cleanup_x11_get_image_reply(xcb_get_image_reply_t **get_image_reply)
{
  if (*get_image_reply != NULL) {
    free(*get_image_reply);
    *get_image_reply = NULL;
  }
}

/* TODO-BUFFER */
static int
update_surface(
    xcb_connection_t *x11,
    struct xcb_get_image_cookie_t *get_image_cookie,
    xcb_window_t window,
    struct messages *messages,
    struct wl_surface *surface,
    struct wl_callback **frame_callback,
    struct wl_buffer *(*buffers)[2],
    uint8_t *buffers_mem,
    size_t buffer_len,
    int *arg_next_buffer)
{
  int error = 0;
  int next_buffer = *arg_next_buffer;
  struct wl_buffer *buffer = (*buffers)[next_buffer];
  uint8_t *buffer_mem = &buffers_mem[buffer_len * next_buffer];

  CLEANUP(x11_get_image_reply) xcb_get_image_reply_t *get_image_reply = NULL;
  get_image_reply = xcb_get_image_reply(x11, *get_image_cookie, NULL);

  /* TODO-GETIMAGE */
  if (get_image_reply != NULL) {
    uint8_t *get_image_data = xcb_get_image_data(get_image_reply);
    /* xcb_*_length returns int, assuming it's non-negative */
    size_t get_image_data_length = xcb_get_image_data_length(get_image_reply);
    if (get_image_data_length > buffer_len) {
      get_image_data_length = buffer_len;
    }

    memcpy(buffer_mem, get_image_data, get_image_data_length);
  }

  /* need to attach the initial buffer to map the window, no matter what */
  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);

  next_buffer++;
  if (next_buffer > 1) {
    next_buffer = 0;
  }

  /* request next image right after copying the current one. this causes the
   * output to lag against the input by about 1 update, but we wait less */
  *get_image_cookie = xcb_get_image_unchecked(
      /*          c */ x11,
      /*     format */ XCB_IMAGE_FORMAT_Z_PIXMAP,
      /*   drawable */ window,
      /*          x */ 0,
      /*          y */ 0,
      /*      width */ width,  /*screen_preferred->width_in_pixels,*/
      /*     height */ height, /*screen_preferred->height_in_pixels,*/
      /* plane_mask */ 0xFFFFFFFF);

  /* request next frame. the reply to the above request should arrive by then */
  *frame_callback = wl_surface_frame(surface);
  if (*frame_callback == NULL) {
    perror("wl_surface_frame");
    return -1;
  }

  error = wl_callback_add_listener(
      *frame_callback,
      &frame_callback_listener,
      messages);
  if (error != 0) {
    fputs("wl_callback_add_listener: listener already set\n", stderr);
    return -1;
  }

  /* all done, cap the update with a commit */
  wl_surface_commit(surface);

  *arg_next_buffer = next_buffer;
  return 0;
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
cleanup_wl_registry(struct wl_registry **registry)
{
  if (*registry != NULL) {
    wl_registry_destroy(*registry);
    *registry = NULL;
  }
}

static void
cleanup_wl_compositor(struct wl_compositor **compositor)
{
  if (*compositor != NULL) {
    wl_compositor_destroy(*compositor);
    *compositor = NULL;
  }
}

static void
cleanup_wl_surface(struct wl_surface **surface)
{
  if (*surface != NULL) {
    wl_surface_destroy(*surface);
    *surface = NULL;
  }
}

static void
cleanup_wl_callback(struct wl_callback **callback)
{
  if (*callback != NULL) {
    wl_callback_destroy(*callback);
    *callback = NULL;
  }
}

static void
cleanup_outputs(struct outputs *outputs)
{
  /* TODO-OUTPUT */
  for (size_t i = 0; i < outputs->num && i < 3; i++) {
    wl_output_release(outputs->outputs[i]);
    outputs->outputs[i] = NULL;
  }
  outputs->num = 0;
}

static void
cleanup_wl_shm(struct wl_shm **shm)
{
  if (*shm != NULL) {
    wl_shm_release(*shm);
    *shm = NULL;
  }
}

static void
cleanup_wl_shm_pool(struct wl_shm_pool **shm_pool)
{
  if (*shm_pool != NULL) {
    wl_shm_pool_destroy(*shm_pool);
    *shm_pool = NULL;
  }
}

/* TODO-BUFFER */
static void
cleanup_wl_buffer(struct wl_buffer *(*buffer)[2])
{
  if ((*buffer)[0] != NULL) {
    wl_buffer_destroy((*buffer)[0]);
    (*buffer)[0] = NULL;
  }

  if ((*buffer)[1] != NULL) {
    wl_buffer_destroy((*buffer)[1]);
    (*buffer)[1] = NULL;
  }
}

static void
cleanup_xdg_wm_base(struct xdg_wm_base **wm_base)
{
  if (*wm_base != NULL) {
    xdg_wm_base_destroy(*wm_base);
    *wm_base = NULL;
  }
}

static void
cleanup_xdg_surface(struct xdg_surface **xdg_surface)
{
  if (*xdg_surface != NULL) {
    xdg_surface_destroy(*xdg_surface);
    *xdg_surface = NULL;
  }
}

static void
cleanup_xdg_toplevel(struct xdg_toplevel **toplevel)
{
  if (*toplevel != NULL) {
    xdg_toplevel_destroy(*toplevel);
    *toplevel = NULL;
  }
}

static void
cleanup_ext_session_lock_manager(
    struct ext_session_lock_manager_v1 **session_lock_manager)
{
  if (*session_lock_manager != NULL) {
    ext_session_lock_manager_v1_destroy(*session_lock_manager);
    *session_lock_manager = NULL;
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

static void
cleanup_shm_fd(int *shm_fd)
{
  int error = 0;

  if (*shm_fd >= 0) {
    error = close(*shm_fd);
    if (error != 0) {
      perror("close");
    }
    *shm_fd = -1;
  }
}

static void
cleanup_shm_region(struct shm_region *shm_region)
{
  int error = 0;

  if (shm_region->addr != MAP_FAILED) {
    error = munmap(shm_region->addr, shm_region->len);
    if (error != 0) {
      perror("munmap");
    }
    shm_region->addr = MAP_FAILED;
    shm_region->len = 0;
  }
}

/*
 * TODO: we currently use x11 GetImage and wayland shm to pass frames around,
 * which makes lots of copies. we could use the x11 shm extension to avoid a
 * copy here in wsstest, or figure out how to use handles to gpu memory to
 * minimize copies altogether.
 */
int
main(int argc, char **argv)
{
  int error = 0;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return EXIT_FAILURE;
  }
  const char *screensaver_path = argv[1];

  /* === SET UP WAYLAND === */

  CLEANUP(wl_display) struct wl_display *wl = NULL;
  wl = wl_display_connect(NULL);
  if (wl == NULL) {
    perror("wl_display_connect");
    return EXIT_FAILURE;
  }

  CLEANUP(wl_registry) struct wl_registry *registry = NULL;
  registry = wl_display_get_registry(wl);
  if (registry == NULL) {
    perror("wl_display_get_registry");
    return EXIT_FAILURE;
  }

  struct names names = { 0 };
  error = wl_registry_add_listener(registry, &registry_listener, &names);
  if (error != 0) {
    fputs("wl_registry_add_listener: listener already set\n", stderr);
    return EXIT_FAILURE;
  }

  CLEANUP(wl_compositor) struct wl_compositor *compositor = NULL;
  CLEANUP(wl_surface) struct wl_surface *surface = NULL;
  CLEANUP(wl_callback) struct wl_callback *frame_callback = NULL;
  CLEANUP(outputs) struct outputs outputs = { 0 };
  CLEANUP(wl_shm) struct wl_shm *shm = NULL;
  CLEANUP(wl_shm_pool) struct wl_shm_pool *shm_pool = NULL;
  CLEANUP(wl_buffer) struct wl_buffer *buffers[2] = { NULL }; /* TODO-BUFFER */
  CLEANUP(xdg_wm_base) struct xdg_wm_base *wm_base = NULL;
  CLEANUP(xdg_surface) struct xdg_surface *xdg_surface = NULL;
  CLEANUP(xdg_toplevel) struct xdg_toplevel *toplevel = NULL;
  CLEANUP(ext_session_lock_manager)
  struct ext_session_lock_manager_v1 *session_lock_manager = NULL;

  error = flush_wl(wl);
  if (error != 0) {
    return EXIT_FAILURE;
  }

  /* === SET UP X11 === */

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

  /* TODO: intern_atom for UTF8_STRING or COMPOUND_TEXT (requires an extra round
   * trip) */
  xcb_change_property(
      /*        c */ x11,
      /*     mode */ XCB_PROP_MODE_REPLACE,
      /*   window */ window,
      /* property */ XCB_ATOM_WM_CLASS,
      /*     type */ XCB_ATOM_STRING, /* NB: this means latin-1 */
      /*   format */ 8,
      /* data_len */ COUNTOF(instance_class), /* include terminating nul byte */
      /*     data */ instance_class);

  xcb_map_window(x11, window);

  /*
   * replies to requests are events, but xcb doesn't let me handle them in the
   * event loop, so we hang onto the cookie to retrieve the reply later. this is
   * the initial request that will be continually issued in a loop; this call is
   * duplicated in update_surface().
   * TODO-GETIMAGE
   */
  xcb_get_image_cookie_t get_image_cookie = xcb_get_image_unchecked(
      /*          c */ x11,
      /*     format */ XCB_IMAGE_FORMAT_Z_PIXMAP,
      /*   drawable */ window,
      /*          x */ 0,
      /*          y */ 0,
      /*      width */ width,  /*screen_preferred->width_in_pixels,*/
      /*     height */ height, /*screen_preferred->height_in_pixels,*/
      /* plane_mask */ 0xFFFFFFFF);

  error = xcb_flush(x11);
  fprintf(stderr, "xcb_flush: %d\n", error);
  if (error <= 0) {
    return EXIT_FAILURE;
  }
  /* unsure what positive error values mean, besides success */
  /* i suspect that the only success value is 1 */

  /* === LAUNCH SCREENSAVER === */

  /* * 2 for nybbles (halves of bytes), + 3 for "0x" and NUL terminator */
  char window_id_string[sizeof window * 2 + 3] = { 0 };
  snprintf(window_id_string, COUNTOF(window_id_string), "%#" PRIx32, window);

  /* lazy, ideally i'd make a copy of environ and work on that */
  error = setenv("XSCREENSAVER_WINDOW", window_id_string, 1);
  if (error != 0) {
    perror("setenv");
    return EXIT_FAILURE;
  }

  /*
   * wl and x11 sockets are cloexec, no need to close explicitly.
   *
   * argv is specified to not be modified by posix_spawn (described in the
   * manual for the exec family of functions, explained under Rationale) so the
   * const-discarding cast is safe in theory.
   */
  CLEANUP(screensaver) pid_t screensaver_pid = 0;
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
    return EXIT_FAILURE;
  }
  fprintf(stderr, "screensaver_pid: %ld\n", (long)screensaver_pid);

  /* === SET UP SHARED MEMORY === */

  CLEANUP(shm_fd) int shm_fd = -1;
  shm_fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (shm_fd < 0) {
    perror("shm_open");
    return EXIT_FAILURE;
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

  /* TODO-SHM */
  error = ftruncate(shm_fd, shm_pool_size);
  if (error != 0) {
    perror("ftruncate");
    return EXIT_FAILURE;
  }

  CLEANUP(shm_region)
  struct shm_region shm_region = {
    .addr = MAP_FAILED,
    .len = 0,
  };
  shm_region.addr = mmap(
      /*   addr */ NULL,
      /* length */ shm_pool_size,
      /*   prot */ PROT_READ | PROT_WRITE,
      /*  flags */ MAP_SHARED,
      /*     fd */ shm_fd,
      /* offset */ 0);
  if (shm_region.addr == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }
  shm_region.len = shm_pool_size;

  /* === EVENT LOOP === */

  /*
   * wl_display_dispatch and xcb_wait_for_event can't timeout (and since we're
   * looping over two event domains we can't use blocking calls anyway), use
   * poll instead. make sure to handle all pending events before polling the
   * connection, otherwise we might leave events stuck in a queue for a while.
   */
  bool got_x11_error = false;
  struct messages messages = { 0 };
  int next_buffer = 0;
  int poll_ready = 1;
  struct pollfd connection_poll[2] = {
    { .fd = wl_display_get_fd(wl), .events = POLLIN },
    { .fd = xcb_get_file_descriptor(x11), .events = POLLIN },
  };
  while (poll_ready > 0) {
    /* === RECEIVE X11 EVENTS === */

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

    /* === RESPOND TO X11 EVENTS === */

    /* if we ever respond to x11 events, we send the responses here */

    /* === RECEIVE WAYLAND EVENTS === */

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
    /* fprintf(stderr, "wl_display_dispatch_pending: %d\n", error); */

    /* === RESPOND TO WAYLAND EVENTS === */

    error = 0;

    if (names.compositor != 0 && compositor == NULL) {
      error =
          bind_compositor(registry, names.compositor, &compositor, &surface);
    }
    if (error != 0) {
      break;
    }

    if (names.outputs_num > outputs.num) {
      error =
          bind_outputs(registry, names.outputs_num, names.outputs, &outputs);
    }
    if (error != 0) {
      break;
    }

    if (names.shm != 0 && shm == NULL) {
      error = bind_shm(registry, names.shm, shm_fd, &shm, &shm_pool, &buffers);
    }
    if (error != 0) {
      break;
    }

    if (names.wm_base != 0 && surface != NULL && wm_base == NULL) {
      error = bind_wm_base(
          registry,
          names.wm_base,
          &messages,
          surface,
          &wm_base,
          &xdg_surface,
          &toplevel);
    }
    if (error != 0) {
      break;
    }

    if (names.session_lock_manager != 0 && session_lock_manager == NULL) {
      error = bind_session_lock_manager(
          registry,
          names.session_lock_manager,
          &session_lock_manager);
    }
    if (error != 0) {
      break;
    }

    if (wm_base != NULL && messages.ping != 0) {
      xdg_wm_base_pong(wm_base, messages.ping);
      messages.ping = 0;
    }

    /* TODO-BUFFER */
    if (xdg_surface != NULL && messages.configure != 0 && surface != NULL &&
        buffers[0] != NULL && buffers[1] != NULL) {
      xdg_surface_ack_configure(xdg_surface, messages.configure);

      cleanup_wl_callback(&frame_callback);
      error = update_surface(
          x11,
          &get_image_cookie,
          window,
          &messages,
          surface,
          &frame_callback,
          &buffers,
          (uint8_t *)shm_region.addr,
          buffer_size,
          &next_buffer);

      messages.configure = 0;
    }
    if (error != 0) {
      break;
    }

    if (messages.frame_time != 0 && surface != NULL && buffers[0] != NULL &&
        buffers[1] != NULL) {
      cleanup_wl_callback(&frame_callback);
      error = update_surface(
          x11,
          &get_image_cookie,
          window,
          &messages,
          surface,
          &frame_callback,
          &buffers,
          (uint8_t *)shm_region.addr,
          buffer_size,
          &next_buffer);

      messages.frame_time = 0;
    }
    if (error != 0) {
      break;
    }

    /* === FLUSH RESPONSES === */

    /* ignore flush errors for now, we check connection errors further down */
    error = flush_wl(wl);

    error = xcb_flush(x11);
    /* fprintf(stderr, "xcb_flush: %d\n", error); */

    /* === HANDLE CONNECTION ERRORS === */

    /* check for errors *after* trying to handle events, because errors are only
     * noticed after reading events */
    error = wl_display_get_error(wl);
    if (error != 0) {
      errno = error;
      perror("wl_display_get_error");
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

    /* === WAIT FOR EVENTS === */

    poll_ready = poll(connection_poll, COUNTOF(connection_poll), 60000);
    if (poll_ready < 0) {
      perror("poll");
      break;
    }
    /* fprintf(stderr, "poll: %d\n", poll_ready); */
  } /* while (poll_ready > 0) */

  if (error != 0 || poll_ready < 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
