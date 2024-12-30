
import os
import time
import math
from importlib import resources

import pygame
from alsa_midi import SequencerClient, WRITE_PORT, READ_PORT, NoteOnEvent, NoteOffEvent, ProgramChangeEvent

import media
from pallets import *


# # # # # # # # #

generator = basic_note_grid(60, 2, 1)
#generator = scales_grid(60, [1, 3, 5], [2, 4, 6])

# # # # # # # # #


client = SequencerClient("fnordboard")
port = client.create_port(
    "output",
    caps=READ_PORT)

timidity_proc = None
connection = None
device_priority = ["Arturia MicroFreak", "EP-1320", "MiniFuse 2 MIDI 1", "TiMidity"]

for target in device_priority:
    for device in client.list_ports(output=True):
        if device.client_name == target:
            connection = device.client_name
            port.connect_to(device)
            break
        elif device.name == target:
            connection = device.name
            port.connect_to(device)
            break

if connection != None:
    print(f"Connected to {connection}")

else:
    import subprocess
    try:
        timidity_proc = subprocess.Popen(["timidity", "-iA", "-Os", "--volume=200"])
        time.sleep(1)
    except OSError:
        print("Unable to start timidity :(")
        timidity_proc = None

    target = "TiMidity"
    for device in client.list_ports(output=True):
        if device.client_name == target:
            connection = device.client_name
            port.connect_to(device)

send_note_off = False
note_offset = 0
if connection == "TiMidity":
    # programs 21, 53, 91, 93, 94, 95, and 97 all work pretty well here
    event = ProgramChangeEvent(channel=0, value=95)
    client.event_output(event, port=port)
    note_offset = -12
    send_note_off = True

time.sleep(1)
if not connection:
    print(f"Unable to connect midi output :(")

if connection == "TiMidity":
    client.drain_output()


try:
    # Correct support of HiDPI on Linux requires setting both of these environment variables as well
    # as passing the desired unscaled resolution to `pygame.display.set_mode` via the `size` parameter.
    os.environ["SDL_VIDEODRIVER"] = "wayland,x11"
    os.environ["SDL_VIDEO_SCALE_METHOD"] = "letterbox"

    pygame.init()

    sizes = pygame.display.get_desktop_sizes()
    display_index = len(sizes) - 1
    screen = pygame.display.set_mode(size=sizes[display_index], display=display_index, flags=pygame.FULLSCREEN)

    screen_w, screen_h = sizes[display_index]

    play_span = min(screen_w, screen_h)

    button_grid = 4
    margin_grid = 1
    tile_grid = button_grid + margin_grid

    subgrid_count = tile_count * button_grid + tile_count * margin_grid + 1
    subgrid = int(play_span / subgrid_count)

    button_span = button_grid * subgrid

    align_common = int((play_span - (subgrid * subgrid_count)) * .5)

    align_x = align_common + screen_w - play_span
    align_y = align_common + screen_h - play_span
    limit_x = align_x + play_span
    limit_y = align_y + play_span

    midi_low = 255
    midi_high = -1
    for y in range(tile_count):
        for x in range(tile_count):
            n = generator(x - origin_tile_x, y - origin_tile_y)
            midi_low = min(midi_low, n)
            midi_high = max(midi_high, n)
    midi_range = midi_high - midi_low
    #print(midi_low, midi_high, midi_range)

    def hue_to_rgb(h):
        h = (h * 6) % 6
        a = h - math.floor(h)
        r = 0
        g = 0
        b = 0
        if h < 1:
            r = 1
            g = a
        elif h < 2:
            r = 1 - a
            g = 1
        elif h < 3:
            g = 1
            b = a
        elif h < 4:
            g = 1 - a
            b = 1
        elif h < 5:
            r = a
            b = 1
        else:
            r = 1
            b = 1 - a
        return (r, g, b)

    def heat_map(note):
        a = (note - midi_low) / midi_range
        return hue_to_rgb(a)

    def byte_color(float_color):
        return [min(max(int(f * 255), 0), 255) for f in float_color]

    #######

    font_path = resources.files(media) / "overpass" / "static" / "Overpass-ExtraLight.ttf"
    font = pygame.font.Font(font_path, int(button_span * .75))
    font_min_y = 100000
    font_max_y = -100000
    for min_x, max_x, min_y, max_y, advance in font.metrics("1234567890"):
        font_min_y = min(font_min_y, min_y)
        font_max_y = max(font_max_y, max_y)
    font_h = font_max_y - font_min_y

    note_sprites = [None] * 255
    for note in range(midi_low, midi_high + 1):
        text = str(note - midi_low)
        label = font.render(text, True, (0, 0, 0))
        w, h = label.get_width(), font.get_height()
        xy = ((button_span - w) * .5, font.get_ascent() - font_h)

        off = pygame.Surface((button_span, button_span))
        off.fill(byte_color(heat_map(note)))

        off.blit(label, xy)

        on = pygame.Surface((button_span, button_span))
        on.fill((255, 255, 255))
        on.blit(label, xy)

        note_sprites[note] = (off, on)

    buttons = list(range(tile_count * tile_count))
    for index in buttons:
        y = tile_count - 1 - (index // tile_count)
        x = index % tile_count

        rect = pygame.Rect(
            align_x + subgrid * (x * tile_grid + 1),
            align_y + subgrid * (y * tile_grid + 1),
            subgrid * button_grid,
            subgrid * button_grid)

        rel_x = x - origin_tile_x
        rel_y = y - origin_tile_y

        note = generator(rel_x, -rel_y)

        buttons[index] = [rect, note]

    def find_button(px, py):
        if px < align_x or px > limit_x or py < align_y or py > limit_y:
            return -1

        px = min(subgrid_count - 1, (px - align_x) // subgrid)
        py = min(subgrid_count - 1, subgrid_count - 1 - max(0, (py - align_y) // subgrid))

        if px % tile_grid == 0 or py % tile_grid == 0:
            return -1

        else:
            bx = px // tile_grid
            by = py // tile_grid
            return by * tile_count + bx

    fingers = {}
    last_held = set()
    redraw = True

    mouse_state = False

    while True:
        live = True

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                live = False

            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                live = False

            elif event.type == pygame.FINGERDOWN or event.type == pygame.FINGERMOTION:
                index = find_button(round(event.x * (screen_w - 1)), round(event.y * (screen_h - 1)))
                if index > -1:
                    fingers[event.finger_id] = index
                elif fingers.get(event.finger_id) is not None:
                    del fingers[event.finger_id]

            elif event.type == pygame.FINGERUP:
                if fingers.get(event.finger_id) is not None:
                    del fingers[event.finger_id]

            elif event.type == pygame.MOUSEBUTTONDOWN:
                mouse_state = True
                finger_id = "m"
                index = find_button(*event.pos)
                if index > -1:
                    fingers[finger_id] = index
                elif fingers.get(finger_id) is not None:
                    del fingers[finger_id]

            elif event.type == pygame.MOUSEMOTION and mouse_state:
                finger_id = "m"
                index = find_button(*event.pos)
                if index > -1:
                    fingers[finger_id] = index
                elif fingers.get(finger_id) is not None:
                    del fingers[finger_id]

            elif event.type == pygame.MOUSEBUTTONUP:
                mouse_state = False
                finger_id = "m"
                if fingers.get(finger_id) is not None:
                    del fingers[finger_id]

        if not live:
            for index in last_held:
                event = NoteOffEvent(note=index, velocity=0)
                client.event_output(event, port=port)
            client.drain_output()
            break

        held = set(fingers.values())

        released = last_held - held
        pressed = held - last_held
        updates = None

        if released or pressed:
            redraw = True

            for index in released:
                event = NoteOffEvent(note=buttons[index][1], velocity=0)
                client.event_output(event, port=port)

            for index in pressed:
                event = NoteOnEvent(note=buttons[index][1], velocity=96)
                client.event_output(event, port=port)

            client.drain_output()
            last_held = held

            updates = released ^ pressed

        if redraw:
            redraw = False

            for index in updates or range(len(buttons)):
                rect, note = buttons[index]
                sprite = note_sprites[note][index in held]
                screen.blit(sprite, rect)

            if updates:
                pygame.display.update([buttons[i][0] for i in updates])
            else:
                pygame.display.flip()

        else:
            time.sleep(1e-9)

    pygame.quit()

finally:
    print("shutting down")
    if timidity_proc:
        timidity_proc.kill()
        time.sleep(1)
