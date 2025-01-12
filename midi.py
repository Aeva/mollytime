

import time
import platform
operating_system = platform.system()


if operating_system == "Linux":
    from linux_midi import *

else:
    from generic_midi import *


device_priority = [
    "VCV Rack",
    "Arturia MicroFreak",
    "EP-1320",
    "MiniFuse 2",
    "TiMidity",
    "Microsoft GS Wavetable Synth 0"]


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


def auto_connect():
    return auto_connect_inner(device_priority)


def run(main_thunk):
    #print_verbose_device_info()
    if connection := auto_connect():
        print(f"Connected to {connection}")

    main_thunk()
