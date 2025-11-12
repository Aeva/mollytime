
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


def blank_selectbar_bg(editor):
    w = editor.grid_size * 3
    h = editor.play_rect.height

    surface = mollytime.draw.Texture((w, h))
    surface.fill(editor.select_color, 0.8)
    return surface


class select_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = mollytime.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect > select")

        w = editor.grid_size * 3
        h = editor.play_rect.height
        self.selectbar_rect = mollytime.Rect(0, 0, w, h)

        self.selectbar_rect = mollytime.Rect(0, 0, w, h)
        self.lhs_selectbar_rect = self.selectbar_rect.copy()
        self.rhs_selectbar_rect = mollytime.Rect(editor.play_rect.width - w, 0, w, h)
        self.screen_label_rect.left += w

        self.lhs_selectbar_bg = blank_selectbar_bg(editor)
        self.rhs_selectbar_bg = blank_selectbar_bg(editor)

        self.lhs_targets = []
        self.lhs_selection = None

        self.rhs_targets = []
        self.connections = []

        self.repopulate_sidebar(editor)

    def repopulate_sidebar(self, editor):
        self.update_play_area = True
        lhs_tile = editor.lhs_selection()
        rhs_tile = editor.rhs_selection()

        screen_label_color = (255, 255, 255)
        font_path, size = AFACAD_REGULAR, editor.grid_size

        self.lhs_selectbar_bg = blank_selectbar_bg(editor)
        self.rhs_selectbar_bg = blank_selectbar_bg(editor)

        tile_size = editor.grid_size * 2
        radius = editor.grid_size
        draw_start = (self.selectbar_rect.centerx - radius, tile_size)
        draw_stop = (self.selectbar_rect.centerx - radius, self.selectbar_rect.bottom - tile_size * 2)

        def header_label(text, target):
            def inner(line_offset, line):
                surface = render_text(font_path, size, screen_label_color, line)
                rect = surface.get_rect()
                rect.top = self.screen_label_rect.top + size * line_offset
                rect.left = (self.selectbar_rect.width - rect.width) / 2
                target.blit(surface, rect)
            for line in enumerate(text.strip().split("\n")):
                inner(*line)

        terminals = {}

        self.lhs_targets = []
        if lhs_tile:
            header_label("select\nsource", self.lhs_selectbar_bg)

            lhs_outputs = editor.patch.get_tile_output_ports(lhs_tile)
            count = len(lhs_outputs)

            if self.lhs_selection and decode_port_tile(self.lhs_selection) != lhs_tile:
                self.lhs_selection = None

            if self.lhs_selection is None and count > 0:
                self.lhs_selection = lhs_outputs[0]

            click_start = (self.lhs_selectbar_rect.centerx - radius, tile_size)
            click_stop = (self.lhs_selectbar_rect.centerx - radius, self.lhs_selectbar_rect.bottom - tile_size * 2)
            outline = 8

            self.lhs_targets = []
            for index, port in enumerate(lhs_outputs):
                alpha = (index + 1) / (count + 1)
                draw_pos = vec_lerp(draw_start, draw_stop, alpha)
                draw_rect = mollytime.Rect(draw_pos, (editor.grid_size * 2, editor.grid_size * 2))
                click_pos = vec_lerp(click_start, click_stop, alpha)
                click_rect = mollytime.Rect(click_pos, (editor.grid_size * 2, editor.grid_size * 2))
                self.lhs_targets.append((click_rect, port))

                tile_id = decode_port_tile(port)
                label = editor.patch.get_output_port_name(port)
                label = f"{label}\n(output)"
                assert(editor.patch.get_tile_symbol(tile_id) != OpCode.OUT)

                if port == self.lhs_selection:
                    editor.dark_tile_bg.draw(self.lhs_selectbar_bg, draw_rect, label)
                    draw_outline(self.lhs_selectbar_bg, draw_rect, parse_color("#fff000"), outline)
                    terminals[port] = vec_add(click_pos, (tile_size + outline, tile_size // 2))
                else:
                    editor.dark_tile_bg.draw(self.lhs_selectbar_bg, draw_rect, label)
                    terminals[port] = vec_add(click_pos, (tile_size, tile_size // 2))
        else:
            self.lhs_selection = None

        self.rhs_targets = []
        if rhs_tile:
            header_label("toggle\ndest", self.rhs_selectbar_bg)

            rhs_inputs = editor.patch.get_tile_input_ports(rhs_tile)
            count = len(rhs_inputs)

            click_start = (self.rhs_selectbar_rect.centerx - radius, tile_size)
            click_stop = (self.rhs_selectbar_rect.centerx - radius, self.rhs_selectbar_rect.bottom - tile_size * 2)

            for index, port in enumerate(rhs_inputs):
                alpha = (index + 1) / (count + 1)
                draw_pos = vec_lerp(draw_start, draw_stop, alpha)
                draw_rect = mollytime.Rect(draw_pos, (editor.grid_size * 2, editor.grid_size * 2))
                click_pos = vec_lerp(click_start, click_stop, alpha)
                click_rect = mollytime.Rect(click_pos, (editor.grid_size * 2, editor.grid_size * 2))
                self.rhs_targets.append((click_rect, port))
                terminals[port] = vec_add(click_pos, (0, tile_size // 2))

                tile_id = decode_port_tile(port)
                label = editor.patch.get_input_port_name(port)
                if editor.patch.get_tile_symbol(tile_id) == OpCode.OUT:
                    label = "line\nout"
                label = f"{label}\n(input)"
                editor.tile_bg.draw(self.rhs_selectbar_bg, draw_rect, label)

        self.connections = []
        if lhs_tile and rhs_tile:
            for out_port, in_port in editor.patch.wires:
                if out_port == self.lhs_selection and in_port in rhs_inputs:
                    out_tile = decode_port_tile(out_port)
                    in_tile = decode_port_tile(in_port)
                    assert(out_tile == lhs_tile and in_tile == rhs_tile)
                    self.connections.append((terminals[out_port], terminals[in_port]))

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

            self.side_bar_targets.append((connect_rect, editor.clear_selection_target, self.goto_clear_selection))

            if editor.rhs_selection():
                rect = mollytime.Rect(
                    editor.grid_size,
                    4 * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)

                self.side_bar_targets.append((rect, editor.swap_sides_target, self.goto_swap_sides))

    def goto_frozen(self, editor):
        editor.freeze()
        self.repopulate_sidebar(editor)

    def goto_unfrozen(self, editor):
        editor.unfreeze()
        self.repopulate_sidebar(editor)

    def goto_inspect_screen(self, editor):
        self.live = False

    def goto_clear_selection(self, editor):
        editor.clear_selection()
        self.update_play_area = True
        self.repopulate_sidebar(editor)

    def goto_swap_sides(self, editor):
        editor.reverse_selection()
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
            if self.lhs_selectbar_rect.collidepoint(pos):
                for rect, port in self.lhs_targets:
                    if rect.collidepoint(pos):
                        self.lhs_selection = port
                        self.repopulate_sidebar(editor)
                        break
                return

            if self.rhs_selectbar_rect.collidepoint(pos):
                if self.lhs_selection:
                    for rect, port in self.rhs_targets:
                        if rect.collidepoint(pos):
                            lhs_tile = editor.lhs_selection()
                            rhs_tile = editor.rhs_selection()

                            assert(decode_port_tile(self.lhs_selection) == lhs_tile)
                            assert(decode_port_tile(port) == rhs_tile)

                            editor.toggle_connection(self.lhs_selection, port)
                            editor.clear_selection()
                            self.repopulate_sidebar(editor)
                            break
                return

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

            frame = editor.reset_play_area()

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

            frame.blit(self.lhs_selectbar_bg, self.lhs_selectbar_rect)
            frame.blit(self.rhs_selectbar_bg, self.rhs_selectbar_rect)
            for start, stop in self.connections:
                draw_arrow(frame, parse_color("#0F0"), start, stop, 8)

            frame.blit(self.screen_label_surface, self.screen_label_rect)

        # draw sidebar
        if self.update_sidebar or self.force_redraw:
            self.update_sidebar = False
            update_anything = True

            frame = editor.reset_side_bar()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            self.draw_system_status(editor, frame)

        if update_anything:
            self.force_redraw = False
            editor.present()
        else:
            editor.clock.tick(60)
