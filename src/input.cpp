#include <cstdint>
#include <keyboard.hpp>
#include <pointer.hpp>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include "server.hpp"

extern "C" {
#include "wlr/types/wlr_cursor.h"
#include "wlr/types/wlr_seat.h"
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
}

void server_new_input(struct wl_listener* /*listener*/, void* data) {
  /* This event is raised by the backend when a new input device becomes
   * available. */
  struct wlr_input_device* device = static_cast<wlr_input_device*>(data);
  switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      server_new_keyboard(&g_Server, device);
      break;
    case WLR_INPUT_DEVICE_POINTER: server_new_pointer(&g_Server, device); break;
    default: break;
  }
  /* We need to let the wlr_seat know what our capabilities are, which is
   * communiciated to the client. In TinyWL we always have a cursor, even if
   * there are no pointer devices, so we always include that capability. */
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&g_Server.keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(g_Server.seat, caps);
}

void seat_request_cursor(struct wl_listener* /*listener*/, void* data) {
  /* This event is raised by the seat when a client provides a cursor image */
  struct wlr_seat_pointer_request_set_cursor_event* event =
      static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
  struct wlr_seat_client* focused_client =
      g_Server.seat->pointer_state.focused_client;
  /* This can be sent by any client, so we check to make sure this one is
   * actually has pointer focus first. */
  if (focused_client == event->seat_client) {
    /* Once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. It will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs. */
    wlr_cursor_set_surface(
        g_Server.cursor, event->surface, event->hotspot_x, event->hotspot_y);
  }
}

void seat_request_set_selection(struct wl_listener* /*listener*/, void* data) {
  /* This event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in gfwl we always honor
   */
  struct wlr_seat_request_set_selection_event* event =
      static_cast<wlr_seat_request_set_selection_event*>(data);
  wlr_seat_set_selection(g_Server.seat, event->source, event->serial);
}
