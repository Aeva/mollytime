
import random
from .common import *


class scope_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect > scope", parse_color("#333"))
        self.repopulate_sidebar(editor)

        self.scope_surface = editor.play_area.surface.copy()
        self.scope_surface.fill(parse_color("#000"))
        self.wire_color = parse_color("#333")

        self.beam_color = parse_color("#fff000")
        self.scope_cursor = 0

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        goto_inspect_rect = pygame.Rect(
            editor.grid_size,
            0 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_inspect_icon = editor.inspect_target

        active_rect = pygame.Rect(
            editor.grid_size,
            1 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.scope_active

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None)]

    def goto_inspect_screen(self, editor):
        self.live = False

    def on_move(self, editor, pos):
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

    def on_press(self, editor, pos):
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

    def on_release(self, editor, pos):
        self.press_start = None

    def draw(self, editor):
        # always draw the play area sans background
        editor.play_area.focus_x = editor.focus_x
        editor.play_area.focus_y = editor.focus_y

        frame = self.scope_surface#.copy()

        for tile_id, tile_xy in editor.tile_positions.items():
            rect = editor.get_tile_rect(tile_id)
            label = editor.patch.get_tile_label(tile_id)
            editor.dark_tile_bg.draw(frame, rect, label)

        for (out_port, in_port) in editor.patch.wires:
            lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
            rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
            radius = max(4, editor.grid_size // 12)
            draw_arrow(frame, self.wire_color, lhs_rect, rhs_rect, radius)

        sample = editor.patch.read_output_probe()
        beam_x = self.scope_cursor
        center = editor.play_rect.h * .5
        beam_y = center * -sample + center

        next_x = (beam_x + 1) % editor.play_rect.w
        top = (next_x + 1, editor.play_rect.top)
        bottom = (next_x + 1, editor.play_rect.bottom)
        draw_line(frame, (0, 0, 0), top, bottom, 1)

        beam_color = self.beam_color if abs(sample) <= 1.0 else (255, 0, 0)

        zero = (beam_x, center)
        beam = (beam_x, beam_y)
        draw_line(frame, beam_color, zero, beam, 1)

        self.scope_cursor += 1
        if self.scope_cursor >= editor.play_rect.w:
            self.scope_cursor = 0
            self.beam_color = (random.randint(32, 255), random.randint(32, 255), random.randint(32, 255))

        frame.blit(self.screen_label_surface, self.screen_label_rect)
        editor.screen.blit(frame, editor.play_area.viewport)

        # draw sidebar
        if self.update_sidebar or self.force_redraw:
            self.update_sidebar = False
            update_anything = True

            frame = editor.side_bar.surface.copy()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            editor.screen.blit(frame, editor.side_bar.viewport)

        self.draw_touch_points(editor)
        pygame.display.flip()
