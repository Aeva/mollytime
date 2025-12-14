
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

import os
import pathlib
import threading
import time
from .common import *
from .select import select_screen
from .calc import calculator_screen
from .pick_and_place import pick_and_place_screen

from .. import mollytime


def find_search_path():
    examples_dir = os.path.join(os.path.split(__file__)[0], "..", "..", "examples")
    if os.path.isdir(examples_dir):
        return os.path.abspath(examples_dir)

    home_dir = pathlib.Path.home()
    if os.path.isdir(home_dir):
        return home_dir

    return os.path.abspath(os.getcwd())


class inspect_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = mollytime.mouse.get_pos()
        self.press_start = None
        self.repopulate_sidebar(editor)
        self.pending_save = None
        self.pending_load = None
        self.search_path = find_search_path()
        self.save_path = None
        self.load_path = None

        self.hold = {}
        self.can_throttle = True

        self.scope_target = None
        self.scope_target_changed = True
        self.scope_overlay = None
        self.scope_start = time.time()
        self.last_x = 0
        self.beam_hue = random.randint(0, 360)
        self.advance_scope_color()
        self.update_interactive_tiles(editor)

    def resize_screen(self, editor):
        self.repopulate_sidebar(editor)
        self.scope_overlay = None

    def toggle_scope(self, editor, tile_id):
        if self.scope_target == tile_id:
            self.scope_target = None
        else:
            self.scope_target_changed = True
            self.scope_target = tile_id
        if self.scope_target:
            editor.patch.set_active_probe(self.scope_target)
        else:
            editor.patch.clear_active_probe()
        self.update_play_area = True

    def update_interactive_tiles(self, editor):
        self.interactive_tiles = []
        for tile_id in editor.tile_positions.keys():
            symbol = editor.patch.get_tile_symbol(tile_id)
            if symbol in (OpCode.BOOP, OpCode.TWEAK, OpCode.CONST, OpCode.SCOPE, OpCode.OUT):
                self.interactive_tiles.append(tile_id)

    def advance_scope_color(self):
        margin = 40
        self.beam_hue = (self.beam_hue + random.randint(margin, 360 - margin)) % 360
        self.beam_color = mollytime.hsl(self.beam_hue, 1.0, 0.5)

    def refresh_can_throttle(self, editor):
        for tile_id in editor.tile_positions.keys():
            symbol = editor.patch.get_tile_symbol(tile_id)
            if symbol == OpCode.BOOP or symbol == OpCode.TWEAK:
                self.can_throttle = False
                return
        self.can_throttle = True

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        active_rect = mollytime.Rect(
            editor.grid_size,
            0 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.inspect_active

        goto_move_rect = mollytime.Rect(
            editor.grid_size,
            1 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_move_icon = editor.move_target

        goto_select_rect = mollytime.Rect(
            editor.grid_size,
            2 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_select_icon = editor.select_target

        goto_save_rect = mollytime.Rect(
            editor.grid_size,
            3 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_save_icon = editor.save_target

        goto_load_rect = mollytime.Rect(
            editor.grid_size,
            4 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_load_icon = editor.load_target

        self.side_bar_targets = [
            (active_rect, active_icon, self.goto_self),
            (goto_move_rect, goto_move_icon, self.goto_pick_and_place_screen),
            (goto_select_rect, goto_select_icon, self.goto_select_screen),
            (goto_save_rect, goto_save_icon, self.goto_save_patch),
            (goto_load_rect, goto_load_icon, self.goto_load_patch)]

    def goto_self(self, editor):
        if self.scope_target:
            self.toggle_scope(editor, None)

    def goto_pick_and_place_screen(self, editor):
        overlay = pick_and_place_screen(editor)
        self.purge_events()
        self.toggle_scope(editor, None)
        self.update_interactive_tiles(editor)
        self.update_play_area = True
        self.update_sidebar = True
        editor.clear_selection()
        self.refresh_can_throttle(editor)

    def goto_select_screen(self, editor):
        overlay = select_screen(editor)
        editor.unfreeze()
        self.purge_events()
        self.toggle_scope(editor, None)
        self.update_play_area = True
        self.update_sidebar = True
        editor.clear_selection()
        self.refresh_can_throttle(editor)

    def goto_calculator(self, editor):
        overlay = calculator_screen(editor)
        self.purge_events()
        self.scope_target_changed = True
        self.update_play_area = True
        self.update_sidebar = True
        editor.clear_selection()

    def save_patch(self, editor):
        self.search_path = os.path.split(self.save_path)[0]
        editor.save_patch(self.save_path)

    def load_patch(self, editor):
        assert(os.path.isfile(self.load_path))
        self.search_path = os.path.split(self.load_path)[0]
        editor.load_patch(self.load_path)
        self.toggle_scope(editor, None)
        self.update_interactive_tiles(editor)
        self.force_redraw = True
        self.refresh_can_throttle(editor)

    def goto_save_patch(self, editor):
        assert(not self.pending_save)
        assert(not self.pending_load)

        self.purge_events()
        self.pending_save = True

        if self.search_path and os.path.isdir(self.search_path):
            patch_dir = self.search_path
        else:
            patch_dir = find_search_path()

        # hack fix for SDL_ShowSaveFileDialog on Windows interpreting patch_dir as
        # the directory below if the last character is not the directory delimiter.
        patch_dir = os.path.join(patch_dir, "")

        mollytime.display.show_save_dialog(patch_dir)

    def goto_load_patch(self, editor):
        assert(not self.pending_save)
        assert(not self.pending_load)

        self.purge_events()
        self.pending_load = True

        if self.search_path and os.path.isdir(self.search_path):
            patch_dir = self.search_path
        else:
            patch_dir = find_search_path()

        # hack fix for SDL_ShowOpenFileDialog on Windows interpreting patch_dir as
        # the directory below if the last character is not the directory delimiter.
        patch_dir = os.path.join(patch_dir, "")

        mollytime.display.show_load_dialog(patch_dir)

    def handle_escape(self, editor):
        if self.scope_target is None:
            super().handle_escape(editor)
        else:
            self.toggle_scope(editor, None)

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
        if self.pending_save or self.pending_load:
            return

        if editor.play_rect.collidepoint(pos):
            something_happened = False
            for tile_id, (tile_x, tile_y) in editor.tile_positions.items():
                rect = mollytime.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)
                if rect.collidepoint(pos):
                    symbol = editor.patch.get_tile_symbol(tile_id)
                    if symbol == OpCode.CONST:
                        editor.clear_selection()
                        editor.toggle_selection(tile_id)
                        self.goto_calculator(editor)
                        return
                    elif symbol == OpCode.BOOP:
                        if not event.button.touch:
                            self.hold["m"] = tile_id
                            editor.patch.set_special_input(tile_id, 1.0)
                        return
                    elif symbol in (OpCode.OUT, OpCode.SCOPE):
                        self.toggle_scope(editor, tile_id)
                        return
                    break

            if not something_happened:
                # begin play are view panning
                self.press_start = pos

        elif editor.side_bar_rect.collidepoint(pos):
            # test side bar targets
            rel_pos = (pos[0] - editor.side_bar.viewport.x, pos[1] - editor.side_bar.viewport.y)
            for rect, surface, action in self.side_bar_targets:
                if action is not None and rect.collidepoint(rel_pos):
                    action(editor)
                    self.resize_screen(editor)
                    return

    def on_release(self, editor, pos, event):
        self.press_start = None
        if not event.button.touch:
            if tile_id := self.hold.get("m", None):
                editor.patch.set_special_input(tile_id, 0.0)
                del self.hold["m"]

    def on_scroll(self, editor, event):
        for tile_id, (tile_x, tile_y) in editor.tile_positions.items():
            rect = mollytime.Rect(
                editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                editor.grid_size * 2, editor.grid_size * 2)
            if rect.collidepoint(event.pos) and editor.patch.get_tile_symbol(tile_id) == OpCode.TWEAK:
                # TODO touch pad also reports horizontal, so maybe we can do something with that?
                #print(event.horizontal, event.vertical)
                editor.patch.add_range_special_input(tile_id, event.vertical * -0.01, 0.0, 1.0)
                self.update_play_area = True

    def touch_start(self, editor, key, pos, event):
        super().touch_start(editor, key, pos, event)
        for tile_id, (tile_x, tile_y) in editor.tile_positions.items():
            rect = mollytime.Rect(
                editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                editor.grid_size * 2, editor.grid_size * 2)
            if rect.collidepoint(pos):
                symbol = editor.patch.get_tile_symbol(tile_id)
                if symbol == OpCode.BOOP:
                    self.hold[key] = tile_id
                    editor.patch.set_special_input(tile_id, 1.0)

    def touch_update(self, editor, key, pos, event):
        super().touch_update(editor, key, pos, event)

    def touch_end(self, editor, key, pos, event):
        super().touch_end(editor, key, pos, event)
        if tile_id := self.hold.get(key, None):
            editor.patch.set_special_input(tile_id, 0.0)
            del self.hold[key]

    @profile_function("inspect.draw")
    def draw(self, editor):
        if self.pending_save:
            save_status, save_path = mollytime.display.get_save_dialog_result()
            if save_status < 0:
                self.pending_save = False
            elif save_status > 0:
                self.pending_save = False
                if not save_path.endswith(".beep"):
                    save_path += ".beep"
                self.save_path = save_path
                self.save_patch(editor)

        if self.pending_load:
            load_status, load_path = mollytime.display.get_load_dialog_result()
            if load_status < 0:
                self.pending_load = False
            elif load_status > 0 and load_path:
                self.pending_load = False
                self.load_path = load_path
                assert(os.path.isfile(self.load_path))
                self.load_patch(editor)

        update_anything = False

        # draw the play area
        if self.update_play_area or self.force_redraw:
            self.update_play_area = False
            update_anything = True

            if editor.play_area.focus_x != editor.focus_x or editor.play_area.focus_y != editor.focus_y:
                editor.play_area.focus_x = editor.focus_x
                editor.play_area.focus_y = editor.focus_y
                editor.play_area.redraw()

            frame = editor.reset_play_area()

            radius = int(editor.grid_size * .72)
            for tile_id, tile_xy in editor.tile_positions.items():
                rect = editor.get_tile_rect(tile_id)
                label = editor.patch.get_tile_label(tile_id)
                symbol = editor.patch.get_tile_symbol(tile_id)
                if self.draw_clip and symbol == OpCode.OUT:
                    editor.clip_tile.draw(frame, rect, label)
                else:
                    if symbol == OpCode.TWEAK:
                        editor.tile_bg.draw(frame, rect)
                        arc = editor.patch.get_special_input(tile_id)
                        mollytime.draw.pie(frame, (0, 0, 0), rect.center, radius, .75, -arc)
                    else:
                        editor.tile_bg.draw(frame, rect, label)

            for (out_port, in_port) in editor.patch.wires:
                lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
                rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
                radius = max(1, editor.grid_size // 12)
                draw_arrow(frame, (0, 0, 0), lhs_rect, rhs_rect, radius)

        # draw sidebar
        if self.update_sidebar or self.force_redraw:
            self.update_sidebar = False
            update_anything = True

            frame = editor.reset_side_bar()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            self.draw_system_status(editor, frame)

        if self.scope_target:
            self.force_redraw = False
            if not self.scope_overlay:
                self.scope_overlay = mollytime.draw.Texture((editor.play_area.viewport.width, editor.play_area.viewport.height))
                self.scope_overlay.set_blend_mode(mollytime.draw.premultiplied_alpha)

                self.scope_history = self.scope_overlay.copy()
                self.scope_history.fill(parse_color("#000"), 0.0)
                self.scope_history.set_blend_mode(mollytime.draw.premultiplied_alpha)

                self.scope_mask = self.scope_overlay.copy()
                self.scope_mask.fill(parse_color("#000"), 0.0)

            interactive_hot = mollytime.draw.Texture((editor.play_area.viewport.width, editor.play_area.viewport.height))
            interactive_hot.set_blend_mode(mollytime.draw.multiply)
            interactive_hot.fill((255, 255, 255), 1.0)

            interactive_cold = mollytime.draw.Texture((editor.play_area.viewport.width, editor.play_area.viewport.height))
            interactive_cold.set_blend_mode(mollytime.draw.premultiplied_alpha)
            interactive_cold.fill(editor.scope_bg_color, 0.9)

            outline = editor.heavy_line
            half_outline = max(outline // 2, 1)

            # highlight the interactive elements
            for tile_id in self.interactive_tiles:
                rect = editor.get_tile_rect(tile_id)
                label = editor.patch.get_tile_label(tile_id)
                symbol = editor.patch.get_tile_symbol(tile_id)

                if tile_id == self.scope_target:
                    hot_alpha = 0.5
                    cold_alpha = 1.0
                    draw_outline(interactive_cold, rect, parse_color("#333"), outline)
                    outline_rect = mollytime.Rect(rect.x - half_outline, rect.y - half_outline, rect.width + outline, rect.height + outline)
                    draw_outline(interactive_cold, outline_rect, parse_color("#FFF"), half_outline)
                    draw_outline(interactive_hot, outline_rect, parse_color("#CCC"), half_outline)
                elif symbol in (OpCode.BOOP, OpCode.TWEAK, OpCode.OUT, OpCode.SCOPE):
                    hot_alpha = 0.3
                    cold_alpha = 0.8
                else:
                    hot_alpha = 0.2
                    cold_alpha = 0.2

                interactive_cold.fill_rect(editor.scope_bg_color, rect, 1.0)

                if symbol == OpCode.TWEAK:
                    editor.tile_bg.draw(interactive_cold, rect, alpha=cold_alpha, text_alpha = 1.0)
                    editor.tile_bg.draw(interactive_hot, rect, alpha=hot_alpha)
                    arc = editor.patch.get_special_input(tile_id)
                    mollytime.draw.pie(interactive_cold, (0, 0, 0), rect.center, 64, .75, -arc, alpha = 1.0)
                    mollytime.draw.pie(interactive_hot, (200, 200, 200), rect.center, 64, .75, -arc)
                else:
                    editor.tile_bg.draw(interactive_cold, rect, label, cold_alpha, text_alpha = 1.0)
                    editor.tile_bg.draw(interactive_hot, rect, label, hot_alpha)

            # draw the beam
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
                beam_color = (0, 0, 0)
            elif abs_sample > 1.0:
                beam_color = (255, 255, 255)

            if self.scope_target_changed:
                self.scope_target_changed = False
                self.scope_history.fill((0, 0, 0), 0.0)
                self.scope_mask.fill((0, 0, 0), 0.0)
                elapsed = 2
            else:
                self.scope_history.fill_rect((0, 0, 0), clear_rect, alpha=0.0)
                mollytime.draw.rect(self.scope_history, beam_color, beam_rect, alpha=0.8)
                self.scope_mask.fill_rect((0, 0, 0), clear_rect, alpha=0.0)
                mollytime.draw.rect(self.scope_mask, (255, 255, 255), beam_rect, alpha=1.0)

            if elapsed > 1:
                self.last_x = 0
                self.scope_start = frame_start
                self.advance_scope_color()
            else:
                self.last_x = beam_x

            line_color = parse_color("#CCC")
            for i in range(1, 8):
                alpha = i / 8
                y = int(editor.play_rect.h * alpha)
                start_pt = (0, y)
                end_pt = (editor.play_rect.w, y)
                if alpha == 0.5:
                    mollytime.draw.line(interactive_hot, line_color, start_pt, end_pt, 1, alpha = .9)
                    mollytime.draw.line(interactive_cold, line_color, start_pt, end_pt, 1, alpha = .2)
                elif i % 2 == 1:
                    mollytime.draw.line(interactive_hot, line_color, start_pt, end_pt, 1, alpha = .6)
                    mollytime.draw.line(interactive_cold, line_color, start_pt, end_pt, 1, alpha = .12)
                else:
                    mollytime.draw.line(interactive_hot, line_color, start_pt, end_pt, 1, alpha = .7)
                    mollytime.draw.line(interactive_cold, line_color, start_pt, end_pt, 1, alpha = .125)

            self.scope_mask.set_blend_mode(mollytime.draw.eraser)
            interactive_cold.blit(self.scope_mask, (0, 0))

            self.scope_overlay.blit(self.scope_history, (0, 0))
            self.scope_overlay.blit(interactive_hot, (0, 0))

            self.scope_mask.set_blend_mode(mollytime.draw.inverse_eraser)
            self.scope_overlay.blit(self.scope_mask, (0, 0))

            self.scope_overlay.blit(interactive_cold, (0, 0))

            # draw the emergency exit sign
            if is_nan:
                rect = editor.get_tile_rect(self.scope_target)
                editor.invalid_placement.draw(self.scope_overlay, rect, "NaN")

            # present w/ the scope overlay
            editor.present((self.scope_overlay, (0, 0)))

        elif update_anything:
            #font_debug_surface(editor.screen)
            self.force_redraw = False
            editor.present()
        elif self.can_throttle:
            editor.clock.tick(60)
        else:
            # When there is a boop instruction in the patch, we throttle for a
            # much shorter period.  This keeps the CPU usage low, but keeps the
            # UI responsive.  This probably adds at most 4 milliseconds of
            # quantization to processing mouse and touch input events.  MIDI
            # input events are handled by the mollytime backend in a separate
            # thread, and thus are not affected.
            # This is probably overkill.
            editor.clock.tick(240)
