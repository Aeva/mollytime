
from .common import *
from .select import select_screen


class inspect_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect")

        active_rect = pygame.Rect(
            editor.grid_size,
            0 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.inspect_active

        goto_select_rect = pygame.Rect(
            editor.grid_size,
            3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_select_icon = editor.select_target

        self.side_bar_targets = [
            (active_rect, active_icon, None),
            (goto_select_rect, goto_select_icon, self.goto_select_screen)]

    def goto_select_screen(self, editor):
        overlay = select_screen(editor)
        self.purge_events()
        self.update_play_area = True
        self.update_sidebar = True
        editor.clear_selection()

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

    def on_release(self, editor):
        self.press_start = None

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area:
            self.update_play_area = False
            update_anything = True

            editor.play_area.focus_x = editor.focus_x
            editor.play_area.focus_y = editor.focus_y
            editor.play_area.redraw()

            frame = editor.play_area.surface.copy()

            def frame_x(tile_x):
                return editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3

            def frame_y(tile_y):
                return editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3

            def frame_xy(wire_xy):
                return (frame_x(wire_xy[0]) + editor.grid_size, frame_y(wire_xy[1]) + editor.grid_size)

            for (lhs_id, lhs_param), (rhs_id, rhs_param) in editor.wires:
                lhs_pos = editor.tile_positions[lhs_id]
                rhs_pos = editor.tile_positions[rhs_id]
                draw_line(frame, (0, 0, 0), frame_xy(lhs_pos), frame_xy(rhs_pos), max(4, editor.grid_size // 8))

            for (tile_x, tile_y) in editor.tile_positions.values():
                rect = pygame.Rect(
                    frame_x(tile_x), frame_y(tile_y),
                    editor.grid_size * 2, editor.grid_size * 2)

                frame.blit(editor.tile_bg.surface, rect)

            frame.blit(self.screen_label_surface, self.screen_label_rect)
            editor.screen.blit(frame, editor.play_area.viewport)

        # draw sidebar
        if self.update_sidebar:
            self.update_sidebar = False
            update_anything = True

            frame = editor.side_bar.surface.copy()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            editor.screen.blit(frame, editor.side_bar.viewport)

        if update_anything:
            #font_debug_surface(editor.screen)
            pygame.display.flip()
        else:
            editor.clock.tick(60)
