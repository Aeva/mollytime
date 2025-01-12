
from alsa_midi import SequencerClient, WRITE_PORT, READ_PORT
from alsa_midi import NoteOnEvent, NoteOffEvent, KeyPressureEvent, ControlChangeEvent
from alsa_midi import ProgramChangeEvent, ChannelPressureEvent, PitchBendEvent
from alsa_midi.port import PortCaps, PortType


client = SequencerClient("MollyTime")
port = client.create_port(
    "output",
    caps=READ_PORT,
    type=PortType.APPLICATION | PortType.SOFTWARE)


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


def print_verbose_device_info():
    #devices = client.list_ports()
    devices = client.list_ports(output=True)
    for device in devices:
        print("-" * 79)
        print((device.name, device.client_name))

        for key in dir(device):
            if key.startswith("_") or key in ("capability", "type", "name", "client_name"):
                continue
            print(f" - {key}: {getattr(device, key, '')}")

        print(" - capability:")
        for flag in PortCaps:
            if (device.capability & flag.value) == flag.value:
                print(f"    - {flag.name}")

        print(" - type:")
        for flag in PortType:
            if (device.type & flag.value) == flag.value:
                print(f"    - {flag.name}")


def auto_connect_inner(device_priority):
    for target in device_priority:
        for device in client.list_ports(output=True):
            if device.client_name == target:
                port.connect_to(device)
                return device.client_name

            elif device.name == target:
                port.connect_to(device)
                return device.name
    return None
