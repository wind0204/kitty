#include <float.h>
#include "state.h"

inline static float
norm(float x, float y) {
    return sqrtf(x * x + y * y);
}

static void
update_cursor_trail_target(CursorTrail *ct, Window *w) {
#define EDGE(axis, index) ct->cursor_edge_##axis[index]
#define WD w->render_data
    float left = FLT_MAX, right = FLT_MAX, top = FLT_MAX, bottom = FLT_MAX;
    switch (WD.screen->cursor_render_info.shape) {
        case CURSOR_BLOCK:
        case CURSOR_HOLLOW:
        case CURSOR_BEAM:
        case CURSOR_UNDERLINE:
            left = WD.xstart + WD.screen->cursor_render_info.x * WD.dx;
            bottom = WD.ystart - (WD.screen->cursor_render_info.y + 1) * WD.dy;
        default:
            break;
    }
    switch (WD.screen->cursor_render_info.shape) {
        case CURSOR_BLOCK:
        case CURSOR_HOLLOW:
            right = left + WD.dx;
            top = bottom + WD.dy;
            break;
        case CURSOR_BEAM:
            right = left + WD.dx / WD.screen->cell_size.width * OPT(cursor_beam_thickness);
            top = bottom + WD.dy;
            break;
        case CURSOR_UNDERLINE:
            right = left + WD.dx;
            top = bottom + WD.dy / WD.screen->cell_size.height * OPT(cursor_underline_thickness);
            break;
        default:
            break;
    }
    if (left != FLT_MAX) {
        EDGE(x, 0) = left;
        EDGE(x, 1) = right;
        EDGE(y, 0) = top;
        EDGE(y, 1) = bottom;
    }
}

static void
update_cursor_trail_target_in_another_window(CursorTrail *ct, Window *tgt_w, Window *own_w, int bias_x, int bias_y) {
    WindowRenderData *twd = &tgt_w->render_data;
    WindowRenderData *owd = &own_w->render_data;
    float left = FLT_MAX, right = FLT_MAX, top = FLT_MAX, bottom = FLT_MAX;
    switch (owd->screen->cursor_render_info.shape) {
        case CURSOR_BLOCK:
        case CURSOR_HOLLOW:
        case CURSOR_BEAM:
        case CURSOR_UNDERLINE:
            left = owd->xstart + twd->screen->cursor_render_info.x * owd->dx;
            bottom = owd->ystart - (twd->screen->cursor_render_info.y + 1) * owd->dy;
        default:
            break;
    }
    switch (owd->screen->cursor_render_info.shape) {
        case CURSOR_BLOCK:
        case CURSOR_HOLLOW:
            right = left + owd->dx;
            top = bottom + owd->dy;
            break;
        case CURSOR_BEAM:
            right = left + owd->dx / owd->screen->cell_size.width * OPT(cursor_beam_thickness);
            top = bottom + owd->dy;
            break;
        case CURSOR_UNDERLINE:
            right = left + owd->dx;
            top = bottom + owd->dy / owd->screen->cell_size.height * OPT(cursor_underline_thickness);
            break;
        default:
            break;
    }
    if (left != FLT_MAX) {
        EDGE(x, 0) = left + bias_x * owd->dx / owd->screen->cell_size.width;
        EDGE(x, 1) = right + bias_x * owd->dx / owd->screen->cell_size.width;
        EDGE(y, 0) = top + bias_y * owd->dy / owd->screen->cell_size.height;
        EDGE(y, 1) = bottom + bias_y * owd->dy / owd->screen->cell_size.height;
    }
}

static bool
should_skip_cursor_trail_update(CursorTrail *ct, Window *w, OSWindow *os_window) {
    if (os_window->live_resize.in_progress) {
        return true;
    }

    if (OPT(cursor_trail_start_threshold) > 0 && !ct->needs_render) {
        int dx = (int)round((ct->corner_x[0] - EDGE(x, 1)) / WD.dx);
        int dy = (int)round((ct->corner_y[0] - EDGE(y, 0)) / WD.dy);
        if (abs(dx) + abs(dy) <= OPT(cursor_trail_start_threshold)) {
            return true;
        }
    }
    return false;
}

static void
update_cursor_trail_corners(CursorTrail *ct, Window *w, monotonic_t now, OSWindow *os_window) {
    // the trail corners move towards the cursor corner at a speed proportional to their distance from the cursor corner.
    // equivalent to exponential ease out animation.
    static const int corner_index[2][4] = {{1, 1, 0, 0}, {0, 1, 1, 0}};

    // the decay time for the trail to reach 1/1024 of its distance from the cursor corner
    float decay_fast = OPT(cursor_trail_decay_fast);
    float decay_slow = OPT(cursor_trail_decay_slow);

    if (should_skip_cursor_trail_update(ct, w, os_window)) {
        for (int i = 0; i < 4; ++i) {
            ct->corner_x[i] = EDGE(x, corner_index[0][i]);
            ct->corner_y[i] = EDGE(y, corner_index[1][i]);
        }
    }
    else if (ct->updated_at < now) {
        float cursor_center_x = (EDGE(x, 0) + EDGE(x, 1)) * 0.5f;
        float cursor_center_y = (EDGE(y, 0) + EDGE(y, 1)) * 0.5f;
        float cursor_diag_2 = norm(EDGE(x, 1) - EDGE(x, 0), EDGE(y, 1) - EDGE(y, 0)) * 0.5f;
        float dt = (float)monotonic_t_to_s_double(now - ct->updated_at);

        // dot product here is used to dynamically adjust the decay speed of
        // each corner. The closer the corner is to the cursor, the faster it
        // moves.
        float dx[4], dy[4];
        float dot[4];  // dot product of "direction vector" and "cursor center to corner vector"
        for (int i = 0; i < 4; ++i) {
            dx[i] = EDGE(x, corner_index[0][i]) - ct->corner_x[i];
            dy[i] = EDGE(y, corner_index[1][i]) - ct->corner_y[i];
            if (fabsf(dx[i]) < 1e-6 && fabsf(dy[i]) < 1e-6) {
                dx[i] = dy[i] = 0.0f;
                dot[i] = 0.0f;
                continue;
            }
            dot[i] = (dx[i] * (EDGE(x, corner_index[0][i]) - cursor_center_x) +
                      dy[i] * (EDGE(y, corner_index[1][i]) - cursor_center_y)) /
                     cursor_diag_2 / norm(dx[i], dy[i]);
        }
        float min_dot = FLT_MAX, max_dot = -FLT_MAX;
        for (int i = 0; i < 4; ++i) {
            min_dot = fminf(min_dot, dot[i]);
            max_dot = fmaxf(max_dot, dot[i]);
        }

        for (int i = 0; i < 4; ++i) {
            if ((dx[i] == 0 && dy[i] == 0) || min_dot == FLT_MAX) {
                continue;
            }

            float decay = (min_dot == max_dot)
                ? decay_slow
                : decay_slow + (decay_fast - decay_slow) * (dot[i] - min_dot) / (max_dot - min_dot);
            float step = 1.0f - exp2f(-10.0f * dt / decay);
            ct->corner_x[i] += dx[i] * step;
            ct->corner_y[i] += dy[i] * step;
        }
    }
}

static void
update_cursor_trail_opacity(CursorTrail *ct, Window *w, monotonic_t now) {
    const bool cursor_trail_always_visible = false;
    if (cursor_trail_always_visible) {
        ct->opacity = 1.0f;
    } else if (WD.screen->modes.mDECTCEM) {
        ct->opacity += (float)monotonic_t_to_s_double(now - ct->updated_at) / OPT(cursor_trail_decay_slow);
        ct->opacity = fminf(ct->opacity, 1.0f);
    } else {
        ct->opacity -= (float)monotonic_t_to_s_double(now - ct->updated_at) / OPT(cursor_trail_decay_slow);
        ct->opacity = fmaxf(ct->opacity, 0.0f);
    }
}

static void
update_cursor_trail_needs_render(CursorTrail *ct, Window *w) {
    static const int corner_index[2][4] = {{1, 1, 0, 0}, {0, 1, 1, 0}};
    ct->needs_render = false;

    // check if any corner is still far from the cursor corner, so it should be rendered
    const float dx_threshold = WD.dx / WD.screen->cell_size.width * 0.5f;
    const float dy_threshold = WD.dy / WD.screen->cell_size.height * 0.5f;
    for (int i = 0; i < 4; ++i) {
        float dx = fabsf(EDGE(x, corner_index[0][i]) - ct->corner_x[i]);
        float dy = fabsf(EDGE(y, corner_index[1][i]) - ct->corner_y[i]);
        if (dx_threshold <= dx || dy_threshold <= dy) {
            ct->needs_render = true;
            break;
        }
    }
}

bool
update_cursor_trail(CursorTrail *ct, Window *w, monotonic_t now, OSWindow *os_window) {
    id_type max_fc_count = 0;
    id_type prev_focused_os_window = 0, focused_os_window = 0;
    // This code will pick arbitrary windows on Sway. Just utilize global_state.prev_focused_os_window
    //for (size_t i = 0; i < global_state.num_os_windows; i++) {
    //    OSWindow *w = &global_state.os_windows[i];
    //    if (w->last_focused_counter > max_fc_count) {
    //        prev_focused_os_window = focused_os_window;
    //        focused_os_window = w->id; max_fc_count = w->last_focused_counter;
    //    }
    //}
    for (size_t i = 0; i < global_state.num_os_windows; i++) {
        OSWindow *w = &global_state.os_windows[i];
        if (w->last_focused_counter > max_fc_count) {
            focused_os_window = w->id; max_fc_count = w->last_focused_counter;
        }
    }
    prev_focused_os_window = global_state.prev_focused_os_window;

    if ( OPT(cursor_trail_choreographed) &&
            OPT(cursor_trail) <= now - WD.screen->cursor->position_changed_by_client_at &&
            os_window->id == prev_focused_os_window ) {
        if (!WD.screen->paused_rendering.expires_at) {
            OSWindow *focused_osw = 0;
            for (size_t o = 0; o < global_state.num_os_windows; o++) {
                OSWindow *osw = global_state.os_windows + o;
                if (osw->id == focused_os_window) {
                    focused_osw = osw;
                }
            }
            if ( focused_osw ) {
                int focused_window_pos_x, focused_window_pos_y, prev_focused_window_pos_x, prev_focused_window_pos_y;
                focused_window_pos_x = focused_osw->before_fullscreen.x;
                focused_window_pos_y = focused_osw->before_fullscreen.y;
                prev_focused_window_pos_x = os_window->before_fullscreen.x;
                prev_focused_window_pos_y = os_window->before_fullscreen.y;

                Tab *focused_tab = focused_osw->tabs + focused_osw->last_active_tab;
                update_cursor_trail_target_in_another_window(ct,
                        focused_tab->windows + focused_tab->active_window,
                        w,
                        focused_window_pos_x - prev_focused_window_pos_x,
                        prev_focused_window_pos_y - focused_window_pos_y
                        );
            }
        }
    } else {
        if ( !WD.screen->paused_rendering.expires_at &&
                OPT(cursor_trail) <= now - WD.screen->cursor->position_changed_by_client_at ) {
            if ( OPT(cursor_trail_choreographed) && os_window->is_focused &&
                    os_window->id == focused_os_window &&
                    global_state.origin_of_trail != prev_focused_os_window ) {
                OSWindow *prev_focused_osw = 0;
                for (size_t o = 0; o < global_state.num_os_windows; o++) {
                    OSWindow *osw = global_state.os_windows + o;
                    if (osw->id == prev_focused_os_window) {
                        prev_focused_osw = osw;
                    }
                }
                if ( prev_focused_osw ) {
                    int focused_window_pos_x, focused_window_pos_y, prev_focused_window_pos_x, prev_focused_window_pos_y;
                    focused_window_pos_x = os_window->before_fullscreen.x;
                    focused_window_pos_y = os_window->before_fullscreen.y;
                    prev_focused_window_pos_x = prev_focused_osw->before_fullscreen.x;
                    prev_focused_window_pos_y = prev_focused_osw->before_fullscreen.y;

                    // Prepare some pointers
                    Tab *prev_focused_tab = &prev_focused_osw->tabs[prev_focused_osw->last_active_tab];
                    Window *prev_focused_window = &prev_focused_tab->windows[prev_focused_tab->active_window];

                    // Update the bottom and left edges
                    for ( int i = 0;  i < 4;  i++ ) {
                        ct->corner_x[i] = prev_focused_window->render_data.screen->cursor_render_info.x
                            * w->render_data.dx;
                        ct->corner_x[i] += w->render_data.xstart;
                        ct->corner_y[i] = 0 - prev_focused_window->render_data.screen->cursor_render_info.y
                            * w->render_data.dy;
                        ct->corner_y[i] += w->render_data.ystart - w->render_data.dy;

                        ct->corner_x[i] += (prev_focused_window_pos_x - focused_window_pos_x) *
                            w->render_data.dx / w->render_data.screen->cell_size.width;
                        ct->corner_y[i] -= (prev_focused_window_pos_y - focused_window_pos_y) *
                            w->render_data.dy / w->render_data.screen->cell_size.height;
                    }

                    // Update the top and right edges
                    switch (prev_focused_window->render_data.screen->cursor_render_info.shape) {
                        case CURSOR_BLOCK:
                        case CURSOR_HOLLOW:
                            ct->corner_x[0] += w->render_data.dx;
                            ct->corner_x[1] += w->render_data.dx;
                            ct->corner_y[0] += w->render_data.dy;
                            ct->corner_y[3] += w->render_data.dy;
                            break;
                        case CURSOR_BEAM:
                            ct->corner_x[0] += w->render_data.dx / w->render_data.screen->cell_size.width
                                * OPT(cursor_beam_thickness);
                            ct->corner_x[1] += w->render_data.dx / w->render_data.screen->cell_size.width
                                * OPT(cursor_beam_thickness);
                            ct->corner_y[0] += w->render_data.dy;
                            ct->corner_y[3] += w->render_data.dy;
                            break;
                        case CURSOR_UNDERLINE:
                            ct->corner_x[0] += w->render_data.dx;
                            ct->corner_x[1] += w->render_data.dx;
                            ct->corner_y[0] += w->render_data.dy / w->render_data.screen->cell_size.height
                                * OPT(cursor_underline_thickness);
                            ct->corner_y[3] += w->render_data.dy / w->render_data.screen->cell_size.height
                                * OPT(cursor_underline_thickness);
                            break;
                        default:
                            break;
                    }

                    // Mark it so as for the future self to know that corner coordinates were
                    // copied from the previosly focused OSWindow
                    global_state.origin_of_trail = prev_focused_os_window;
                }
            }
            update_cursor_trail_target(ct, w);
        }
    }

    update_cursor_trail_corners(ct, w, now, os_window);
    update_cursor_trail_opacity(ct, w, now);

    bool needs_render_prev = ct->needs_render;
    update_cursor_trail_needs_render(ct, w);

    ct->updated_at = now;

    // returning true here will cause the cells to be drawn
    return ct->needs_render || needs_render_prev;
}

#undef WD
#undef EDGE
