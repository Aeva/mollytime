
from .common import *
from .connect import connect_screen


class select_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect > select")
        self.repopulate_sidebar(editor)

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        goto_inspect_rect = pygame.Rect(
            editor.grid_size,
            0 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_inspect_icon = editor.inspect_target

        active_rect = pygame.Rect(
            editor.grid_size,
            3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.select_active

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None)]

        if editor.any_selected():
            connect_rect = pygame.Rect(
                editor.grid_size,
                6 * editor.grid_size,
                editor.grid_size * 2, editor.grid_size * 2)

            connect_icon = editor.connect_target

            self.side_bar_targets.append((connect_rect, connect_icon, self.goto_connect_screen))

    def goto_inspect_screen(self, editor):
        self.live = False

    def goto_connect_screen(self, editor):
        if editor.any_selected():
            overlay = connect_screen(editor)
            self.purge_events()
            self.update_play_area = True
            self.update_sidebar = True
            editor.clear_selection()
            self.repopulate_sidebar(editor)

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
                    something_happened = True
                    self.update_play_area = True
                    state = editor.toggle_selection(tile_id)
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

            def frame_xy(tile_xy):
                return (
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_xy[0] * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_xy[1] * editor.grid_size * 3)

            wire_offset = (editor.grid_size, editor.grid_size)
            for (lhs_id, lhs_param), (rhs_id, rhs_param) in editor.wires:
                lhs_pos = vec_add(frame_xy(editor.tile_positions[lhs_id]), wire_offset)
                rhs_pos = vec_add(frame_xy(editor.tile_positions[rhs_id]), wire_offset)
                draw_line(frame, (0, 0, 0), lhs_pos, rhs_pos, max(4, editor.grid_size // 8))

            for tile_id, tile_xy in editor.tile_positions.items():
                tile = editor.tiles[tile_id]
                rect = pygame.Rect(
                    frame_xy(tile_xy),
                    (editor.grid_size * 2, editor.grid_size * 2))

                pattern = editor.selected_tile_bg if editor.is_selected(tile_id) else editor.tile_bg
                pattern.draw(frame, rect, str(tile))

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
            pygame.display.flip()
        else:
            editor.clock.tick(60)
