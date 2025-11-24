
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
from .. import mollytime
from ..mollytime import OpCode, get_symbol_name
from .common import *


class pick_and_place_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = mollytime.mouse.get_pos()
        self.press_start = None

        self.prospective_tile = None
        self.grabbed_tile = None
        self.original_position = None
        self.last_valid_position = None
        self.last_hover_position = None
        self.drop_deletes = False

        self.set_screen_label(editor, "inspect > pick & place")
        self.repopulate_sidebar(editor)

        self.extra_draws = 0

        self.redraw_palette_overlay(editor)
        self.set_catalog_page(0)
        self.redraw_catalog_index(editor)

    def redraw_palette_overlay(self, editor):
        self.all_palettes = []
        self.palette_names = []
        self.palette_surfaces = []
        self.palette_rects = []

        pages = [
            (":D", [
                [OpCode.TRI, OpCode.SIN, OpCode.SQR, OpCode.MUL, OpCode.ADD, OpCode.OUT],
                [OpCode.SAW, OpCode.NOI, OpCode.MIN, OpCode.MAX, OpCode.CLAMP, OpCode.RCP],
            ]),
            ("8)", [
                [OpCode.STU, OpCode.MIX, OpCode.ADSR, OpCode.CEIL, OpCode.ROUND, OpCode.SCOPE],
                [OpCode.UTS, OpCode.INV, OpCode.BOOP, OpCode.FLOOR, OpCode.FLP, OpCode.PLS],
            ]),
            ("%)", [
                [0, .1, .25, .5, 1, 2],
                [440, -.1, -.25, -.5, -1, -2],
            ]),
            (":O", [
                [OpCode.IN, None, OpCode.MIDI_HZ, OpCode.NOTE, OpCode.GATE, None],
                [OpCode.AUX, None, OpCode.LOUD_FUDGE, OpCode.VELO, OpCode.PRES, OpCode.CTRL],
            ]),
            (":3", [
                [OpCode.POW, OpCode.SPOW, None, OpCode.FLD, OpCode.GRAD, OpCode.ABS],
                [OpCode.TAPE_LOOP, OpCode.RNG, OpCode.RSQN, OpCode.QNTZ, None, OpCode.SIGN],
            ]),
            (":y", [
                [OpCode.TPTSVF_LOWPASS, OpCode.TPTSVF_BANDPASS, OpCode.TPTSVF_HIGHPASS, OpCode.TPTSVF_NOTCH, None, OpCode.BAL],
                [OpCode.PHASE, OpCode.SIN_TRAIN, OpCode.TRI_TRAIN, OpCode.SQR_TRAIN, OpCode.SAW_TRAIN, OpCode.MOON],
            ]),
        ]

        coverage = set()

        for name, shelf in pages:
            tile_span = (editor.grid_size * 2)
            tile_stride = (editor.grid_size * 3)

            rows = len(shelf)
            columns = max([len(row) for row in shelf])

            padding = editor.grid_size

            width = (columns * 3 - 1) * editor.grid_size + padding * 2
            height = (rows * 3 - 1) * editor.grid_size + padding

            width = min(width, editor.play_area.viewport.width)
            height = min(height, editor.play_area.viewport.height)

            align_x = (editor.play_area.viewport.width - width) // 2
            align_y = (editor.play_area.viewport.height - height)

            palette_rect = mollytime.Rect(align_x, align_y, width, height)

            palette_surface = mollytime.draw.Texture((width, height))
            palette_surface.fill(editor.select_color, 0.95)

            align_x += padding // 2
            align_y += padding

            palette = {}

            y = 0
            for row in shelf:
                x = 0
                for archetile in row:
                    if archetile is not None:
                        hit_rect = mollytime.Rect(align_x + x * tile_stride, align_y + y * tile_stride, tile_span, tile_span)
                        palette[archetile] = hit_rect

                        if type(archetile) in (int, float):
                            label = f"{archetile}"
                            coverage.add(OpCode.CONST)
                        else:
                            label = get_symbol_name(archetile)
                            coverage.add(archetile)
                        draw_rect = mollytime.Rect(padding + x * tile_stride, padding + y * tile_stride, tile_span, tile_span)
                        editor.tile_bg.draw(palette_surface, draw_rect, label)
                    x += 1
                y += 1

            self.all_palettes.append(palette)
            self.palette_names.append(name)
            self.palette_surfaces.append(palette_surface)
            self.palette_rects.append(palette_rect)

        ignore = [OpCode.Count]
        for e in OpCode:
            if e not in ignore:
                assert e in coverage, f"pick and place screen does not expose OpCode.{e.name}!"

    def set_catalog_page(self, page):
        if page > -1:
            page = page % len(self.palette_names)
            self.tile_palette = self.all_palettes[page]
            self.palette_name = self.palette_names[page]
            self.palette_surface = self.palette_surfaces[page]
            self.palette_rect = self.palette_rects[page]
            self.current_palette = page
        else:
            self.tile_palette = None
            self.palette_name = None
            self.palette_surface = None
            self.palette_rect = None
            self.current_palette = -1

    def redraw_catalog_index(self, editor):
        columns = min(editor.play_rect.width // (editor.grid_size * 3), len(self.palette_names))
        rows = math.ceil(len(self.palette_names) / columns)
        index_w = columns * editor.grid_size * 3 + editor.grid_size
        index_h = rows * editor.grid_size * 3

        align_x = (editor.play_rect.width - index_w) // 2
        align_y = editor.grid_size // 2
        index_w = editor.play_rect.width

        self.catalog_index_rect = mollytime.Rect(
            0, 0, index_w, index_h)
        self.catalog_index_surface = mollytime.draw.Texture((index_w, index_h))
        self.catalog_index_surface.fill(editor.select_color, 0.95)

        align_x += editor.grid_size

        self.catalog_index_targets = []
        for index, name in enumerate(self.palette_names):
            x = index % columns
            y = index // columns
            rect = mollytime.Rect(
                x * editor.grid_size * 3 + align_x,
                y * editor.grid_size * 3 + align_y,
                editor.grid_size * 2, editor.grid_size * 2)

            if index == self.current_palette:
                editor.tile_bg.draw(self.catalog_index_surface, rect, name)
                draw_outline(self.catalog_index_surface, rect, parse_color("#fff000"), 8)
            else:
                editor.tile_bg.draw(self.catalog_index_surface, rect, name)
            self.catalog_index_targets.append((rect, name))

    def request_extra_draws(self):
        # This is used to request a full redraw some number of frames after dropping a tile.
        # The interval of 64 frames was chosen arbitrarily.  The number of frames needed to
        # flush the stale frame seems to vary significantly on Windows, though in most of the
        # time a low number greater than one is sufficient.
        self.extra_draws = 64

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

        active_icon = editor.move_active

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None)]

    def goto_inspect_screen(self, editor):
        self.live = False

    def on_move(self, editor, pos, event):
        self.cursor_pos = pos

        if self.press_start:
            move_x = pos[0] - self.press_start[0]
            move_y = pos[1] - self.press_start[1]

            if move_x != 0 or move_y != 0:
                editor.focus_x -= move_x
                editor.focus_y -= move_y
                self.update_play_area = True

            self.press_start = pos

        elif self.grabbed_tile or self.prospective_tile is not None:
            self.force_redraw = True
            self.drop_deletes = editor.side_bar_rect.collidepoint(pos) or self.palette_rect.collidepoint(pos)

            hover_xy = editor.cursor_to_grid(pos)
            hover_rect = editor.get_grid_rect(hover_xy)
            if hover_rect.collidepoint(pos):
                self.last_hover_position = hover_xy

                collision = False
                for other_tile_id, other_tile_xy in editor.tile_positions.items():
                    if other_tile_id != self.grabbed_tile and other_tile_xy == hover_xy:
                        collision = True
                        break
                if not collision:
                    self.last_valid_position = hover_xy

    def on_press(self, editor, pos, event):
        self.cursor_pos = pos

        if self.catalog_index_rect.collidepoint(pos):
            for rect, name in self.catalog_index_targets:
                if rect.collidepoint(pos):
                    index = self.palette_names.index(name)
                    assert(index > -1)
                    self.set_catalog_page(-1 if self.current_palette == index else index)
                    self.redraw_catalog_index(editor)
                    self.update_play_area = True
            return

        if self.current_palette > -1 and self.palette_rect.collidepoint(pos):
            for archetile, rect in self.tile_palette.items():
                if rect.collidepoint(pos):
                    assert(self.prospective_tile is None)
                    assert(self.grabbed_tile is None)
                    self.prospective_tile = archetile

        elif editor.play_rect.collidepoint(pos):
            something_happened = False
            for tile_id, (tile_x, tile_y) in editor.tile_positions.items():
                rect = mollytime.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)
                if rect.collidepoint(pos):
                    assert(self.prospective_tile is None)
                    assert(self.grabbed_tile is None)
                    self.grabbed_tile = tile_id
                    self.original_position = (tile_x, tile_y)
                    self.last_valid_position = (tile_x, tile_y)
                    self.last_hover_position = (tile_x, tile_y)
                    something_happened = True
                    self.force_redraw = True
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
        if self.prospective_tile is not None:
            if self.last_valid_position and not self.drop_deletes:
                if type(self.prospective_tile) in (int, float):
                    editor.make_constant(self.last_valid_position, self.prospective_tile)
                else:
                    editor.make_tile(self.last_valid_position, self.prospective_tile)
            self.force_redraw = True
            self.prospective_tile = None
            self.last_valid_position = None
            self.last_hover_position = None
            self.drop_deletes = False
            self.rotate_quick_cons = False

        elif self.grabbed_tile:
            if self.drop_deletes:
                editor.erase_tile(self.grabbed_tile)
            else:
                editor.tile_positions[self.grabbed_tile] = self.last_valid_position
            self.force_redraw = True
            self.grabbed_tile = None
            self.last_valid_position = None
            self.last_hover_position = None
            self.drop_deletes = False

        self.press_start = None

    def draw(self, editor):
        update_anything = False
        if self.extra_draws > 0:
            self.extra_draws -= 1
            self.force_redraw = True

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

                if tile_id == self.grabbed_tile:
                    editor.initial_placement.draw(frame, rect, label)
                else:
                    editor.tile_bg.draw(frame, rect, label)

            if self.grabbed_tile and not self.drop_deletes:
                label = editor.patch.get_tile_label(self.grabbed_tile)
                rect = editor.get_grid_rect(self.last_valid_position)
                editor.valid_placement.draw(frame, rect, label)

            elif self.prospective_tile is not None and self.last_valid_position and not self.drop_deletes:
                if type(self.prospective_tile) in (int, float):
                    label = f"{self.prospective_tile}"
                else:
                    label = get_symbol_name(self.prospective_tile)
                rect = editor.get_grid_rect(self.last_valid_position)
                editor.valid_placement.draw(frame, rect, label)

            for (out_port, in_port) in editor.patch.wires:
                lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
                rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
                radius = max(1, editor.grid_size // 12)
                draw_arrow(frame, (0, 0, 0), lhs_rect, rhs_rect, radius)

            if self.current_palette > -1:
                frame.blit(self.palette_surface, self.palette_rect)

            frame.blit(self.catalog_index_surface, self.catalog_index_rect)

        # draw sidebar
        if self.update_sidebar or self.force_redraw:
            self.update_sidebar = False
            update_anything = True

            frame = editor.reset_side_bar()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            self.draw_system_status(editor, frame)

        drag_and_draw = None
        if self.grabbed_tile or (self.prospective_tile is not None and self.last_hover_position):
            overlay_span = (editor.grid_size * 4)
            overlay = mollytime.draw.Texture((overlay_span, overlay_span))
            overlay.set_blend_mode(mollytime.draw.premultiplied_alpha)
            overlay.fill((0, 0, 0), 0)

            rect = mollytime.Rect(0, 0, editor.grid_size * 4, editor.grid_size * 4)
            rect.center = [round(i) for i in self.cursor_pos]
            drag_and_draw = (overlay, rect)
            self.request_extra_draws()

            rect = mollytime.Rect(editor.grid_size, editor.grid_size, editor.grid_size * 2, editor.grid_size * 2)

            if self.prospective_tile is not None:
                if type(self.prospective_tile) in (int, float):
                    label = f"{self.prospective_tile}"
                else:
                    label = get_symbol_name(self.prospective_tile)
            else:
                label = editor.patch.get_tile_label(self.grabbed_tile)

            if self.drop_deletes:
                if self.prospective_tile is not None:
                    editor.tile_bg.draw(overlay, rect, "drop\nto\ncancel")
                else:
                    symbol = editor.patch.get_tile_symbol(self.grabbed_tile)
                    if symbol == OpCode.CONST and editor.patch.get_constant(self.grabbed_tile) == 1337:
                        editor.tile_bg.draw(overlay, rect, "DROP\n&\nRUN")
                    else:
                        editor.tile_bg.draw(overlay, rect, "drop\nto\ndelete")
            elif self.last_valid_position == self.last_hover_position:
                editor.tile_bg.draw(overlay, rect, label)
            else:
                editor.invalid_placement.draw(overlay, rect, label)

        if update_anything or drag_and_draw:
            self.force_redraw = False
            editor.present(drag_and_draw)
        else:
            editor.clock.tick(60)
