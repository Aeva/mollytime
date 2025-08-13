
import os
import time
import platform
operating_system = platform.system()

if operating_system == "Linux":
    from linux_midi import *

else:
    from generic_midi import *

import xml.etree.ElementTree as etree


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
    settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings.xml")
    tree = etree.parse(settings_path)
    settings = tree.getroot()

    device_priority = []
    for category in settings:
        if category.tag == "autoconnect":
            for device in category:
                if device.tag == "device":
                    target_os = device.attrib.get("os", operating_system)
                    if target_os != operating_system:
                        continue
                    device_priority.append(device.text.strip())

    return auto_connect_inner(device_priority)


def run(main_thunk):
    #print_verbose_device_info()
    if connection := auto_connect():
        print(f"Connected to {connection}")

    main_thunk()
