#include <cstdlib>
#include <ctime>
#include <includes.hpp>
#include <memory>
#include <output.hpp>
#include <scene.hpp>
#include <server.hpp>
#include <tiling/focus.hpp>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>

#include "tiling/container/base.hpp"
#include "tiling/container/root.hpp"
#include "tiling/state.hpp"
#include "wlr/util/log.h"

void GfOutput::set_usable_space(wlr_box box) {
  this->usable_space = box;
}
wlr_box GfOutput::get_usable_space() {
  if (wlr_box_empty(&this->usable_space)) {
    wlr_box usable_area{0, 0, 0, 0};
    wlr_output_effective_resolution(
        this->wlr_output, &usable_area.width, &usable_area.height);
    this->set_usable_space(usable_area);
  }
  return this->usable_space;
};
// Gets the output that a container is in.
std::shared_ptr<GfOutput>
get_output_from_container(const std::shared_ptr<GfContainer>& container) {
  auto container_point = get_container_origin(container); // container->box;
  auto outputs         = g_Server.outputs;
  for (auto output : outputs) {
    wlr_box output_box = {
        .x      = output->scene_output->x,
        .y      = output->scene_output->y,
        .width  = output->wlr_output->width,
        .height = output->wlr_output->height,
    };
    if (output_box.x <= container_point.x &&
        output_box.y <= container_point.y &&
        output_box.x + output_box.width >= container_point.x &&
        output_box.y + output_box.height >= container_point.y) {
      return output;
    }
  }
  return nullptr;
}

// Focuses the output that the container is in.
void focus_output_from_container(
    const std::shared_ptr<GfContainer>& container) {
  auto output = get_output_from_container(container);
  if (output) {
    g_Server.focused_output = output;
  } else {
    wlr_log(WLR_ERROR, "Your container doesn't have an output ):<");
  }
}

static void output_frame(struct wl_listener*    listener,
                         [[maybe_unused]] void* data) {
  /* This function is called every time an output is ready to display a frame,
   * generally at the output's refresh rate (e.g. 60Hz). */
  struct GfOutput*         output = wl_container_of(listener, output, frame);
  struct wlr_scene*        scene  = g_Server.scene.root;

  struct wlr_scene_output* scene_output =
      wlr_scene_get_scene_output(scene, output->wlr_output);

  /* Render the scene if needed and commit the output */
  wlr_scene_output_commit(scene_output, nullptr);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener* listener, void* data) {
  /* This function is called when the backend requests a new state for
   * the output. For example, Wayland and X11 backends request a new mode
   * when the output window is resized. */
  struct GfOutput* output = wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state* event =
      static_cast<wlr_output_event_request_state*>(data);
  wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener*    listener,
                           [[maybe_unused]] void* data) {
  struct GfOutput* output = wl_container_of(listener, output, destroy);

  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->request_state.link);
  wl_list_remove(&output->destroy.link);
  wl_list_remove(&output->link);
  free(output);
}

void server_new_output(struct wl_listener* /*listener*/, void* data) {
  /* This event is raised by the backend when a new output (aka a display or
   * monitor) becomes available. */
  struct wlr_output* wlr_output = static_cast<struct wlr_output*>(data);

  /* Configures the output created by the backend to use our allocator
   * and our renderer. Must be done once, before commiting the output */
  wlr_output_init_render(wlr_output, g_Server.allocator, g_Server.renderer);

  /* The output may be disabled, switch it on. */
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);

  /* Some backends don't have modes. DRM+KMS does, and we need to set a mode
   * before we can use the output. The mode is a tuple of (width, height,
   * refresh rate), and each monitor supports only a specific set of modes. We
   * just pick the monitor's preferred mode, a more sophisticated compositor
   * would let the user configure it. */
  struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
  if (mode != nullptr) {
    wlr_output_state_set_mode(&state, mode);
  }

  /* Atomically applies the new output state. */
  wlr_output_commit_state(wlr_output, &state);
  wlr_output_state_finish(&state);

  /* Allocates and configures our state for this output */
  struct std::shared_ptr<GfOutput> output = std::make_shared<GfOutput>();
  output->wlr_output                      = wlr_output;

  /* Sets up a listener for the frame event. */
  output->frame.notify = output_frame;
  wl_signal_add(&wlr_output->events.frame, &output->frame);

  /* Sets up a listener for the state request event. */
  output->request_state.notify = output_request_state;
  wl_signal_add(&wlr_output->events.request_state, &output->request_state);

  /* Sets up a listener for the destroy event. */
  output->destroy.notify = output_destroy;
  wl_signal_add(&wlr_output->events.destroy, &output->destroy);

  g_Server.focused_output = output;
  g_Server.outputs.push_back(output);

  // TODO(gabe): Maybe move this to the constructor of tiling state.
  // (I actually think this may be worth doing inheritance for)
  output->tiling_state->root = std::make_shared<GfContainerRoot>(
      g_Server, GFWL_CONTAINER_ROOT, output->tiling_state->weak_from_this());

  output->tiling_state->split_dir = GFWL_SPLIT_DIR_HORI;
  output->tiling_state->output    = output;

  /* Adds this to the output layout. The add_auto function arranges outputs
   * from left-to-right in the order they appear. A more sophisticated
   * compositor would let the user configure the arrangement of outputs in the
   * layout.
   *
   * The output layout utility automatically adds a wl_output global to the
   * display, which Wayland clients can see to find out information about the
   * output (such as DPI, scale factor, manufacturer, etc).
   */
  output->output_layout_output =
      wlr_output_layout_add_auto(g_Server.output_layout, wlr_output);
  output->scene_output =
      wlr_scene_output_create(g_Server.scene.root, wlr_output);
  wlr_scene_output_layout_add_output(g_Server.scene_layout,
                                     output->output_layout_output,
                                     output->scene_output);
}
