
import pygame_setup
import pygame

from tiles import *
from fonts import *
from colors import *
from patterns import *


class program_card:
    def __init__(self, screen, dpi):
        self.focus_x = 0
        self.focus_y = 0

        self.tiles = {}
        self.tile_positions = {}

        # connections
        self.wires = set()
        self.by_input = {}
        self.by_output = {}

        #   2    *
        #  440  sin   +
        #       sin  out
        two = self.add_tile((-1, -1), const_tile(2))
        a4_hz = self.add_tile((-1, 0), const_tile(440))
        a5_hz = self.add_tile((0, -1), mul_tile())
        a5_osc = self.add_tile((0, 0), sin_tile())
        a4_osc = self.add_tile((0, 1), sin_tile())
        summed = self.add_tile((1, 0), add_tile())
        out = self.add_tile((1, 1), out_tile())

        self.connect_tiles((two, '#'), (a5_hz, '*'))
        self.connect_tiles((a4_hz, '#'), (a5_hz, '*'))
        self.connect_tiles((a5_hz, '='), (a5_osc, 'hz'))
        self.connect_tiles((a4_hz, '#'), (a4_osc, 'hz'))
        self.connect_tiles((a4_osc, 'amp'), (summed, '+'))
        self.connect_tiles((a5_osc, 'amp'), (summed, '+'))
        self.connect_tiles((summed, '='), (out, 'out'))

        self.selected = []

        self.clock = pygame.time.Clock()
        self.resize(screen, dpi)

    def add_tile(self, position, tile):
        self.tiles[tile.id] = tile
        self.tile_positions[tile.id] = position
        for name in tile.inputs.keys():
            self.by_input[(tile.id, name)] = set()
        for name in tile.outputs:
            self.by_output[(tile.id, name)] = set()
        return tile.id

    def connect_tiles(self, out_key, in_key):
        if out_key in self.by_input and in_key in self.by_output:
            return self.connect_tiles(in_key, out_key)
        assert(out_key in self.by_output)
        assert(in_key in self.by_input)

        receiver = self.tiles[in_key[0]]
        if self.by_input[in_key] and not receiver.commutative:
            return

        self.wires.add((out_key, in_key))
        self.by_output[out_key].add(in_key)
        self.by_input[in_key].add(out_key)

    def disconnect_tiles(self, out_key, in_key):
        self.wires.remove((out_key, in_key))
        self.by_output[out_key].remove(in_key)
        self.by_input[in_key].remove(out_key)

    def toggle_selection(self, tile_id):
        assert(tile_id in self.tiles)
        if tile_id in self.selected:
            # deselect the tile
            self.selected = [select for select in self.selected if select != tile_id]
        else:
            # deselect incompatible selected tiles
            tile = self.tiles[tile_id]
            def can_connect(other_id):
                other = self.tiles[other_id]
                return bool((tile.inputs and other.outputs) or (other.inputs and tile.outputs))
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

    def connectable_selection(self):
        inputs = 0
        outputs = 0
        for tile_id in self.selected:
            tile = self.tiles[tile_id]
            inputs += len(tile.inputs)
            outputs += len(tile.outputs)
        return inputs > 0 and outputs > 0

    def lhs_selection(self):
        if len(self.selected) >= 1:
            return self.tiles[self.selected[0]]
        else:
            return None

    def rhs_selection(self):
        if len(self.selected) >= 2:
            return self.tiles[self.selected[1]]
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

        self.connect_target = plate_bg(self.grid_size, tile_color, "connect")
        self.connect_active = plate_bg(self.grid_size, self.select_color, "connect")

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
