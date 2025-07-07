
from .common import *


class move_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None

        self.grabbed_tile = None
        self.original_position = None
        self.last_valid_position = None
        self.last_hover_position = None
        self.drop_deletes = False

        self.set_screen_label(editor, "inspect > move")
        self.repopulate_sidebar(editor)

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

        active_icon = editor.move_active

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None)]

    def goto_inspect_screen(self, editor):
        self.live = False

    def on_move(self, editor, pos):
        self.cursor_pos = pos

        if self.press_start:
            move_x = pos[0] - self.press_start[0]
            move_y = pos[1] - self.press_start[1]

            if move_x != 0 or move_y != 0:
                editor.focus_x -= move_x
                editor.focus_y -= move_y
                self.update_play_area = True

            self.press_start = pos

        elif self.grabbed_tile:
            self.force_redraw = True
            self.drop_deletes = editor.side_bar_rect.collidepoint(pos)

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

    def on_press(self, editor, pos):
        self.cursor_pos = pos

        if editor.play_rect.collidepoint(pos):
            something_happened = False
            for tile_id, (tile_x, tile_y) in editor.tile_positions.items():
                rect = pygame.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)
                if rect.collidepoint(pos):
                    assert(self.grabbed_tile == None)
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

    def on_release(self, editor):
        if self.grabbed_tile:
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

                if tile_id == self.grabbed_tile:
                    editor.initial_placement.draw(frame, rect, label)
                else:
                    editor.tile_bg.draw(frame, rect, label)

            for (out_port, in_port) in editor.patch.wires:
                lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
                rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
                radius = max(4, editor.grid_size // 12)
                draw_arrow(frame, (0, 0, 0), lhs_rect, rhs_rect, radius)

            if self.grabbed_tile and not self.drop_deletes:
                label = editor.patch.get_tile_label(self.grabbed_tile)

                rect = editor.get_grid_rect(self.last_valid_position)
                editor.valid_placement.draw(frame, rect, label)

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

        if self.grabbed_tile:
            rect = pygame.rect.Rect(0, 0, editor.grid_size * 2, editor.grid_size * 2)
            rect.center = self.cursor_pos
            if self.drop_deletes:
                symbol = editor.patch.get_tile_symbol(self.grabbed_tile)
                if symbol == OpCode.CONST and editor.patch.get_constant(self.grabbed_tile) == 1337:
                    editor.tile_bg.draw(editor.screen, rect, "DROP\n&\nRUN")
                else:
                    editor.tile_bg.draw(editor.screen, rect, "drop\nto\ndelete")
            elif self.last_valid_position == self.last_hover_position:
                editor.tile_bg.draw(editor.screen, rect, label)
            else:
                editor.invalid_placement.draw(editor.screen, rect, label)

        if update_anything:
            self.draw_touch_points(editor)
            pygame.display.flip()
        else:
            editor.clock.tick(60)
