
import rtmidi
# See https://github.com/SpotlightKid/python-rtmidi
# and https://learn.sparkfun.com/tutorials/midi-tutorial/all#messages


midiout = rtmidi.MidiOut()


def note_on(note, velocity, channel=0):
    """
    Args `note` and `velocity` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    midiout.send_message([0x90 | 0xF & channel, note, velocity])


def note_off(note, velocity=0, channel=0):
    """
    Args `note` and `velocity` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    midiout.send_message([0x80 | 0xF & channel, note, velocity])


def polyphonic_pressure(note, pressure, channel=0):
    """
    Args `note` and `pressure` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    midiout.send_message([0xA0 | 0xF & channel, note, pressure])


def control_change(controller_number, value, channel=0):
    """
    Args `controller_number` and `value` are integers between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    midiout.send_message([0xB0 | 0xF & channel, controller_number, value])


def program_change(program_number, channel=0):
    """
    Arg `program_number` is an integer between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    midiout.send_message([0xC0 | 0xF & channel, program_number])


def channel_pressure(pressure, channel=0):
    """
    Arg `pressure` is an integer between 0 and 127 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """
    midiout.send_message([0xC0 | 0xF & channel, pressure])


def pitch_bend(bend, channel=0):
    """
    Arg `bend` is a float between -1.0 and 1.0 inclusive.
    Arg 'channel' is an integer between 0 and 15 inclusive.
    """

    # Educated guess based on what worked for the linux_midi backend.
    bend = int(bind)
    if bend < 0:
        bend = 0x3FFF - max(-1, abs(bend)) * 0x1FFF
    elif bend > 0:
        bend = min(bend, 1) * 0x1FFF

    midiout.send_message([0xC0 | 0xF & channel, bend & 0x7f, (bend >> 7) & 0x7f])


def flush():
    # Unused in this backend.
    pass


def print_verbose_device_info():
    available_ports = midiout.get_ports()
    for index, name in enumerate(available_ports):
        print(f"{index}: {name}")


def device_names():
    return midiout.get_ports()


def auto_connect_inner(device_priority):
    available_ports = midiout.get_ports()
    for target in device_priority:
        for index, name in enumerate(available_ports):
            if name == target:
                midiout.open_port(index)
                return name
    try:
        midiout.open_virtual_port("MollyTime")
    except NotImplementedError:
        pass
    return None
