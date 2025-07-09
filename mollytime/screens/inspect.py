
from .common import *
from .select import select_screen
from .calc import calculator_screen
from .pick_and_place import pick_and_place_screen


class inspect_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect")
        self.repopulate_sidebar(editor)

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        active_rect = pygame.Rect(
            editor.grid_size,
            0 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.inspect_active

        goto_select_rect = pygame.Rect(
            editor.grid_size,
            2 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_select_icon = editor.select_target

        goto_move_rect = pygame.Rect(
            editor.grid_size,
            1 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_move_icon = editor.move_target

        self.side_bar_targets = [
            (active_rect, active_icon, None),
            (goto_select_rect, goto_select_icon, self.goto_select_screen),
            (goto_move_rect, goto_move_icon, self.goto_pick_and_place_screen)]

    def goto_select_screen(self, editor):
        overlay = select_screen(editor)
        self.purge_events()
        self.update_play_area = True
        self.update_sidebar = True
        editor.clear_selection()

    def goto_pick_and_place_screen(self, editor):
        overlay = pick_and_place_screen(editor)
        self.purge_events()
        self.update_play_area = True
        self.update_sidebar = True
        editor.clear_selection()

    def goto_calculator(self, editor):
        overlay = calculator_screen(editor)
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
            something_happened = False
            for tile_id, (tile_x, tile_y) in editor.tile_positions.items():
                rect = pygame.Rect(
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
                    return

    def on_release(self, editor):
        self.press_start = None

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area or self.force_redraw:
            self.update_play_area = False
            update_anything = True

            editor.play_area.focus_x = editor.focus_x
            editor.play_area.focus_y = editor.focus_y
            editor.play_area.redraw()

            frame = editor.play_area.surface.copy()

            for tile_id, tile_xy in editor.tile_positions.items():
                rect = editor.get_tile_rect(tile_id)
                label = editor.patch.get_tile_label(tile_id)
                editor.tile_bg.draw(frame, rect, label)

            for (out_port, in_port) in editor.patch.wires:
                lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
                rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
                radius = max(4, editor.grid_size // 12)
                draw_arrow(frame, (0, 0, 0), lhs_rect, rhs_rect, radius)

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

        if update_anything:
            #font_debug_surface(editor.screen)
            self.draw_touch_points(editor)
            pygame.display.flip()
        else:
            editor.clock.tick(60)
