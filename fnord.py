
import time
import pygame
from alsa_midi import SequencerClient, WRITE_PORT, READ_PORT, NoteOnEvent, NoteOffEvent, ProgramChangeEvent


client = SequencerClient("fnordboard")
port = client.create_port(
    "output",
    caps=READ_PORT)

timidity_proc = None
connection = None
device_priority = ["Arturia MicroFreak", "MiniFuse 2 MIDI 1", "TiMidity"]

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
    pygame.init()

    sizes = pygame.display.get_desktop_sizes()
    display_index = len(sizes) - 1
    screen = pygame.display.set_mode(size=sizes[display_index], display=display_index, flags=pygame.FULLSCREEN)

    screen_w, screen_h = sizes[display_index]

    play_span = min(screen_w, screen_h)

    tile_count = 9
    subgrid_count = tile_count * 2 + tile_count + 1
    subgrid = int(play_span / subgrid_count)

    align_x = int((play_span - (subgrid * subgrid_count)) * .5)
    align_y = align_x + screen_h - play_span

    buttons = list(range(tile_count * tile_count))
    for index in buttons:
        y = tile_count - 1 - (index // tile_count)
        x = index % tile_count

        rect = pygame.Rect(
            align_x + subgrid * (x * 3 + 1),
            align_y + subgrid * (y * 3 + 1),
            subgrid * 2,
            subgrid * 2)

        r = min(max((x + 1) / tile_count, 0), 1)
        g = min(max(1.0 - (y + 1) / tile_count, 0), 1)
        color = [r, g, .5]

        buttons[index] = [rect, color, False]

    def find_button(px, py):
        px = min(subgrid_count - 1, (px - align_x) // subgrid)
        py = min(subgrid_count - 1, subgrid_count - 1 - max(0, (py - align_y) // subgrid))
        if px % 3 == 0 or py % 3 == 0:
            return None
        else:
            bx = px // 3
            by = py // 3
            return by * tile_count + bx

    fingers = {}
    last_held = set()
    redraw = True

    while True:
        live = True

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                live = False

            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                live = False

            elif event.type == pygame.FINGERDOWN or event.type == pygame.FINGERMOTION:
                index = find_button(round(event.x * (screen_w - 1)), round(event.y * (screen_h - 1)))
                if index:
                    fingers[event.finger_id] = index
                elif fingers.get(event.finger_id) is not None:
                    del fingers[event.finger_id]

            elif event.type == pygame.FINGERUP:
                if fingers.get(event.finger_id) is not None:
                    del fingers[event.finger_id]

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
                event = NoteOffEvent(note=index, velocity=0)
                client.event_output(event, port=port)

            for index in pressed:
                event = NoteOnEvent(note=index, velocity=96)
                client.event_output(event, port=port)

            client.drain_output()
            last_held = held

            updates = [buttons[i][0] for i in (released ^ pressed)]

        if redraw:
            redraw = False
            screen.fill((0, 0, 0))
            for index, (rect, color, state) in enumerate(buttons):
                if index in held:
                    color = (255, 255, 255)
                else:
                    color = [int(c * 255) for c in color]
                pygame.draw.rect(screen, color, rect)

            if updates:
                pygame.display.update(updates)
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
