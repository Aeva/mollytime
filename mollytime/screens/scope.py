
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

import math
import time
import random
from .common import *

from .. import mollytime

class scope_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = mollytime.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect > scope", parse_color("#333"))
        self.repopulate_sidebar(editor)

        self.scope_surface = editor.play_area.surface.copy()
        self.scope_surface.fill(parse_color("#000"))
        self.wire_color = parse_color("#333")

        self.beam_color = parse_color("#fff000")
        self.scope_start = time.time()
        self.last_x = 0
        min_sample, max_sample = editor.patch.read_scope_probe()

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

        active_icon = editor.scope_active

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None)]

    def goto_inspect_screen(self, editor):
        self.live = False

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
            # begin play are view panning
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
        # always draw the play area sans background
        editor.play_area.focus_x = editor.focus_x
        editor.play_area.focus_y = editor.focus_y

        frame = editor.replace_play_area(self.scope_surface)

        for tile_id, tile_xy in editor.tile_positions.items():
            rect = editor.get_tile_rect(tile_id)
            label = editor.patch.get_tile_label(tile_id)
            editor.dark_tile_bg.draw(frame, rect, label)

        for (out_port, in_port) in editor.patch.wires:
            lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
            rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
            radius = max(1, editor.grid_size // 12)
            draw_arrow(frame, self.wire_color, lhs_rect, rhs_rect, radius)

        min_sample, max_sample = editor.patch.read_scope_probe()
        is_nan = min_sample == 1.0 and max_sample == -1.0
        if is_nan:
            min_sample, max_sample = max_sample, min_sample

        abs_sample = max(abs(min_sample), abs(max_sample))

        frame_start = time.time()
        elapsed = (frame_start - self.scope_start) / 5

        beam_x = int(editor.play_rect.w * elapsed)
        center = editor.play_rect.h // 2 -1
        min_beam_y = center * -min_sample + center
        max_beam_y = center * -max_sample + center
        w = max(1, abs(beam_x - self.last_x))
        h = max(1, abs(max_beam_y - min_beam_y))
        beam_rect = mollytime.Rect((self.last_x, max_beam_y), (w, h))
        clear_rect = mollytime.Rect((self.last_x, 0), (w, editor.play_rect.h))

        beam_color = self.beam_color
        if is_nan:
            beam_color = (255, 0, 255)
        elif abs_sample > 1.0:
            beam_coolor = (255, 0, 0)

        mollytime.draw.rect(frame, (0, 0, 0), clear_rect)
        mollytime.draw.rect(frame, beam_color, beam_rect)

        if elapsed > 1:
            self.last_x = 0
            self.scope_start = frame_start
            self.beam_color = (random.randint(32, 255), random.randint(32, 255), random.randint(32, 255))
        else:
            self.last_x = beam_x

        frame.blit(self.screen_label_surface, self.screen_label_rect)

        # draw sidebar
        if self.update_sidebar or self.force_redraw:
            self.update_sidebar = False
            update_anything = True

            frame = editor.reset_side_bar()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            self.draw_system_status(editor, frame)
            editor.screen.blit(frame, editor.side_bar.viewport)

        self.draw_touch_points(editor)
        editor.present()
