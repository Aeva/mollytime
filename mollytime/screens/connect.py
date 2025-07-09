
from .common import *


class connect_screen(editor_screen):
    def setup(self, editor):
        self.lhs_tile = editor.lhs_selection()
        self.rhs_tile = editor.rhs_selection() or self.lhs_tile

        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.press_stop = None
        self.cut_start = None
        self.cut_stop = None

        screen_label_color = (255, 255, 255)
        self.set_screen_label(editor, "connect", screen_label_color, 1)

        goto_apply_rect = pygame.Rect(
            editor.grid_size,
            3 * 3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_cancel_rect = pygame.Rect(
            editor.grid_size,
            4 * 3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        self.side_bar_targets = [
            (goto_apply_rect, editor.apply_target, self.goto_apply),
            (goto_cancel_rect, editor.cancel_target, self.goto_cancel)]

        self.update_sidebar = True
        self.update_play_area = True

        editor.play_area.focus_x = editor.focus_x
        editor.play_area.focus_y = editor.focus_y
        editor.play_area.redraw()

        node_graph = editor.play_area.surface.copy()

        for tile_id, tile_xy in editor.tile_positions.items():
            rect = editor.get_tile_rect(tile_id)
            label = editor.patch.get_tile_label(tile_id)
            pattern = editor.selected_tile_bg if editor.is_selected(tile_id) else editor.tile_bg
            pattern.draw(node_graph, rect, label)

        for (out_port, in_port) in editor.patch.wires:
            lhs_rect = editor.get_tile_rect(decode_port_tile(out_port))
            rhs_rect = editor.get_tile_rect(decode_port_tile(in_port))
            radius = max(4, editor.grid_size // 12)
            draw_arrow(node_graph, (0, 0, 0), lhs_rect, rhs_rect, radius)

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

        radius = (viewport.h - editor.grid_size) // 3

        self.bg.blit(self.screen_label_surface, self.screen_label_rect)
        font_path, size = AFACAD_REGULAR, editor.grid_size
        surface = render_text(font_path, size, screen_label_color, editor.patch.get_tile_name(self.lhs_tile))
        rect = surface.get_rect().copy()
        rect.top = self.screen_label_rect.top
        rect.left = viewport.centerx - radius
        self.bg.blit(surface, rect)
        surface = render_text(font_path, size, screen_label_color, editor.patch.get_tile_name(self.rhs_tile))
        rect = surface.get_rect().copy()
        rect.top = self.screen_label_rect.top
        rect.left = viewport.centerx + radius
        self.bg.blit(surface, rect)

        tile_size = editor.grid_size * 2

        self.lhs_output_ports = []
        self.lhs_output_rects = {}
        self.lhs_input_ports = []
        self.lhs_input_rects = {}
        start = (viewport.centerx - radius, 0)
        stop = (viewport.centerx - radius - tile_size, viewport.bottom - tile_size)

        lhs_outputs = editor.patch.get_tile_output_ports(self.lhs_tile)
        lhs_inputs = editor.patch.get_tile_input_ports(self.lhs_tile)
        count = len(lhs_outputs) + len(lhs_inputs)
        index = 0
        for port in lhs_outputs:
            alpha = (index + 1) / (count + 1)
            index += 1
            pos = vec_lerp(start, stop, alpha)
            self.lhs_output_ports.append(port)
            self.lhs_output_rects[port] = pygame.Rect(pos, (editor.grid_size * 2, editor.grid_size * 2))
        for port in lhs_inputs:
            alpha = (index + 1) / (count + 1)
            index += 1
            pos = vec_lerp(start, stop, alpha)
            self.lhs_input_ports.append(port)
            self.lhs_input_rects[port] = pygame.Rect(pos, (editor.grid_size * 2, editor.grid_size * 2))

        self.rhs_output_ports = []
        self.rhs_output_rects = {}
        self.rhs_input_ports = []
        self.rhs_input_rects = {}
        start = (viewport.centerx + radius, 0)
        stop = (viewport.centerx + radius - tile_size, viewport.bottom - tile_size)
        rhs_inputs = list(reversed(editor.patch.get_tile_input_ports(self.rhs_tile)))
        rhs_outputs = list(reversed(editor.patch.get_tile_output_ports(self.rhs_tile)))
        count = len(rhs_outputs) + len(rhs_inputs)
        index = 0
        for port in rhs_inputs:
            alpha = (index + 1) / (count + 1)
            index += 1
            pos = vec_lerp(start, stop, alpha)
            self.rhs_input_ports.append(port)
            self.rhs_input_rects[port] = pygame.Rect(pos, (editor.grid_size * 2, editor.grid_size * 2))
        for port in rhs_outputs:
            alpha = (index + 1) / (count + 1)
            index += 1
            pos = vec_lerp(start, stop, alpha)
            self.rhs_output_ports.append(port)
            self.rhs_output_rects[port] = pygame.Rect(pos, (editor.grid_size * 2, editor.grid_size * 2))

        self.connections = set()
        for out_port, in_port in editor.patch.wires:
            out_tile = decode_port_tile(out_port)
            in_tile = decode_port_tile(in_port)
            if out_tile in editor.selected and in_tile in editor.selected:
                self.connections.add((out_port, in_port))

    def goto_apply(self, editor):
        self.live = False
        for wire in self.connections:
            editor.patch.connect_tiles(*wire)
        disconnects = set()
        for wire in editor.patch.wires:
            out_port, in_port = wire
            out_tile = decode_port_tile(out_port)
            in_tile = decode_port_tile(in_port)
            has_output = out_tile in (self.lhs_tile, self.rhs_tile)
            has_input = in_tile in (self.lhs_tile, self.rhs_tile)
            if has_output and has_input and wire not in self.connections:
                disconnects.add(wire)
        for wire in disconnects:
            editor.patch.disconnect_tiles(*wire)

    def goto_cancel(self, editor):
        self.live = False

    def on_move(self, editor, pos):
        self.cursor_pos = pos

        if self.press_start:
            self.press_stop = pos
            self.update_play_area = True
        elif self.cut_start:
            self.cut_stop = pos
            self.update_play_area = True

    def on_press(self, editor, pos):
        if editor.play_rect.collidepoint(pos):
            all_rects = list(self.lhs_output_rects.values()) \
                + list(self.rhs_output_rects.values()) \
                + list(self.lhs_input_rects.values()) \
                + list(self.rhs_input_rects.values())
            for rect in all_rects:
                if rect.collidepoint(pos):
                    self.press_start = pos
                    self.press_stop = pos
                    self.update_play_area = True
                    return
            if not self.press_start:
                self.cut_start = pos
                self.cut_stop = pos
                self.update_play_area = True

        elif editor.side_bar_rect.collidepoint(pos):
            rel_pos = (pos[0] - editor.side_bar.viewport.x, pos[1] - editor.side_bar.viewport.y)
            for rect, surface, action in self.side_bar_targets:
                if action is not None and rect.collidepoint(rel_pos):
                    action(editor)
                    return

    def on_release(self, editor, pos):
        if self.press_start:
            def find_match(self):
                def any_hit(self, rect):
                    return rect.collidepoint(self.press_start) or rect.collidepoint(self.press_stop)
                rect_groups = (
                    (self.lhs_output_rects, self.rhs_input_rects),
                    (self.rhs_output_rects, self.lhs_input_rects))
                for output_rects, input_rects in rect_groups:
                    for out_port, out_rect in output_rects.items():
                        if any_hit(self, out_rect):
                            for in_port, in_rect in input_rects.items():
                                if any_hit(self, in_rect):
                                    return out_port, in_port
                return None, None
            start_port, stop_port = find_match(self)

            if start_port and stop_port and start_port != stop_port:
                wire = (start_port, stop_port)
                self.connections.add(wire)

            self.press_start = None
            self.press_stop = None
            self.update_play_area = True

        elif self.cut_start:
            def intersection(wire_start, wire_stop):
                a = vec_sub(wire_stop, wire_start)
                b = vec_sub(self.cut_stop, self.cut_start)
                a_mag = length(a)
                b_mag = length(b)
                a = vec_scale(a, 1/a_mag)
                b = vec_scale(b, 1/b_mag)
                c = vec_sub(self.cut_start, wire_start)
                ax, ay = a
                bx, by = b
                cx, cy = c
                det = ax * by - ay * bx
                if det == 0:
                    # rays are parallel
                    return False

                u = (cx * by - cy * bx) / det
                v = (cx * ay - cy * ax) / det

                return (u >= 0 and v >= 0 and u <= a_mag and v <= b_mag)

            disconnects = []
            for wire in self.connections:
                out_port, in_port = wire
                out_rect = self.lhs_output_rects.get(out_port)
                in_rect = self.rhs_input_rects.get(in_port)
                if not (out_rect and in_rect):
                    out_rect = self.rhs_output_rects.get(out_port)
                    in_rect = self.lhs_input_rects.get(in_port)
                assert (out_rect and in_rect)
                endpoints = [out_rect.center, in_rect.center]
                if intersection(*endpoints):
                    disconnects.append(wire)
            for wire in disconnects:
                self.connections.remove(wire)

            self.cut_start = None
            self.cut_stop = None
            self.update_play_area = True

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area or self.force_redraw:
            self.update_play_area = False
            update_anything = True

            frame = self.bg.copy()

            line_color = (128, 255, 255)
            line_width = editor.grid_size // 4
            radius = line_width // 2

            for port, rect in list(self.lhs_output_rects.items()) + list(self.rhs_output_rects.items()):
                tile_id = decode_port_tile(port)
                label = editor.patch.get_output_port_name(port)
                assert(editor.patch.get_tile_symbol(tile_id) != OpCode.OUT)
                editor.tile_bg.draw(frame, rect, label)

            for port, rect in list(self.lhs_input_rects.items()) + list(self.rhs_input_rects.items()):
                tile_id = decode_port_tile(port)
                label = editor.patch.get_input_port_name(port)
                if editor.patch.get_tile_symbol(tile_id) == OpCode.OUT:
                    label = "line\nout"
                editor.tile_bg.draw(frame, rect, label)

            for out_port, in_port in self.connections:
                start_rect = self.lhs_output_rects.get(out_port) or self.rhs_output_rects.get(out_port)
                stop_rect = self.lhs_input_rects.get(in_port) or self.rhs_input_rects.get(in_port)
                draw_arrow(frame, line_color, start_rect, stop_rect, radius)

            if self.press_start and self.press_stop:
                line_color = (0, 255, 0)
                radius = max(editor.grid_size // 4, 4)
                pygame.draw.circle(frame, line_color, self.press_start, radius)
                pygame.draw.circle(frame, line_color, self.press_stop, radius)
                draw_line(frame, line_color, self.press_start, self.press_stop, radius)

            elif self.cut_start and self.cut_stop:
                line_color = (255, 0, 0)
                radius = 2
                draw_line(frame, line_color, self.cut_start, self.cut_stop, radius)

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
            self.draw_touch_points(editor)
            pygame.display.flip()
        else:
            editor.clock.tick(60)
