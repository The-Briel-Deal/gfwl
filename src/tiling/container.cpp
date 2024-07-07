#include "container.hpp"
#include "state.hpp"
#include <includes.hpp>
#include <output.hpp>
#include <server.hpp>
#include <xdg_shell.hpp>

GfContainer::GfContainer(bool root, gfwl_tiling_state *state,
                         gfwl_container_type type,
                         std::shared_ptr<GfContainer> parent,
                         gfwl_server *server, gfwl_toplevel *toplevel) {
  this->is_root = root;
  this->tiling_state = state;
  this->e_type = type;
  this->parent_container = parent;
  this->server = server;
  this->toplevel = toplevel;
};

enum gfwl_split_direction GfContainer::get_split_dir() {
  switch (this->e_type) {
  case GFWL_CONTAINER_VSPLIT:
    return GFWL_SPLIT_DIR_VERT;
  case GFWL_CONTAINER_HSPLIT:
    return GFWL_SPLIT_DIR_HORI;
  case GFWL_CONTAINER_ROOT:
    return GFWL_SPLIT_DIR_HORI;
  default:
    return GFWL_SPLIT_DIR_UNKNOWN;
  }
}

// TODO: Replace duplicate parts with generalized helpers.
// Change this to get output size from the parent.
void GfContainer::vert_split_containers() {
  // Get count.
  int count = this->child_containers.size();
  if (count == 0) {
    wlr_log(WLR_DEBUG, "You probably don't want to divide by 0");
    return;
  }

  int width = this->box.width;
  int height = this->box.height;

  int per_win_height = height / count;

  // Set all sizes. (recycling count for the index)
  count = 0;
  for (auto curr : this->child_containers) {
    wlr_box box = {.x = this->box.x,
                   .y = this->box.y + (per_win_height * count),
                   .width = width,
                   .height = per_win_height};
    set_container_box(curr, box);
    count += 1;
  }
}

void GfContainer::hori_split_containers() {
  // Get count.
  int count = this->child_containers.size();
  if (count == 0) {
    wlr_log(WLR_DEBUG, "You probably don't want to divide by 0");
    return;
  }
  // Get Width and Height.
  int width = this->box.width;
  int height = this->box.height;

  // Get per_win_width.
  int per_win_width = width / count;

  // Set all sizes. (recycling count for the index)
  count = 0;
  for (auto curr : this->child_containers) {
    wlr_box box = {.x = this->box.x + (count * per_win_width),
                   .y = this->box.y,
                   .width = per_win_width,
                   .height = height};
    set_container_box(curr, box);
    count += 1;
  }
}

void GfContainer::split_containers() {
  switch (this->get_split_dir()) {
  case GFWL_SPLIT_DIR_HORI:
    this->hori_split_containers();
    break;
  case GFWL_SPLIT_DIR_VERT:
    this->vert_split_containers();
    break;
  default:
    break;
  }
}

void GfContainer::parse_containers() {
  // Get output if we're at the root.
  if (this->is_root) {
    struct gfwl_output *output =
        wl_container_of(this->server->outputs.next, output, link);
	assert(output);
    this->box.width = output->wlr_output->width;
    this->box.height = output->wlr_output->height;
  }
  this->split_containers();
  for (auto child : this->child_containers) {
    if (child->e_type == GFWL_CONTAINER_HSPLIT ||
        child->e_type == GFWL_CONTAINER_VSPLIT) {
      child->parse_containers();
    }
  }
}

// Only supports toplevels for now.
void set_container_box(std::shared_ptr<GfContainer> container,
                       struct wlr_box box) {
  container->box = box;
  if (container->e_type == GFWL_CONTAINER_TOPLEVEL) {
    struct wlr_xdg_toplevel *toplevel = container->toplevel->xdg_toplevel;
    // Set the size.
    wlr_xdg_toplevel_set_size(toplevel, box.width, box.height);
    // Set the position.
    struct wlr_scene_tree *scene_tree = (wlr_scene_tree *)toplevel->base->data;
    wlr_scene_node_set_position(&scene_tree->node, box.x, box.y);
  }
};
