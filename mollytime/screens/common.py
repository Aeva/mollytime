
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

import gc
import time
import random
from xml.etree import ElementTree


from ..fonts import *
from ..colors import *
from ..patterns import *
from ..dpi import calculate_dpi
from ..perf import profile_function
from ..power import poll_battery

from .. import backend
from ..backend import Patch, OpCode, decode_port_tile, decode_port_index, get_temporal_pressure


battery_level = None
temporal_pressure = 0.0
temporal_pressure_precent = ""


class program_card:
    def __init__(self, vertical_inches_override):
        self.vertical_inches_override = vertical_inches_override

        self.focus_x = 0
        self.focus_y = 0

        self.patch = Patch()
        self.tile_positions = {}

        def quick_connect(lhs, rhs):
            wire = self.patch.get_implicit_wire(lhs, rhs)
            assert(wire)
            self.patch.connect_tiles(*wire)

        if False:
            #   2    *
            #  440  sin   +
            #       sin  out
            two = self.make_constant((-1, -1), 2)
            a4_hz = self.make_constant((-1, 0), 440)
            a5_hz = self.make_tile((0, -1), OpCode.MUL)
            a5_osc = self.make_tile((0, 0), OpCode.SIN)
            a4_osc = self.make_tile((-1, 1), OpCode.SQR)
            summed = self.make_tile((0, 1), OpCode.ADD)
            half = self.make_constant((1, -1), .5)
            gain = self.make_tile((1, 0), OpCode.MUL)
            out = self.make_tile((1, 1), OpCode.OUT)

            quick_connect(two, a5_hz)
            quick_connect(a4_hz, a5_hz)
            quick_connect(a5_hz, a5_osc)
            quick_connect(a4_hz, a4_osc)
            quick_connect(a4_osc, summed)
            quick_connect(a5_osc, summed)
            quick_connect(half, gain)
            quick_connect(summed, gain)
            quick_connect(gain, out)

        elif False:
            summed = self.make_tile((0, 0), OpCode.ADD)

            ambition = 1000
            half = ambition // 2

            for i in range(ambition):
                alpha = i / (ambition - 1)
                inv_a = 1 - alpha
                hz = 440 # inv_a * 440 + alpha * 440.5
                hz = self.make_constant((-2, i - half), hz)
                osc = self.make_tile((-1, i - half), OpCode.SIN)
                quick_connect(hz, osc)
                quick_connect(osc, summed)

            scale_by = self.make_constant((1, -1), (1.0 / ambition) * .5)
            scale = self.make_tile((1, 0), OpCode.MUL)
            out = self.make_tile((2, 0), OpCode.OUT)
            quick_connect(summed, scale)
            quick_connect(scale_by, scale)
            quick_connect(scale, out)

        self.selected = []

        self.clock = backend.time.Clock()

        self.dpi = None
        self.resize()

    def find_center_of_mass(self, quantized=False):
        center_of_mass = (0, 0)
        count = 0
        for pos in self.tile_positions.values():
            center_of_mass = vec_add(center_of_mass, pos)
            count += 1
        if count > 0:
            center_of_mass = vec_scale(center_of_mass, 1/count)
            if quantized:
                center_of_mass = tuple([int(i) for i in center_of_mass])
        return center_of_mass

    def recenter_bounding_box_method(self):
        positions = list(self.tile_positions.values())
        if len(positions) < 2:
            return False
        low = positions[0]
        high = positions[1]
        for pos in positions[1:]:
            low = [min(low[c], pos[c]) for c in range(2)]
            high = [max(high[c], pos[c]) for c in range(2)]
        w = (high[0] - low[0]) * self.grid_size * 3
        h = (high[1] - low[1]) * self.grid_size * 3

        if w > self.play_area.viewport.w or h > self.play_area.viewport.h:
            return False

        mid = vec_add(low, vec_scale(vec_sub(high, low), .5))
        self.focus_x = mid[0] * self.grid_size * 3
        self.focus_y = mid[1] * self.grid_size * 3
        return True

    def recenter_center_of_mass_method(self):
        center_of_mass = self.find_center_of_mass()
        self.focus_x, self.focus_y = vec_scale(center_of_mass, self.grid_size * 3)

    def recenter(self):
        if self.recenter_bounding_box_method():
            return
        else:
            self.recenter_center_of_mass_method()

    def save_patch(self, save_path):
        tiles = sorted(self.patch.get_all_tile_handles())
        entries = []
        entries.append('\t<patch>\n')
        indent = '\t' * 2

        center_of_mass = self.find_center_of_mass(quantized=True)

        for tile_id in tiles:
            symbol = self.patch.get_tile_symbol(tile_id)
            x, y = vec_sub(self.tile_positions.get(tile_id, (0, 0)), center_of_mass)
            params = {
                "id" : tile_id,
                "symbol" : symbol.name.lower(),
                "x" : x,
                "y" : y,
            }
            if symbol == OpCode.CONST:
                params["value"] = self.patch.get_constant(tile_id)
            elif symbol == OpCode.TWEAK:
                params["value"] = self.patch.get_special_input(tile_id)

            entry = " ".join([f'{key}="{value}"' for key, value in params.items()])
            entries.append(f'{indent}<tile {entry}/>\n')

        for (out_port, in_port) in self.patch.wires:
            out_tile = decode_port_tile(out_port)
            out_index = decode_port_index(out_port)
            out_key = f'{out_tile}:{out_index}'
            in_tile = decode_port_tile(in_port)
            in_index = decode_port_index(in_port)
            in_key = f'{in_tile}:{in_index}'
            entries.append(f'{indent}<wire from="{out_key}" to="{in_key}"/>\n')

        entries.append('\t</patch>\n')
        entries = "".join(entries)
        with open(save_path, 'w') as outfile:
            header = '<?xml version="1.0"?>\n<mollytime version="2025.07.13">\n'
            footer = '</mollytime>\n'
            outfile.write(f'{header}{entries}{footer}')

        print(f"saved: {save_path}")

    def loader_20250713(self, tree, root):
        self.patch = Patch()
        self.tile_positions = {}

        opcode_lookup = {}
        for index in range(OpCode.Count):
            symbol = OpCode(index)
            key = symbol.name.lower()
            opcode_lookup[key] = symbol

        visited_patch = False
        for root_child in root:
            if root_child.tag == "patch" and not visited_patch:
                visited_patch = True
                next_index = 1
                rewrite = {}

                midi_lanes = root_child.attrib.get("midi_lanes", None)
                if midi_lanes is not None:
                    self.patch.midi_lanes = max(int(midi_lanes), 1)

                for patch_child in root_child:
                    if patch_child.tag == "tile":
                        old_id = int(patch_child.attrib["id"])
                        x = int(patch_child.attrib["x"])
                        y = int(patch_child.attrib["y"])
                        symbol = opcode_lookup[patch_child.attrib["symbol"]]
                        assert(old_id not in rewrite)
                        if symbol == OpCode.CONST:
                            value = float(patch_child.attrib["value"])
                            rewrite[old_id] = self.make_constant((x, y), value)
                        else:
                            tile_id = self.make_tile((x, y), symbol)
                            rewrite[old_id] = tile_id
                            if symbol == OpCode.TWEAK:
                                value = float(patch_child.attrib["value"])
                                self.patch.set_special_input(tile_id, value)

                for patch_child in root_child:
                    if patch_child.tag == "wire":
                        out_key = [int(i) for i in patch_child.attrib["from"].split(":")]
                        out_tile = rewrite[out_key[0]]
                        out_index = out_key[1]
                        out_port = backend.make_port_handle(out_tile, out_index)
                        in_key = [int(i) for i in patch_child.attrib["to"].split(":")]
                        in_tile = rewrite[in_key[0]]
                        in_index = in_key[1]
                        in_port = backend.make_port_handle(in_tile, in_index)
                        try:
                            self.patch.connect_tiles(out_port, in_port)
                        except:
                            in_tile_name = self.patch.get_tile_name(in_tile).replace("\n", " ")
                            out_tile_name = self.patch.get_tile_name(out_tile).replace("\n", " ")
                            old_out_port = patch_child.attrib["from"]
                            old_in_port = patch_child.attrib["to"]
                            print(f"Unable to connect {old_out_port} to {old_in_port}!")
                            print(f" - translated to {out_tile}:{out_index} -> {in_tile}:{in_index} aka {out_port} -> {in_port}")
                            print(f" - tile {out_tile} is a {in_tile_name} ({self.patch.get_tile_symbol(out_tile)})")
                            for name in self.patch.get_tile_output_ports(out_tile):
                                print(f"   - {name} --->")
                            print(f" - tile {in_tile} is a {out_tile_name} ({self.patch.get_tile_symbol(in_tile)})")
                            for name in self.patch.get_tile_input_ports(out_tile):
                                print(f"   ---> {name}")
                            raise
        self.recenter()


    def load_patch(self, load_path):
        try:
            tree = ElementTree.parse(load_path)
        except:
            print(f'Unable to open file "{load_path}", because it is not a supported file type or it is malformed.')
            return

        root = tree.getroot()
        if root.tag == "mollytime":
            if root.attrib.get("version") is None:
                print(f'Unable to open mollytime patch file "{load_path}", because the version string is missing!')
            elif root.attrib["version"] == "2025.07.13":
                self.loader_20250713(tree, root)
            else:
                print(f'Unable to open mollytime patch file "{load_path}", because the version is unknown or invalid!')
        else:
            print(f'Unable to open file "{load_path}", because it is not a supported file type.')
        gc.collect()
        print(f"loaded: {load_path}")

    def make_tile(self, position, symbol):
        tile_id = self.patch.make_tile(symbol);
        self.tile_positions[tile_id] = position
        return tile_id

    def make_constant(self, position, value):
        tile_id = self.patch.make_constant(value);
        self.tile_positions[tile_id] = position
        return tile_id

    def erase_tile(self, tile_id):
        self.patch.erase_tile(tile_id)
        del self.tile_positions[tile_id]

    def get_grid_rect(self, tile_xy):
        frame_x = self.play_rect.centerx - self.focus_x - self.grid_size + tile_xy[0] * self.grid_size * 3
        frame_y = self.play_rect.centery - self.focus_y - self.grid_size + tile_xy[1] * self.grid_size * 3
        return backend.Rect((frame_x, frame_y), (self.grid_size * 2, self.grid_size * 2))

    def get_tile_rect(self, tile_id):
        tile_xy = self.tile_positions[tile_id]
        return self.get_grid_rect(tile_xy)

    def cursor_to_grid(self, pos):
        # center-relative cursor position
        rel = vec_sub(pos, self.play_rect.center)
        # grid-relative cursor position, in pixels
        rel = vec_add(vec_add(rel, (self.focus_x, self.focus_y)), (self.grid_size * 1.5, self.grid_size * 1.5))
        # grid-quantized position
        tile_size = self.grid_size * 3
        return (int(rel[0] // tile_size), int(rel[1] // tile_size))

    def freeze(self):
        return self.patch.freeze()

    def unfreeze(self):
        return self.patch.unfreeze()

    def is_frozen(self):
        return self.patch.get_frozen()

    def toggle_connection(self, out_key, in_key):
        self.patch.toggle_connection(out_key, in_key)

    def toggle_selection(self, tile_id):
        if tile_id in self.selected:
            # deselect the tile
            self.selected = [select for select in self.selected if select != tile_id]
        else:
            # deselect incompatible selected tiles
            def can_connect(other_id):
                return self.patch.can_connect(tile_id, other_id) or self.patch.can_connect(other_id, tile_id)
            self.selected = list(filter(can_connect, self.selected))

            # select the new tile
            if len(self.selected) < 2:
                self.selected.append(tile_id)
            else:
                assert(len(self.selected) == 2)
                self.selected = [self.selected[1], tile_id]

    def clear_selection(self):
        self.selected = []
        self.force_redraw = True

    def reverse_selection(self):
        self.selected = self.selected[::-1]
        self.force_redraw = True

    def is_selected(self, tile):
        return tile in self.selected

    def any_selected(self):
        return len(self.selected) != 0

    def implicit_wire_from_selection(self):
        """
        If the selection has one obvious possible connection assuming the first
        chronological selection is the output and the second is the input, then
        this function returns the implied wire tuple.
        """
        if len(self.selected) == 2:
            return self.patch.get_implicit_wire(*self.selected)
        return None

    def connectable_selection(self):
        inputs = 0
        outputs = 0
        for tile_id in self.selected:
            inputs += len(self.patch.get_tile_input_ports(tile_id))
            outputs += len(self.patch.get_tile_output_ports(tile_id))
        return inputs > 0 and outputs > 0

    def lhs_selection(self):
        if len(self.selected) >= 1:
            return self.selected[0]
        else:
            return None

    def rhs_selection(self):
        if len(self.selected) >= 2:
            return self.selected[1]
        else:
            return None

    def reset_play_area(self):
        self._play_area_surface = self.play_area.surface.copy()
        return self._play_area_surface

    def reset_settings_area(self):
        self._play_area_surface = self.settings_area.surface.copy()
        return self._play_area_surface

    def replace_play_area(self, replacement):
        self._play_area_surface = replacement
        return self._play_area_surface

    def reset_side_bar(self):
        self._side_bar_surface = self.side_bar.surface.copy()
        return self._side_bar_surface

    def draw_touch_points(self):
        radius = self.grid_size / 2
        for key, pos in editor_screen.touch_points.items():
            color = editor_screen.touch_colors[key]
            backend.draw.circle(self.screen, color, pos, radius)

    def present(self, overlay = None):
        self.screen.blit(self._play_area_surface, self.play_area.viewport)
        self.screen.blit(self._side_bar_surface, self.side_bar.viewport)
        if overlay:
            self.screen.blit(*overlay)
        self.draw_touch_points()
        backend.draw.flip()

    def resize(self):
        self.screen = backend.draw.get_rendering_surface()
        self.dpi = dpi = calculate_dpi(self.vertical_inches_override)

        screen_rect = self.screen.get_rect()
        screen_w = screen_rect.width
        screen_h = screen_rect.height
        assert(screen_w > 1)
        assert(screen_h > 1)

        self.grid_size = dpi // 3

        side_bar_w = self.grid_size * 3
        side_bar_h = screen_h

        self.heavy_line = max(dpi // 32, 2)

        self.play_rect = backend.Rect(0, 0, screen_w - side_bar_w, screen_h)
        self.play_area = tile_grid_bg(self.play_rect, self.grid_size)
        self._play_area_surface = self.play_area.surface.copy()

        self.settings_area = settings_grid_bg(self.play_rect, self.grid_size)
        self._settings_area = self.settings_area.surface.copy()

        self.side_bar_rect = backend.Rect(screen_w - side_bar_w, 0, side_bar_w, side_bar_h)
        self.side_bar = side_bar_bg(self.side_bar_rect, self.grid_size)
        self._side_bar_surface = self.side_bar.surface.copy()

        self.tile_color = parse_color("#dee5e8")
        self.tile_bg = plate_bg(self.grid_size, self.tile_color)
        self.dark_tile_bg = plate_bg(self.grid_size, parse_color("#211a17"))
        self.clip_tile = plate_bg(self.grid_size, parse_color("#F48"))
        self.poly_tile_bg = plate_bg(self.grid_size, parse_color("#be9ad3"))
        self.const_tile_bg = plate_bg(self.grid_size, parse_color("#d7e6d2"))
        self.disconnected_tile_bg = plate_bg(self.grid_size, parse_color("#d1e7f0"))

        self.initial_placement = plate_outline(self.grid_size, parse_color("#888"))
        self.valid_placement = plate_outline(self.grid_size, parse_color("#080"))
        self.invalid_placement = plate_outline(self.grid_size, parse_color("#800"), True)

        self.select_color = lch_swizzle(self.tile_color, parse_color("#880000"), (.5, .75, 0))
        self.selected_tile_bg = plate_bg(self.grid_size, self.select_color)

        self.select_color2 = lch_swizzle(self.tile_color, parse_color("#880000"), (.5, .75, .3))
        self.selected_tile_bg2 = plate_bg(self.grid_size, self.select_color2)

        lch = list(self.select_color.encode(ColorSpace.OkLCH).channels)
        lch[0] *= 0.25
        lch[1] *= 0.5
        self.scope_bg_color = backend.oklch(*lch)

        self.scope_tile_highlight = plate_outline(self.grid_size, parse_color("#211a17"))

        self.heat_color = lch_swizzle(self.tile_color, parse_color("#880000"), (.5, .75, 1))

        self.inspect_target = plate_bg(self.grid_size, self.tile_color, "inspect")
        self.inspect_active = plate_bg(self.grid_size, self.select_color, "inspect")

        self.select_target = plate_bg(self.grid_size, self.tile_color, "select")
        self.select_active = plate_bg(self.grid_size, self.select_color, "select")

        self.freeze_patch = plate_bg(self.grid_size, self.tile_color, "freeze\npatch")
        self.unfreeze_patch = plate_bg(self.grid_size, self.heat_color, "thaw\npatch")

        self.move_target = plate_bg(self.grid_size, self.tile_color, "pick\n&\nplace")
        self.move_active = plate_bg(self.grid_size, self.select_color, "pick\n&\nplace")

        self.save_target = plate_bg(self.grid_size, self.tile_color, "save\npatch")
        self.save_active = plate_bg(self.grid_size, self.select_color, "save\npatch")

        self.load_target = plate_bg(self.grid_size, self.tile_color, "load\npatch")
        self.load_active = plate_bg(self.grid_size, self.select_color, "load\npatch")

        self.settings_target = plate_bg(self.grid_size, self.tile_color, "config")
        self.settings_active = plate_bg(self.grid_size, self.select_color, "config")

        self.midi_target = plate_bg(self.grid_size, self.tile_color, "midi")
        self.midi_active = plate_bg(self.grid_size, self.select_color, "midi")

        self.misc_target = plate_bg(self.grid_size, self.tile_color, "misc")
        self.misc_active = plate_bg(self.grid_size, self.select_color, "misc")

        self.calc_target = plate_bg(self.grid_size, self.tile_color, "calc")
        self.calc_active = plate_bg(self.grid_size, self.select_color, "calc")

        self.scope_target = plate_bg(self.grid_size, self.tile_color, "scope")
        self.scope_active = plate_bg(self.grid_size, self.select_color, "scope")

        self.clear_selection_target = plate_bg(self.grid_size, self.tile_color, "deselect")
        self.swap_sides_target = plate_bg(self.grid_size, self.tile_color, "swap\nsides")

        self.auto_connect = plate_bg(self.grid_size, self.tile_color, "connect")
        self.auto_disconnect = plate_bg(self.grid_size, self.tile_color, "detach")

        self.apply_target = plate_bg(self.grid_size, self.tile_color, "apply")

        self.cancel_target = plate_bg(self.grid_size, self.tile_color, "cancel")

        self.placeholder_target = plate_bg(self.grid_size, self.tile_color, "magic")

class editor_screen:
    touch_points = {}
    touch_colors = {}

    def __init__(self, editor):
        self.last_status_check = 0
        self.last_clip_check = 0
        self.last_clip = 0
        self.draw_clip = False
        self.update_play_area = True
        self.update_sidebar = True
        self.force_redraw = True
        self.last_time_update = 0
        self.current_time = None
        self.day_progress = 0
        self.up_late = False

        self.setup(editor)

        self.live = True
        self.purge_events()
        self.draw(editor)

        while self.live:
            self.process_events(editor)

            now = time.time()
            delta = now - self.last_clip_check
            if delta > 0.016:
                self.last_clip_check = now
                min_sample, max_sample = editor.patch.read_output_probe()
                output_probe = max(abs(min_sample), abs(max_sample))
                is_clipping = output_probe > 1.0
                was_clipping = (now - self.last_clip) < .5
                if is_clipping:
                    self.last_clip = now
                if not self.draw_clip and was_clipping:
                    self.draw_clip = True
                    self.update_play_area = True
                elif self.draw_clip and not was_clipping:
                    self.draw_clip = False
                    self.update_play_area = True

            delta = now - self.last_status_check
            if delta > 1.0:
                self.last_status_check = now
                self.perf_check()
                self.battery_check()

            delta = now - self.last_time_update
            if delta > 1:
                self.last_time_update = now

                hours_part = str(int(time.strftime("%I"))) # lose the trailing zero
                minutes_part = time.strftime("%M")
                meridiem_part = time.strftime("%p")
                current_time = f"{hours_part}:{minutes_part} {meridiem_part}"

                self.day_progress = (float(minutes_part) / 60.0 + float(time.strftime("%H")))

                # this only works if the times are in the same day
                up_late_start = 1.0 # 1 am
                up_late_end = 8.0   # 8 am

                if not self.up_late and self.day_progress >= up_late_start and self.day_progress < up_late_end:
                    self.up_late = True
                    self.update_sidebar = True

                if self.up_late and self.day_progress >= up_late_end:
                    self.up_late = False
                    self.update_sidebar = True

                if self.current_time != current_time or self.up_late:
                    self.current_time = current_time
                    self.update_sidebar = True

            self.draw(editor)

    def toggle_fullscreen(self, editor):
        backend.display.toggle_fullscreen()

    def handle_resize_event(self, editor, event):
        self.reset_touch_tracker()
        self.force_redraw = True
        editor.resize()
        self.resize_screen(editor)

    def resize_screen(self, editor):
        pass

    def on_scroll(self, editor, event):
        pass

    def touch_start(self, editor, key, pos, event):
        self.force_redraw = True
        editor_screen.touch_points[key] = pos
        editor_screen.touch_colors[key] = oklch(0.5, 0.15, random.randint(0, 360))

    def touch_update(self, editor, key, pos, event):
        if key in editor_screen.touch_points:
            self.force_redraw = True
            editor_screen.touch_points[key] = pos

    def touch_end(self, editor, key, pos, event):
        if key in editor_screen.touch_points:
            self.force_redraw = True
            del editor_screen.touch_points[key]
            del editor_screen.touch_colors[key]

    def reset_touch_tracker(self):
        self.force_redraw = True
        self.primary_touch = None
        editor_screen.touch_points = {}
        editor_screen.touch_colors = {}

    def perf_check(self):
        global temporal_pressure, temporal_pressure_precent
        new_value = get_temporal_pressure()
        if new_value <= 0:
            # The current temporal pressure reading is invalid.
            self.update_sidebar = bool(temporal_pressure_precent)
            temporal_pressure = 0
            temporal_pressure_precent = ""
        else:
            # Lerp between floor and ceiling temporal pressure percents using the temporal
            # pressure itself as the alpha value.  This is on the theory that the lower bound
            # is under the error margin, and thus taking the floor gives less noisy results.
            # This has a soothing effect to discourage unecessary optimization when a patch is
            # already very inexpensive, and a motivating effect when it is not.
            alpha = min(max(new_value, 0), 1)
            inv_a = 1.0 - alpha
            percent = new_value * 100
            percent = int(inv_a * math.floor(percent) + alpha * math.ceil(percent))
            new_label = f"{percent}%"
            if new_label != temporal_pressure_precent:
                temporal_pressure = new_value
                temporal_pressure_precent = new_label
                self.update_sidebar = True

    def battery_check(self):
        global battery_level
        new_battery_level = poll_battery()
        if new_battery_level != battery_level:
            battery_level = new_battery_level
            self.update_sidebar = True

    def draw_system_status(self, editor, frame):
        global battery_level
        view_rect = frame.get_rect()
        font_path, size = NATIONAL_PARK_REGULAR, max(10, editor.grid_size * 2 * .24)
        line_offset_by = size
        line_offset = 0

        def draw_label(text, fg_color=(255, 255, 255), bg_color=(0, 0, 0)):
            nonlocal line_offset
            label = render_text(font_path, size, fg_color, text)
            border = render_text(font_path, size, bg_color, text)
            label_rect = label.get_rect()
            for y in (-2, -1, 1, 2):
                for x in (-2, -1, 1, 2):
                    dest = (
                        view_rect.centerx - label_rect.centerx + x,
                        view_rect.bottom - label_rect.height + y - line_offset)
                    frame.blit(border, dest)
            dest = (
                view_rect.centerx - label_rect.centerx,
                view_rect.bottom - label_rect.height - line_offset)
            frame.blit(label, dest)
            line_offset += line_offset_by

        if temporal_pressure > 0.0:
            alpha = min(max(temporal_pressure, 0), 1)
            if alpha < .5:
                color = parse_color("#FFF")
            else:
                alpha = alpha * 2 - 1
                ramp = color_ramp(parse_color("#FFF"), parse_color("#FF0"), parse_color("#F00"))
                color = ramp.sample(alpha)

            text = f"LOAD: {temporal_pressure_precent}"
            draw_label(text, color)
        else:
            draw_label("LOAD: ???")

        if battery_level:
            if battery_level < 20:
                ramp = color_ramp(parse_color("#F00"), parse_color("#FF0"))
                color = ramp.sample(float(max(battery_level - 10, 0)) / 10.0)
            else:
                ramp = color_ramp(parse_color("#FF0"), parse_color("#0C0"))
                color = ramp.sample(min(float(battery_level - 20) / 79.0, 1.0))
            text = f"BAT: {battery_level}%"
            draw_label(text, color)

        if self.current_time:
            if self.up_late:
                if int(time.time()) % 2 == 0:
                    color = parse_color("#F00")
                else:
                    color = parse_color("#800")
            else:
                if self.day_progress >= 12.0 + 5.0: # 5 pm
                    # current approximate time of dusk
                    color = parse_color("#66A")
                else:
                    # day light, probably!
                    color = parse_color("#88F")

            draw_label(f"{self.current_time}", color)

    def purge_events(self):
        self.reset_touch_tracker()
        for event in backend.events.get():
            if (event.type == backend.events.KEYDOWN and event.key == backend.events.K_ESCAPE):
                self.live = False
            elif event.type == backend.events.QUIT:
                exit(0)

    def handle_escape(self, editor):
        self.purge_events()
        self.live = False

    @profile_function("process_events")
    def process_events(self, editor):
        for event in backend.events.get():
            if event.type == backend.events.KEYDOWN and event.key.key == backend.events.K_ESCAPE:
               self.handle_escape(editor)

            elif event.type == backend.events.KEYDOWN and event.key.key in (backend.events.K_F, backend.events.K_F11):
               self.toggle_fullscreen(editor)

            elif event.type == backend.events.WINDOWRESIZE or event.type == backend.events.PIXELSIZECHANGED:
                self.handle_resize_event(editor, event)

            elif event.type == backend.events.MOUSEMOTION and (abs(event.motion.rel[0]) > 0 or abs(event.motion.rel[1]) > 0):
                self.on_move(editor, event.motion.pos, event)

            elif event.type == backend.events.MOUSEBUTTONDOWN and event.button.button == backend.events.BUTTON_LEFT:
                self.on_press(editor, event.button.pos, event)

            elif event.type == backend.events.MOUSEBUTTONUP and event.button.button == backend.events.BUTTON_LEFT:
                self.on_release(editor, event.button.pos, event)

            elif event.type == backend.events.MOUSEWHEEL:
                self.on_scroll(editor, event.wheel)

            elif event.type == backend.events.FINGERMOTION:
                key = (event.tfinger.touch_id, event.tfinger.finger_id)
                pos = (event.tfinger.x * editor.screen.get_width(), event.tfinger.y * editor.screen.get_height())
                self.touch_update(editor, key, pos, event)
                if key == self.primary_touch:
                    self.on_move(editor, pos, event)

            elif event.type == backend.events.FINGERDOWN:
                key = (event.tfinger.touch_id, event.tfinger.finger_id)
                pos = (event.tfinger.x * editor.screen.get_width(), event.tfinger.y * editor.screen.get_height())
                self.touch_start(editor, key, pos, event)
                if self.primary_touch is None:
                    self.primary_touch = key
                    self.on_press(editor, pos, event)


            elif event.type == backend.events.FINGERUP:
                key = (event.tfinger.touch_id, event.tfinger.finger_id)
                pos = (event.tfinger.x * editor.screen.get_width(), event.tfinger.y * editor.screen.get_height())
                self.touch_end(editor, key, pos, event)
                if key == self.primary_touch:
                    self.on_release(editor, pos, event)
                    self.primary_touch = None

            elif event.type == backend.events.QUIT:
                sys.exit(0)

    def draw(self, editor):
        pass
