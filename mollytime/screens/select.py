
# Copyright 2025 Aeva Palecek
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from .common import *
from .connect import connect_screen

from .. import mollytime


class select_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = mollytime.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect > select")
        self.repopulate_sidebar(editor)

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        goto_inspect_rect = mollytime.Rect(
            editor.grid_size,
            0 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_inspect_icon = editor.inspect_target

        active_rect = mollytime.Rect(
            editor.grid_size,
            1 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.select_active

        toggle_frozen_rect = mollytime.Rect(
            editor.grid_size,
            2 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        if editor.is_frozen():
            toggle_frozen_icon = editor.unfreeze_patch
            toggle_frozen = self.goto_unfrozen
        else:
            toggle_frozen_icon = editor.freeze_patch
            toggle_frozen = self.goto_frozen

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None),
            (toggle_frozen_rect, toggle_frozen_icon, toggle_frozen)]

        if editor.connectable_selection():
            connect_rect = mollytime.Rect(
                editor.grid_size,
                3 * editor.grid_size * 3,
                editor.grid_size * 2, editor.grid_size * 2)

            connect_icon = editor.connect_target

            self.side_bar_targets.append((connect_rect, connect_icon, self.goto_connect_screen))

            if implicit_wire := editor.implicit_wire_from_selection():
                rect = mollytime.Rect(
                    editor.grid_size,
                    4 * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)
                icon = None
                if implicit_wire not in editor.patch.wires:
                    icon = editor.auto_connect
                else:
                    icon = editor.auto_disconnect
                self.side_bar_targets.append((rect, icon, self.toggle_implicit_connection))

    def goto_frozen(self, editor):
        editor.freeze()
        self.repopulate_sidebar(editor)

    def goto_unfrozen(self, editor):
        editor.unfreeze()
        self.repopulate_sidebar(editor)

    def goto_inspect_screen(self, editor):
        self.live = False

    def goto_connect_screen(self, editor):
        if editor.connectable_selection():
            overlay = connect_screen(editor)
            self.purge_events()
            self.update_play_area = True
            self.update_sidebar = True
            editor.clear_selection()
            self.repopulate_sidebar(editor)

    def toggle_implicit_connection(self, editor):
        if wire := editor.implicit_wire_from_selection():
            editor.toggle_connection(*wire)
            editor.clear_selection()
            self.update_play_area = True
            self.repopulate_sidebar(editor)

    def on_move(self, editor, pos, event):
        self.cursor_pos = pos

        if not self.press_start:
            return

        move_x = pos[0] - self.press_start[0]
        move_y = pos[1] - self.press_start[1]

        if move_x != 0 or move_y != 0:
            editor.focus_x -= move_x
            editor.focus_y -= move_y
            self.update_play_area = True

        self.press_start = pos

    def on_press(self, editor, pos, event):
        if editor.play_rect.collidepoint(pos):
            something_happened = False
            for tile_id, (tile_x, tile_y) in editor.tile_positions.items():
                rect = mollytime.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)
                if rect.collidepoint(pos):
                    something_happened = True
                    self.update_play_area = True
                    state = editor.toggle_selection(tile_id)
                    self.repopulate_sidebar(editor)
                    break

            if not something_happened:
                self.press_start = pos

        elif editor.side_bar_rect.collidepoint(pos):
            # test side bar targets
            rel_pos = (pos[0] - editor.side_bar.viewport.x, pos[1] - editor.side_bar.viewport.y)
            for rect, surface, action in self.side_bar_targets:
                if action is not None and rect.collidepoint(rel_pos):
                    action(editor)
                    return

    def on_release(self, editor, pos, event):
        self.press_start = None

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area or self.force_redraw:
            self.update_play_area = False
            update_anything = True

            editor.play_area.focus_x = editor.focus_x
            editor.play_area.focus_y = editor.focus_y
            editor.play_area.redraw()

            frame = editor.play_area.surface.copy()

            for tile_id, tile_xy in editor.tile_positions.items():
                rect = editor.get_tile_rect(tile_id)
                label = editor.patch.get_tile_label(tile_id)
                if editor.is_selected(tile_id):
                    pattern = editor.selected_tile_bg
                elif self.draw_clip and editor.patch.get_tile_symbol(tile_id) == OpCode.OUT:
                    pattern = editor.clip_tile
                else:
                    pattern = editor.tile_bg
                pattern.draw(frame, rect, label)

            for (out_port, in_port) in editor.patch.wires:
                lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
                rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
                radius = max(1, editor.grid_size // 12)
                draw_arrow(frame, (0, 0, 0), lhs_rect, rhs_rect, radius)

            frame.blit(self.screen_label_surface, self.screen_label_rect)
            editor.screen.blit(frame, editor.play_area.viewport)

        # draw sidebar
        if self.update_sidebar or self.force_redraw:
            self.update_sidebar = False
            update_anything = True

            frame = editor.side_bar.surface.copy()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            self.draw_system_status(editor, frame)
            editor.screen.blit(frame, editor.side_bar.viewport)

        if update_anything:
            self.force_redraw = False
            self.draw_touch_points(editor)
            mollytime.draw.flip()
        else:
            editor.clock.tick(60)
