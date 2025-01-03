
import overrides

import time

from alsa_midi import SequencerClient, WRITE_PORT, READ_PORT, NoteOnEvent, NoteOffEvent, ProgramChangeEvent


octave_labels = (
    ("C"),
    ("C♯", "D♭"),
    ("D"),
    ("D♯", "E♭"),
    ("E"),
    ("F"),
    ("F♯", "G♭"),
    ("G"),
    ("G♯", "A♭"),
    ("A"),
    ("A♯", "B♭"),
    ("B"))

def simple_note_name(note, tie=0):
    octave = note // 12
    index = note % 12
    return f"{octave_labels[index][tie]}{octave - 1}"


client = SequencerClient("fnordboard")
port = client.create_port(
    "output",
    caps=READ_PORT)

timidity_proc = None
connection = None
device_priority = ["Arturia MicroFreak", "EP-1320", "MiniFuse 2 MIDI 1", "TiMidity"]


def program_change(channel, program):
    event = ProgramChangeEvent(channel=channel, value=program)
    client.event_output(event, port=port)


def note_on(note, velocity):
    event = NoteOnEvent(note=note, velocity=velocity)
    client.event_output(event, port=port)


def note_off(note):
    event = NoteOffEvent(note=note, velocity=0)
    client.event_output(event, port=port)


def flush():
    client.drain_output()


def run(main_thunk):
    global connection
    global timidity_proc

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

    if connection == "TiMidity":
        # programs 21, 53, 91, 93, 94, 95, and 97 all work pretty well here
        event = ProgramChangeEvent(channel=0, value=95)
        client.event_output(event, port=port)

    time.sleep(1)
    if not connection:
        print(f"Unable to connect midi output :(")

    if connection == "TiMidity":
        client.drain_output()

    try:
        main_thunk()

    finally:
        if timidity_proc:
            print("halting timidity subprocess")
            timidity_proc.kill()
            time.sleep(1)
            print("done")
