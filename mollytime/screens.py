
import math
import os
import re
import subprocess

import pygame_setup
import pygame

from fonts import *
from colors import *
from patterns import *


class node_graph_card:
    def __init__(self, screen, dpi):
        self.focus_x = 0
        self.focus_y = 0
        self.tiles = [(-1, 0), (1, 0), (0, -1), (0, 1)]
        self.selected = []

        self.clock = pygame.time.Clock()
        self.resize(screen, dpi)

    def toggle_selection(self, tile):
        assert(tile in self.tiles)
        if tile in self.selected:
            self.selected = [select for select in self.selected if select != tile]
        else:
            if len(self.selected) < 2:
                self.selected.append(tile)
            else:
                assert(len(self.selected) == 2)
                self.selected = [self.selected[1], tile]

    def clear_selection(self):
        self.selected = []

    def is_selected(self, tile):
        return tile in self.selected

    def any_selected(self):
        return len(self.selected) != 0

    def lhs_selection(self):
        return (self.selected + [None])[0]

    def rhs_selection(self):
        fill = self.lhs_selection()
        return (self.selected + [fill, fill])[1]

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
            for (tile_x, tile_y) in editor.tiles:
                rect = pygame.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)
                if rect.collidepoint(pos):
                    something_happened = True
                    self.update_play_area = True
                    state = editor.toggle_selection((tile_x, tile_y))
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
            for (tile_x, tile_y) in editor.tiles:
                rect = pygame.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)

                sprite = editor.selected_tile_bg if editor.is_selected((tile_x, tile_y)) else editor.tile_bg
                frame.blit(sprite.surface, rect)

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
            for (tile_x, tile_y) in editor.tiles:
                rect = pygame.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
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
