
import time

import pygame_setup
import pygame

from color import random_color
import surface_tools
import midi




class Tile:
    """
    The Tile is the basic interactive element of the gui.  A Tile is always created by
    a Plato subclass and never directly.
    """


    def __init__(self, rect, color):
        """
        This is a cold path.
        """

        color = color or random_color()
        surface = surface_tools.rect((rect.w, rect.h), color)

        # The parameters needed to draw this object.  A tile can draw multiple surfaces.
        self.draw_params = [(surface, rect)]

        # The bounding rect that is the union of all rects from self.draw_params.
        # Filled in automatically by the play surface.
        self.bounding_rect = None


    def hold(self, x, y):
        """
        Called when the Tile becomes active.
        Args `x` and `y` are values between 0.0 and 1.0 inclusive, and indicate where
        the tile was pressed relative to the tile's top left corner.

        This is a hot path.
        """
        pass


    def rub(self, x, y):
        """
        Called on an active Tile when the pressing finger or mouse cursor move within the tile.
        Args `x` and `y` are values between 0.0 and 1.0 inclusive, and indicate the new press
        position relative to the tile's top left corner.

        This is a hot path.
        """
        pass


    def release(self):
        """
        Called when the Tile deactivates.

        This is a hot path.
        """
        pass




class Plato:
    """
    Plato objects are plates which group together Tiles of similar functionality.
    A Plato object is typically constructed with its basic parameters in either pip
    space or a derived coordinate space like Tile offsets.  A Plato object is also
    responsible for defining and tracking the Tiles that belong to it.  Finally,
    Plato objects are also responsible for routing events to its tiles.
    """


    def __init__(self, x, y, w, h):
        """
        Args `x`, `y`, `w`, and `h` are specified as pip counts.

        Coordinates are as-if the origin were in the top left corner of the
        screen (same as pygame), but the play surface will fit this into actual
        screen space, thus negative coordinates are legal and useful.

        This is a cold path.
        """

        self.pip_min_x = int(x)
        self.pip_min_y = int(y)
        self.pip_w = int(w)
        self.pip_h = int(h)

        if self.pip_w < 0:
            self.pip_w = abs(self.pip_w)
            self.pip_min_x -= self.pip_w

        if self.pip_h < 0:
            self.pip_h = abs(self.pip_h)
            self.pip_min_y -= self.pip_h

        self.pip_w = max(self.pip_w, 1)
        self.pip_h = max(self.pip_h, 1)

        self.pip_max_x = self.pip_min_x + self.pip_w - 1
        self.pip_max_y = self.pip_min_y + self.pip_h - 1

        # The bounding rect for the entire plate.  Usually set by `populate`.
        self.frame = None

        # A list of Tiles belonging to this object.  Usually filled by `populate`.
        self.tiles = []


    def populate(self, pip_to_rect, frame_color=(64, 64, 64)):
        """
        Called by the PlaySurface.  This function is responsible for populating the
        self.frame and self.tiles variables, and is intended to be overridden.

        This is a cold path.
        """
        self.frame = pip_to_rect(self.pip_min_x, self.pip_min_y, self.pip_w, self.pip_h)
        self.tiles = [Tile(self.frame, frame_color)]


    def get_tiles(self):
        """
        Returns all tiles controlled by this Plato object.

        This is a cold path.
        """
        return self.tiles


    def match(self, point):
        """
        This function takes a screenspace coordinate and returns the tile that most
        matches for the purpose of routing input events.  If no tile matches, return
        None to continue propagating the event.  This is only called if the point is
        within the self.frame rect.  This function is intended to be overriden.

        This is a hot path.
        """
        if self.frame.collidepoint(point):
            for tile in reversed(self.tiles):
                for surface, rect in tile.draw_params:
                    if rect.collidepoint(point):
                        return tile
        return None




class PlaySurface:
    """
    The PlaySurface object is responsible for calculating the play space area from the
    screen size and the set of all Plato objects passed into it.  This determines the
    "pip" unit that is used for constructing the UI.  The coordinate system follows
    Pygame's except that negative widiget coordinates are valid because nothing can be
    placed off screen.  It is only practical to have one PlaySurface live at a time
    per display.

    The PalySurface object is also responsible for routing input events to the Plato
    objects it manages.

    This class is not intended to be overriden, but I'm not your mom.
    """


    def __init__(self, screen_size, plates, horizontal_align=.5, vertical_align=.5):
        """
        This is a cold path.
        """

        self.screen_w, self.screen_h = screen_size
        self.plates = plates

        pip_min_x = plates[0].pip_min_x
        pip_min_y = plates[0].pip_min_y
        pip_max_x = plates[0].pip_max_x
        pip_max_y = plates[0].pip_max_y

        for board in plates[1:]:
            pip_min_x = min(pip_min_x, board.pip_min_x)
            pip_min_y = min(pip_min_y, board.pip_min_y)
            pip_max_x = max(pip_max_x, board.pip_max_x)
            pip_max_y = max(pip_max_y, board.pip_max_y)

        pip_width = abs(pip_max_x - pip_min_x) + 1
        pip_height = abs(pip_max_y - pip_min_y) + 1

        pip_size = min(self.screen_w // pip_width, self.screen_h // pip_height)
        play_width = pip_size * pip_width
        play_height = pip_size * pip_height

        # Align to bottom-center:
        align_x = (self.screen_w - play_width) * horizontal_align
        align_y = (self.screen_h - play_height) * vertical_align

        def pip_to_screen(pip_x, pip_y):
            pip_x -= pip_min_x
            pip_y -= pip_min_y
            return (align_x + pip_x * pip_size, align_y + pip_y * pip_size)

        def pip_rect(pip_x, pip_y, pip_w, pip_h):
            return pygame.Rect(
                pip_to_screen(pip_x, pip_y),
                (pip_w * pip_size, pip_h * pip_size))

        for plate in plates:
            plate.populate(pip_rect)

        for plate in plates:
            for tile in plate.get_tiles():
                # Used to track rubbing.
                tile.__last_xy = (None, None)
                tile.__next_xy = (None, None)
                first, *rest = [rect for surface, rect in tile.draw_params]
                tile.bounding_rect = first.unionall(rest)

        self.fingers = {}
        self.last_held = set()
        self.mouse_state = False


    def test_point(self, point):
        """
        This is a hot path.
        """

        x, y = point

        for plate in self.plates:
            if tile := plate.match(point):
                x = (x - tile.bounding_rect.x) / tile.bounding_rect.width
                y = (y - tile.bounding_rect.y) / tile.bounding_rect.height
                tile.__next_xy = (x, y)
                return tile
        return None


    def input_event(self, event):
        """
        This is a hot path.
        """

        if event.type == pygame.FINGERDOWN or event.type == pygame.FINGERMOTION:
            point = (round(event.x * (self.screen_w - 1)), round(event.y * (self.screen_h - 1)))
            if tile := self.test_point(point):
                self.fingers[event.finger_id] = tile
            elif self.fingers.get(event.finger_id) is not None:
                del self.fingers[event.finger_id]

        elif event.type == pygame.FINGERUP:
            if self.fingers.get(event.finger_id) is not None:
                del self.fingers[event.finger_id]

        elif event.type == pygame.MOUSEBUTTONDOWN:
            if event.touch:
                return
            self.mouse_state = True
            finger_id = "m"
            if tile := self.test_point(event.pos):
                self.fingers[finger_id] = tile
            elif self.fingers.get(finger_id) is not None:
                del self.fingers[finger_id]

        elif event.type == pygame.MOUSEMOTION and self.mouse_state:
            if event.touch:
                return
            finger_id = "m"
            if tile := self.test_point(event.pos):
                self.fingers[finger_id] = tile
            elif self.fingers.get(finger_id) is not None:
                del self.fingers[finger_id]

        elif event.type == pygame.MOUSEBUTTONUP:
            if event.touch:
                return
            self.mouse_state = False
            finger_id = "m"
            if self.fingers.get(finger_id) is not None:
                del self.fingers[finger_id]


    def draw(self):
        """
        This is a hot path.
        """

        update_rects = []
        blit_sequence = []

        for plate in self.plates:
            update_rects.append(plate.frame)
            for tile in plate.tiles:
                blit_sequence += tile.draw_params

        return update_rects, blit_sequence


    def crank(self):
        """
        This is a hot path.
        """

        held = set(self.fingers.values())

        released = self.last_held - held
        pressed = held - self.last_held
        sustained = held & self.last_held

        self.last_held = held

        for tile in released:
            tile.release()
            tile.__last_xy = (None, None)
            tile.__next_xy = (None, None)

        for tile in pressed:
            tile.hold(*tile.__next_xy)
            tile.__last_xy = tile.__next_xy

        rubbed = set()
        for tile in sustained:
            if tile.__next_xy != tile.__last_xy:
                tile.rub(*tile.__next_xy)
                tile.__last_xy = tile.__next_xy
                rubbed.add(tile)

        if pressed or released or rubbed:
            midi.flush()
            return self.draw()

        else:
            return None, None




class Instrument:
    """
    Instrument objects represent the play session.  These are responsible for
    initializing Pygame, creating the PlaySurface, and running your main loop.  This
    object is not intended to be subclassed, and it is only useful to have one one live
    Instrument active at a time per display.  However, this class is currently written
    with the assumption that you only want to have one active at a time.

    Your Plato subclasses implement the actual behavior and presentation of the
    instrument.
    """

    def __init__(self, plates, horizontal_align=.5, vertical_align=1):
        """
        The `plates` argument is a list of Plato subclasses.

        The `horizontal_align` and `vertical_align` arguments are used to align the
        final play surface on screen.  The valid range for both is a number between
        0 and 1 inclusive, with 0 being the top left corner of the screen and 1 being
        the bottom right corner.  The default is center bottom.

        This is a cold path.
        """

        self.plates = plates
        self.h_align = horizontal_align
        self.v_align = vertical_align

    def __call__(self):
        """
        This is called to start the play session.

        The inner while loop is the root of all hot paths in this program.
        """
        pygame.init()

        sizes = pygame.display.get_desktop_sizes()
        display_index = len(sizes) - 1
        display_size = sizes[display_index]
        screen = pygame.display.set_mode(size=display_size, display=display_index, flags=pygame.FULLSCREEN)

        screen.fill((0, 0, 0))
        pygame.display.flip()

        play_surface = PlaySurface(display_size, self.plates, self.h_align, self.v_align)

        update_rects, blit_sequence = play_surface.draw()
        screen.blits(blit_sequence=blit_sequence)
        pygame.display.flip()

        while True:
            live = True

            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    live = False

                elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                    live = False

                else:
                    play_surface.input_event(event)

            if not live:
                break

            update_rects, blit_sequence = play_surface.crank()

            if blit_sequence:
                screen.blits(blit_sequence=blit_sequence)
                pygame.display.update(update_rects)
            else:
                time.sleep(1e-9)

        surface_tools.reset_memo()
        pygame.quit()
