
from .common import *


class connect_screen(editor_screen):
    def setup(self, editor):
        self.lhs_tile = editor.lhs_selection()
        self.rhs_tile = editor.rhs_selection()

        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.press_stop = None
        self.set_screen_label(editor, f"connect {self.lhs_tile} ↔ {self.rhs_tile}", (255, 255, 255), 1)

        goto_apply_rect = pygame.Rect(
            editor.grid_size,
            3 * 3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_cancel_rect = pygame.Rect(
            editor.grid_size,
            4 * 3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        self.side_bar_targets = [
            (goto_apply_rect, editor.apply_target, self.goto_cancel),
            (goto_cancel_rect, editor.cancel_target, self.goto_cancel)]

        self.update_sidebar = True
        self.update_play_area = True

        editor.play_area.focus_x = editor.focus_x
        editor.play_area.focus_y = editor.focus_y
        editor.play_area.redraw()

        node_graph = editor.play_area.surface.copy()
        for (tile_x, tile_y) in editor.tiles:
            rect = pygame.Rect(
                editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                editor.grid_size * 2, editor.grid_size * 2)

            sprite = editor.selected_tile_bg if editor.is_selected((tile_x, tile_y)) else editor.tile_bg
            node_graph.blit(sprite.surface, rect)

        node_graph.set_alpha(int(0.25 * 255))

        matte_color = editor.select_color # parse_color("#b1b3b8") #editor.select_color

        self.bg = pygame.Surface((node_graph.get_width(), node_graph.get_height()))
        self.bg.fill((0, 0, 0))
        self.bg.blit(node_graph, (0, 0))

        viewport = editor.play_area.viewport
        matte = self.bg.copy()

        start = (viewport.centerx, 0)
        stop = (viewport.centerx - editor.grid_size * 2, viewport.h)
        points = [(0, 0), start, stop, (0, viewport.h)]
        pygame.draw.polygon(matte, matte_color, points)
        pygame.draw.aaline(matte, matte_color, start, stop)

        start = (viewport.centerx + editor.grid_size * 2, 0)
        stop = (viewport.centerx, viewport.h)
        points = [
            (viewport.w, viewport.h), stop,
            start, (viewport.w, 0)]
        pygame.draw.polygon(matte, matte_color, points)
        start = (start[0] - 1, start[1])
        stop = (stop[0] - 1, stop[1])
        pygame.draw.aaline(matte, matte_color, start, stop)

        matte.set_alpha(int(0.8 * 255))
        self.bg.blit(matte, (0, 0))

        self.bg.blit(self.screen_label_surface, self.screen_label_rect)

        radius = (viewport.h - editor.grid_size) // 3

        self.lhs_rect = pygame.Rect(0, 0, editor.grid_size * 2, editor.grid_size * 2)
        self.lhs_rect.left = viewport.centerx - radius
        self.lhs_rect.centery = viewport.centery

        self.rhs_rect = pygame.Rect(0, 0, editor.grid_size * 2, editor.grid_size * 2)
        self.rhs_rect.right = viewport.centerx + radius
        self.rhs_rect.centery = viewport.centery

        self.lhs_nodes = [self.lhs_tile]
        self.rhs_nodes = [self.rhs_tile]

        self.nodes = self.lhs_nodes + self.rhs_nodes
        self.node_rects = {}
        self.node_rects[self.lhs_tile] = self.lhs_rect
        self.node_rects[self.rhs_tile] = self.rhs_rect
        self.connections = set()

    def goto_cancel(self, editor):
        self.live = False

    def on_move(self, editor, pos):
        self.cursor_pos = pos

        if self.press_start:
            self.press_stop = pos
            self.update_play_area = True

    def on_press(self, editor, pos):
        if editor.play_rect.collidepoint(pos):
            for tile in self.nodes:
                if self.node_rects[tile].collidepoint(pos):
                    self.press_start = pos
                    self.press_stop = pos
                    self.update_play_area = True
                    return

        elif editor.side_bar_rect.collidepoint(pos):
            rel_pos = (pos[0] - editor.side_bar.viewport.x, pos[1] - editor.side_bar.viewport.y)
            for rect, surface, action in self.side_bar_targets:
                if action is not None and rect.collidepoint(rel_pos):
                    action(editor)
                    return

    def on_release(self, editor):
        if self.press_start:
            start_key = None
            stop_key = None

            for tile in self.lhs_nodes:
                rect = self.node_rects[tile]
                if rect.collidepoint(self.press_start):
                    start_key = tile
                    for tile in self.rhs_nodes:
                        rect = self.node_rects[tile]
                        if rect.collidepoint(self.press_stop):
                            stop_key = tile
                            break
                    break

            if start_key is None:
                for tile in self.rhs_nodes:
                    rect = self.node_rects[tile]
                    if rect.collidepoint(self.press_start):
                        start_key = tile
                        for tile in self.lhs_nodes:
                            rect = self.node_rects[tile]
                            if rect.collidepoint(self.press_stop):
                                stop_key = tile
                                break
                        break

            if start_key and stop_key:
                key = (start_key, stop_key)
                self.connections.add(key)

            self.press_start = None
            self.press_stop = None
            self.update_play_area = True

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area:
            self.update_play_area = False
            update_anything = True

            frame = self.bg.copy()

            line_color = (0, 255, 0)
            line_width = editor.grid_size // 4
            for start, stop in self.connections:
                start_pos = self.node_rects[start].center
                stop_pos = self.node_rects[stop].center
                pygame.draw.line(frame, line_color, start_pos, stop_pos, line_width)

            for tile in self.nodes:
                rect = self.node_rects[tile]
                frame.blit(editor.tile_bg.surface, rect)

            if self.press_start and self.press_stop:
                line_color = (int(.75 * 255), 0, 0)
                line_width = editor.grid_size // 2
                radius = line_width // 2
                pygame.draw.circle(frame, line_color, self.press_start, radius)
                pygame.draw.circle(frame, line_color, self.press_stop, radius)
                pygame.draw.line(frame, line_color, self.press_start, self.press_stop, line_width)

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
