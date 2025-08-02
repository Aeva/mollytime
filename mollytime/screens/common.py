
import time
import random
from xml.etree import ElementTree

import pygame_setup
import pygame

from fonts import *
from colors import *
from patterns import *
from perf import profile_function
from power import poll_battery

from mollytime import Patch, OpCode, decode_port_tile, decode_port_index, get_temporal_pressure


battery_level = None
temporal_pressure = 0.0
temporal_pressure_precent = ""


class program_card:
    def __init__(self, screen, dpi):
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

        self.clock = pygame.time.Clock()
        self.resize(screen, dpi)

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
                            rewrite[old_id] = self.make_tile((x, y), symbol)

                for patch_child in root_child:
                    if patch_child.tag == "wire":
                        out_key = [int(i) for i in patch_child.attrib["from"].split(":")]
                        out_tile = rewrite[out_key[0]]
                        out_index = out_key[1]
                        out_port = mollytime.make_port_handle(out_tile, out_index)
                        in_key = [int(i) for i in patch_child.attrib["to"].split(":")]
                        in_tile = rewrite[in_key[0]]
                        in_index = in_key[1]
                        in_port = mollytime.make_port_handle(in_tile, in_index)
                        try:
                            self.patch.connect_tiles(out_port, in_port)
                        except:
                            old_out_port = patch_child.attrib["from"]
                            old_in_port = patch_child.attrib["to"]
                            print(f"Unable to connect {old_out_port} to {old_in_port}!")
                            print(f" - translated to {out_tile}:{out_index} -> {in_tile}:{in_index} aka {out_port} -> {in_port}")
                            print(f" - tile {out_tile} is a {self.patch.get_tile_symbol(out_tile)}")
                            for name in self.patch.get_tile_output_ports(out_tile):
                                print(f"   - {name} --->")
                            print(f" - tile {in_tile} is a {self.patch.get_tile_symbol(in_tile)}")
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
                return self.loader_20250713(tree, root)
            else:
                print(f'Unable to open mollytime patch file "{load_path}", because the version is unknown or invalid!')
        else:
            print(f'Unable to open file "{load_path}", because it is not a supported file type.')

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
        return pygame.Rect((frame_x, frame_y), (self.grid_size * 2, self.grid_size * 2))

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

    def resize(self, screen, dpi):
        self.screen = screen
        self.dpi = dpi

        screen_w = screen.get_rect().width
        screen_h = screen.get_rect().height

        self.grid_size = dpi // 3

        side_bar_w = self.grid_size * 3
        side_bar_h = screen_h

        self.play_rect = pygame.Rect(0, 0, screen_w - side_bar_w, screen_h)
        self.play_area = tile_grid_bg(self.play_rect, self.grid_size)

        self.side_bar_rect = pygame.Rect(screen_w - side_bar_w, 0, side_bar_w, side_bar_h)
        self.side_bar = side_bar_bg(self.side_bar_rect, self.grid_size)

        self.tile_color = parse_color("#dee5e8")
        self.tile_bg = plate_bg(self.grid_size, self.tile_color)
        self.dark_tile_bg = plate_bg(self.grid_size, parse_color("#211a17"))
        self.clip_tile = plate_bg(self.grid_size, parse_color("#F48"))

        self.initial_placement = plate_outline(self.grid_size, parse_color("#888"))
        self.valid_placement = plate_outline(self.grid_size, parse_color("#080"))
        self.invalid_placement = plate_outline(self.grid_size, parse_color("#800"), True)

        self.select_color = lch_swizzle(self.tile_color, parse_color("#880000"), (.5, .75, 0))
        self.selected_tile_bg = plate_bg(self.grid_size, self.select_color)

        self.inspect_target = plate_bg(self.grid_size, self.tile_color, "inspect")
        self.inspect_active = plate_bg(self.grid_size, self.select_color, "inspect")

        self.select_target = plate_bg(self.grid_size, self.tile_color, "select")
        self.select_active = plate_bg(self.grid_size, self.select_color, "select")

        self.move_target = plate_bg(self.grid_size, self.tile_color, "pick\n&\nplace")
        self.move_active = plate_bg(self.grid_size, self.select_color, "pick\n&\nplace")

        self.save_target = plate_bg(self.grid_size, self.tile_color, "save\npatch")
        self.save_active = plate_bg(self.grid_size, self.select_color, "save\npatch")

        self.load_target = plate_bg(self.grid_size, self.tile_color, "load\npatch")
        self.load_active = plate_bg(self.grid_size, self.select_color, "load\npatch")

        self.calc_target = plate_bg(self.grid_size, self.tile_color, "calc")
        self.calc_active = plate_bg(self.grid_size, self.select_color, "calc")

        self.scope_target = plate_bg(self.grid_size, self.tile_color, "scope")
        self.scope_active = plate_bg(self.grid_size, self.select_color, "scope")

        self.connect_target = plate_bg(self.grid_size, self.tile_color, "manual\nconnect")
        self.connect_active = plate_bg(self.grid_size, self.select_color, "full\nconnect")

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

            self.draw(editor)

    def set_screen_label(self, editor, text, color=parse_color("#000"), alpha = .4):
        inner_w = (editor.play_area.viewport.w // editor.grid_size) * editor.grid_size
        inner_h = (editor.play_area.viewport.h // editor.grid_size) * editor.grid_size
        margin_x = (editor.play_area.viewport.w - inner_w) // 2
        margin_y = (editor.play_area.viewport.h - inner_h) // 2

        font_path, size = AFACAD_REGULAR, editor.grid_size
        self.screen_label_surface = render_text(font_path, size, color, text)
        self.screen_label_surface.set_alpha(int(alpha * 255))
        self.screen_label_rect = self.screen_label_surface.get_rect().copy()
        self.screen_label_rect.left = margin_x
        self.screen_label_rect.top = margin_y + editor.grid_size - get_font_baseline(font_path, size)

    def touch_start(self, editor, key, pos, event):
        self.force_redraw = True
        editor_screen.touch_points[key] = pos
        editor_screen.touch_colors[key] = oklch(0.5, 0.15, random.randint(0, 360))

    def touch_update(self, editor, key, pos, event):
        if key in editor_screen.touch_points:
            force_redraw = True
            editor_screen.touch_points[key] = pos

    def touch_end(self, editor, key, pos, event):
        if key in editor_screen.touch_points:
            self.force_redraw = True
            del editor_screen.touch_points[key]
            del editor_screen.touch_colors[key]

    def reset_touch_tracker(self):
        self.force_redraw = True
        editor_screen.touch_points = {}
        editor_screen.touch_colors = {}

    def draw_touch_points(self, editor):
        radius = editor.grid_size / 2
        for key, pos in editor_screen.touch_points.items():
            color = editor_screen.touch_colors[key]
            pygame.draw.circle(editor.screen, color, pos, radius)

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

    def purge_events(self):
        self.reset_touch_tracker()
        for event in pygame.event.get():
            if (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                self.live = False
            elif event.type == pygame.QUIT:
                exit(0)

    @profile_function("process_events")
    def process_events(self, editor):
        for event in pygame.event.get():
            if (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                self.purge_events()
                self.live = False

            elif event.type == pygame.MOUSEMOTION and (abs(event.rel[0]) > 0 or abs(event.rel[1]) > 0):
                self.on_move(editor, event.pos, event)

            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == pygame.BUTTON_LEFT:
                self.on_press(editor, event.pos, event)

            elif event.type == pygame.MOUSEBUTTONUP and event.button == pygame.BUTTON_LEFT:
                self.on_release(editor, event.pos, event)

            elif event.type == pygame.FINGERMOTION:
                key = (event.touch_id, event.finger_id)
                pos = (event.x * editor.screen.get_width(), event.y * editor.screen.get_height())
                self.touch_update(editor, key, pos, event)

            elif event.type == pygame.FINGERDOWN:
                key = (event.touch_id, event.finger_id)
                pos = (event.x * editor.screen.get_width(), event.y * editor.screen.get_height())
                self.touch_start(editor, key, pos, event)

            elif event.type == pygame.FINGERUP:
                key = (event.touch_id, event.finger_id)
                pos = (event.x * editor.screen.get_width(), event.y * editor.screen.get_height())
                self.touch_end(editor, key, pos, event)

            elif event.type == pygame.QUIT:
                exit(0)

    def draw(self, editor):
        pass
