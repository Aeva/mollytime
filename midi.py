

import time

from alsa_midi import SequencerClient, WRITE_PORT, READ_PORT
from alsa_midi import NoteOnEvent, NoteOffEvent, KeyPressureEvent, ControlChangeEvent
from alsa_midi import ProgramChangeEvent, ChannelPressureEvent, PitchBendEvent


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


client = SequencerClient("MollyTime")
port = client.create_port(
    "output",
    caps=READ_PORT)

timidity_proc = None
connection = None
device_priority = ["Arturia MicroFreak", "EP-1320", "VCV Rack input", "MiniFuse 2 MIDI 1", "TiMidity"]


def note_on(note, velocity, channel=0):
    """
    Args `note` and `velocity` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    event = NoteOnEvent(note, channel, velocity)
    client.event_output(event, port=port)


def note_off(note, velocity=0, channel=0):
    """
    Args `note` and `velocity` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    event = NoteOffEvent(note, channel, velocity)
    client.event_output(event, port=port)


def polyphonic_pressure(note, pressure, channel=0):
    """
    Args `note` and `pressure` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    event = KeyPressureEvent(note, channel, pressure)
    client.event_output(event, port=port)


def control_change(controller_number, value, channel=0):
    """
    Args `controller_number` and `value` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    event = ControlChangeEvent(channel, controller_number, value)
    client.event_output(event, port=port)


def program_change(program_number, channel=0):
    """
    Arg `program_number` is an integer between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    event = ProgramChangeEvent(channel, program_number)
    client.event_output(event, port=port)


def channel_pressure(pressure, channel=0):
    """
    Arg `pressure` is an integer between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    event = ChannelPressureEvent(channel, pressure)
    client.event_output(event, port=port)


def pitch_bend(bend, channel=0):
    """
    Arg `bend` is a float between -1.0 and 1.0 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """

    if bend < 0:
        bend = 0x3FFF - max(-1, abs(bend)) * 0x1FFF
    elif bend > 0:
        bend = min(bend, 1) * 0x1FFF

    event = PitchBendEvent(channel, int(bend))

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
        if client.list_ports(output=True):
            print("MIDI devices on system:")
            for device in client.list_ports(output=True):
                print(f" - {(device.client_name, device.name)}")

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
