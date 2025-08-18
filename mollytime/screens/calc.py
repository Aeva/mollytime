
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

import operator
from .common import *

from fonts import *
from colors import *
from patterns import *


class calculator_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None

        self.editing_tile = editor.lhs_selection()

        self.initial_value = editor.patch.get_constant(self.editing_tile)
        term = str(self.initial_value)
        if term.count('.') != 0:
            whole, fractional = term.split('.')
            if len(fractional) == fractional.count('0'):
                term = whole

        self.history = []
        self.calculation = [term]
        self.initial_state = [term]

        screen_label_color = (255, 255, 255)
        self.set_screen_label(editor, "inspect > calculator", screen_label_color, 1)
        self.repopulate_sidebar(editor)
        self.render_play_area(editor)

        rows = [
            ['reset', '440', 'clear', 'back\nspace'],
            [7, 8, 9, '÷'],
            [4, 5, 6, '×'],
            [1, 2, 3, '+'],
            [0, '.', 'sign\nflip', '=']]

        self.operators = {
            '^' : pow,
            '÷' : operator.truediv,
            '×' : operator.mul,
            '-' : operator.sub,
            '+' : operator.add,
        }

        per_row = max(map(len, rows))
        x_span = editor.grid_size * 3 * per_row
        y_span = editor.grid_size * 3 * len(rows)

        self.history_font = NATIONAL_PARK_REGULAR
        self.history_font_size = editor.grid_size

        x_start = editor.play_area.viewport.right - x_span
        y_start = editor.play_area.viewport.bottom - y_span
        self.text_anchor_x = editor.play_area.viewport.centerx + x_span / 2 - editor.grid_size * 4
        self.text_anchor_y = editor.play_area.viewport.bottom - editor.grid_size * 2

        self.buttons = []
        for y, row in enumerate(rows):
            y = y * editor.grid_size * 3 + y_start
            for x, label in enumerate(row):
                x = x * editor.grid_size * 3 + x_start
                if label is None:
                    continue
                rect = pygame.Rect(x, y, editor.grid_size * 2, editor.grid_size * 2)
                icon = plate_bg(editor.grid_size, editor.tile_color, str(label)).surface
                self.buttons.append((rect, icon, label))

    def numerate(self, text):
        return float(text) if text.count(".") else int(text)

    def advance(self, editor):
        if len(self.calculation) > 1:
            assert(len(self.calculation) == 3)
            lhs = self.numerate(self.calculation[0])
            op = self.operators[self.calculation[1]]
            rhs = self.numerate(self.calculation[2])

            if op == operator.truediv and rhs == 0:
                self.calculation = [self.calculation[0]]
                return

            value = op(lhs, rhs)
            if type(value) == complex:
                self.calculation = [self.calculation[0]]
                return

            term = str(value)
            if term.count('.') != 0:
                whole, fractional = term.split('.')
                if len(fractional) == fractional.count('0'):
                    term = whole
                elif len(fractional) > 4:
                    term = f"{value:.4f}"
            self.history.append(" ".join(self.calculation + ['=', term]))
            self.calculation = [term]

        new_value = self.numerate(self.calculation[0])
        editor.patch.set_constant(self.editing_tile, new_value)
        self.render_play_area(editor)

    def on_math(self, editor, label):
        if label == 'reset':
            self.history = []
            editor.patch.set_constant(self.editing_tile, self.initial_value)
            self.calculation = self.initial_state
        elif label == 'clear':
            self.calculation = ["0"]
        elif label == '440':
            self.calculation = ["440"]
        elif label == 'back\nspace':
            active = self.calculation[-1][:-1] or '0'
            self.calculation[-1] = active
        elif type(label) == int:
            active = self.calculation[-1]
            active = f"{active}{label}"
            if len(active) >= 2 and active[0] == '0' and active[1] != '.':
                active = active[1:]
            if len(active) >= 3 and active[:2] == "-0" and active[2] != '.':
                active = f"-{active[2:]}"
            self.calculation[-1] = active
        elif label == '.':
            active = self.calculation[-1]
            if active.count('.') == 0:
                active = f"{active}."
                self.calculation[-1] = active
        elif label == 'sign\nflip':
            active = self.calculation[-1]
            if active[0] == '-':
                active = active[1:]
            else:
                active = f"-{active}"
            self.calculation[-1] = active
        elif label in ['^', '×', '+', '÷', '-']:
            if len(self.calculation) == 3:
                if label == '+' and self.calculation[1] == '+' and self.calculation[2] == '0':
                    self.calculation[1] = '-'
                    return
                elif label == '×' and self.calculation[1] == '×' and self.calculation[2] == '0':
                    self.calculation[1] = '^'
                    return
                else:
                    self.advance(editor)
            self.calculation.append(label)
            self.calculation.append('0')
        elif label == '=':
            self.advance(editor)

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        goto_apply_rect = pygame.Rect(
            editor.grid_size,
            3 * 3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_cancel_rect = pygame.Rect(
            editor.grid_size,
            4 * 3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        self.side_bar_targets = [
            (goto_apply_rect, editor.apply_target, self.goto_apply),
            (goto_cancel_rect, editor.cancel_target, self.goto_cancel)]

    def render_play_area(self, editor):
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

        frame.set_alpha(int(0.25 * 255))

        self.bg = pygame.Surface((frame.get_width(), frame.get_height()))
        self.bg.fill((0, 0, 0))
        self.bg.blit(frame, (0, 0))

    def goto_apply(self, editor):
        self.advance(editor)
        editor.clear_selection()
        self.live = False

    def goto_cancel(self, editor):
        editor.patch.set_constant(self.editing_tile, self.initial_value)
        editor.clear_selection()
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
            for rect, _, label in self.buttons:
                if rect.collidepoint(pos):
                    self.on_math(editor, label)
                    self.update_play_area = True
                    break

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
            frame = self.bg.copy()

            for rect, icon, _ in self.buttons:
                frame.blit(icon, rect)

            font_path, size = self.history_font, self.history_font_size
            color = parse_color("#FFF")
            anchor_x = self.text_anchor_x
            anchor_y = self.text_anchor_y
            line_count = 0
            def print_text(text, alpha=1.0):
                nonlocal line_count
                surface = render_text(font_path, size, color, text)
                surface.set_alpha(int(alpha * 255))
                rect = surface.get_rect().copy()

                rect.right = anchor_x
                rect.centery = anchor_y - rect.height * line_count

                line_count += 1
                frame.blit(surface, rect)

            max_history = 14
            self.history = self.history[-max_history:]
            lines = list(reversed(self.history))

            print_text(" ".join(self.calculation))

            for index, text in enumerate(lines):
                alpha = index / max_history
                alpha = 1.0 - alpha
                alpha = alpha * alpha * alpha
                alpha *= .75
                print_text(text, alpha)

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
            pygame.display.flip()
        else:
            editor.clock.tick(60)
