
import pygame_setup
import pygame

from fonts import *
from colors import *
from patterns import *

from mollytime import Patch, OpCode, decode_port_tile, decode_port_index


class program_card:
    def __init__(self, screen, dpi):
        self.focus_x = 0
        self.focus_y = 0

        self.patch = Patch()
        self.tile_positions = {}

        #   2    *
        #  440  sin   +
        #       sin  out
        two = self.make_constant((-1, -1), 2)
        a4_hz = self.make_constant((-1, 0), 440)
        a5_hz = self.make_tile((0, -1), OpCode.MUL)
        a5_osc = self.make_tile((0, 0), OpCode.SIN)
        a4_osc = self.make_tile((-1, 1), OpCode.SIN)
        summed = self.make_tile((0, 1), OpCode.ADD)
        half = self.make_constant((1, -1), .5)
        gain = self.make_tile((1, 0), OpCode.MUL)
        out = self.make_tile((1, 1), OpCode.OUT)

        def quick_connect(lhs, rhs):
            wire = self.patch.get_implicit_wire(lhs, rhs)
            assert(wire)
            self.patch.connect_tiles(*wire)

        quick_connect(two, a5_hz)
        quick_connect(a4_hz, a5_hz)
        quick_connect(a5_hz, a5_osc)
        quick_connect(a4_hz, a4_osc)
        quick_connect(a4_osc, summed)
        quick_connect(a5_osc, summed)
        quick_connect(half, gain)
        quick_connect(summed, gain)
        quick_connect(gain, out)

        self.selected = []

        self.clock = pygame.time.Clock()
        self.resize(screen, dpi)

    def make_tile(self, position, symbol):
        tile_id = self.patch.make_tile(symbol);
        self.tile_positions[tile_id] = position
        return tile_id

    def make_constant(self, position, value):
        tile_id = self.patch.make_constant(value);
        self.tile_positions[tile_id] = position
        return tile_id

    def get_tile_rect(self, tile_id):
        tile_xy = self.tile_positions[tile_id]
        frame_x = self.play_rect.centerx - self.focus_x - self.grid_size + tile_xy[0] * self.grid_size * 3
        frame_y = self.play_rect.centery - self.focus_y - self.grid_size + tile_xy[1] * self.grid_size * 3
        return pygame.Rect((frame_x, frame_y), (self.grid_size * 2, self.grid_size * 2))

    def toggle_connection(self, out_key, in_key):
        before = self.patch.wires
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

        tile_color = parse_color("#dee5e8")
        self.tile_bg = plate_bg(self.grid_size, tile_color)

        self.select_color = lch_swizzle(tile_color, parse_color("#880000"), (.5, .75, 0))
        self.selected_tile_bg = plate_bg(self.grid_size, self.select_color)

        self.inspect_target = plate_bg(self.grid_size, tile_color, "inspect")
        self.inspect_active = plate_bg(self.grid_size, self.select_color, "inspect")

        self.select_target = plate_bg(self.grid_size, tile_color, "select")
        self.select_active = plate_bg(self.grid_size, self.select_color, "select")

        self.connect_target = plate_bg(self.grid_size, tile_color, "manual\nconnect")
        self.connect_active = plate_bg(self.grid_size, self.select_color, "full\nconnect")

        self.auto_connect = plate_bg(self.grid_size, tile_color, "connect")
        self.auto_disconnect = plate_bg(self.grid_size, tile_color, "detach")

        self.apply_target = plate_bg(self.grid_size, tile_color, "apply")

        self.cancel_target = plate_bg(self.grid_size, tile_color, "cancel")

        self.placeholder_target = plate_bg(self.grid_size, tile_color, "magic")


class editor_screen:
    def __init__(self, editor):
        self.setup(editor)
        self.update_play_area = True
        self.update_sidebar = True

        self.live = True
        self.purge_events()
        self.draw(editor)

        while self.live:
            self.process_events(editor)
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

    def purge_events(self):
        for event in pygame.event.get():
            if (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                self.live = False
            elif event.type == pygame.QUIT:
                exit(0)

    def process_events(self, editor):
        for event in pygame.event.get():
            if (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                self.live = False

            elif event.type == pygame.MOUSEMOTION and (abs(event.rel[0]) > 0 or abs(event.rel[1]) > 0):
                self.on_move(editor, event.pos)

            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == pygame.BUTTON_LEFT:
                self.on_press(editor, event.pos)

            elif event.type == pygame.MOUSEBUTTONUP and event.button == pygame.BUTTON_LEFT:
                self.on_release(editor)

            elif event.type == pygame.QUIT:
                exit(0)

    def draw(self, editor):
        pass
