#include <cstdlib>
#include <includes.hpp>
#include <layer_shell.hpp>
#include <memory>
#include <scene.hpp>
#include <server.hpp>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "output.hpp"
#include "wlr/types/wlr_compositor.h"
#include "wlr/types/wlr_keyboard.h"
#include "wlr/types/wlr_output.h"
#include "wlr/util/box.h"

void focus_layer_surface(struct GfLayerSurface* gfwl_layer_surface) {
  struct wlr_seat*     seat        = gfwl_layer_surface->server->seat;
  struct wlr_keyboard* keyboard    = wlr_seat_get_keyboard(seat);
  gfwl_layer_surface->prev_focused = seat->keyboard_state.focused_surface;

  wlr_seat_keyboard_notify_enter(seat,
                                 gfwl_layer_surface->wlr_layer_surface->surface,
                                 keyboard->keycodes,
                                 keyboard->num_keycodes,
                                 &keyboard->modifiers);
}

void unfocus_layer_surface(struct GfLayerSurface* gfwl_layer_surface) {
  struct wlr_seat*     seat     = gfwl_layer_surface->server->seat;
  struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);

  wlr_seat_keyboard_notify_enter(seat,
                                 gfwl_layer_surface->prev_focused,
                                 keyboard->keycodes,
                                 keyboard->num_keycodes,
                                 &keyboard->modifiers);
}

void configure_anchored_layer_surface(GfLayerSurface* layer_surface) {
  auto*   output = layer_surface->scene->layer_surface->output;

  wlr_box usable_area = {0, 0, 0, 0};
  wlr_output_effective_resolution(
      output, &usable_area.width, &usable_area.height);
  const struct wlr_box full_area = usable_area;

  wlr_scene_layer_surface_v1_configure(
      layer_surface->scene, &full_area, &usable_area);
  layer_surface->output->set_usable_space(usable_area);
}

void handle_layer_surface_map(struct wl_listener*    listener,
                              [[maybe_unused]] void* data) {
  struct GfLayerSurface* gfwl_layer_surface =
      wl_container_of(listener, gfwl_layer_surface, map);

  focus_layer_surface(gfwl_layer_surface);
  configure_anchored_layer_surface(gfwl_layer_surface);
}

void handle_layer_surface_unmap(struct wl_listener*    listener,
                                [[maybe_unused]] void* data) {
  struct GfLayerSurface* gfwl_layer_surface =
      wl_container_of(listener, gfwl_layer_surface, unmap);
  unfocus_layer_surface(gfwl_layer_surface);
}

void handle_layer_surface_commit(struct wl_listener*    listener,
                                 [[maybe_unused]] void* data) {
  wlr_log(WLR_INFO, "Commited Layer Surface Change");
  struct GfLayerSurface* gfwl_layer_surface =
      wl_container_of(listener, gfwl_layer_surface, commit);

  if (gfwl_layer_surface->wlr_layer_surface->initial_commit) {
    configure_anchored_layer_surface(gfwl_layer_surface);
  }
}

void handle_new_layer_shell_surface(struct wl_listener* /*listener*/,
                                    void* data) {

  struct wlr_layer_surface_v1* wlr_layer_surface =
      static_cast<wlr_layer_surface_v1*>(data);
  if (!wlr_layer_surface) {
    wlr_log(WLR_ERROR, "No layer surface.");
    return;
  }

  struct GfLayerSurface* gfwl_layer_surface =
      static_cast<struct GfLayerSurface*>(
          calloc(1, sizeof(*gfwl_layer_surface)));
  if (!gfwl_layer_surface) {
    wlr_log(WLR_ERROR, "No gfwl layer surface.");
    return;
  }
  gfwl_layer_surface->wlr_layer_surface = wlr_layer_surface;
  gfwl_layer_surface->server            = &g_Server;

  if (wlr_layer_surface->output) {
    for (const auto& output : g_Server.outputs) {
      if (output->wlr_output == wlr_layer_surface->output) {
        gfwl_layer_surface->output = output;
        break;
      }
    }
  } else {
    auto gfwl_output           = g_Server.focused_output;
    gfwl_layer_surface->output = gfwl_output;
    wlr_layer_surface->output  = gfwl_output->wlr_output;
  }

  // Create the scene.
  struct wlr_scene_layer_surface_v1* scene_surface =
      wlr_scene_layer_surface_v1_create(g_Server.scene.layer.top,
                                        wlr_layer_surface);
  // Add to layer_surface object.
  gfwl_layer_surface->scene = scene_surface;
  // Register commit handler.
  gfwl_layer_surface->commit.notify = handle_layer_surface_commit;
  wl_signal_add(&wlr_layer_surface->surface->events.commit,
                &gfwl_layer_surface->commit);

  // Register map handler.
  gfwl_layer_surface->map.notify = handle_layer_surface_map;
  wl_signal_add(&wlr_layer_surface->surface->events.map,
                &gfwl_layer_surface->map);

  // Register unmap handler.
  gfwl_layer_surface->unmap.notify = handle_layer_surface_unmap;
  wl_signal_add(&wlr_layer_surface->surface->events.unmap,
                &gfwl_layer_surface->unmap);
}
