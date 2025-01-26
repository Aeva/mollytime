
import midi
import time
import random


def thunk():
    midi.rt_start()

    # At this time of writing, I have my MicroFreak set to channel 3.
    # CC 92 controls the tempo for the MicroFreak.  The MIDI Control Center says this is
    # "Tremolo Depth" (which is the generic name for the control), but this very definitely
    # controls the MicroFreak's tempo.  The values range from 0 to 127, with 0 being 1/1 and
    # 127 being 1/32.  The MIDI Control Center says 0x26 corresponds to 1/4.
    midi.control_change(92, 0x26, channel=3)

    midi.flush()

    try:
        note = 0
        paused = False

        tempo = [120, 120, 60, -120, 240, 240]

        while True:
            bpm = tempo[note]
            next_bpm = tempo[(note + 1) % len(tempo)]
            interval = 60 / (abs(bpm) * 24)

            if bpm > 0 and paused:
                midi.rt_continue()
                paused = False

            for c in range(24):
                midi.rt_clock()
                midi.flush()

                if c == 23 and next_bpm < 0:
                    midi.rt_stop()
                    midi.flush()
                    paused = True

                time.sleep(interval)

            note = (note + 1) % len(tempo)

    except KeyboardInterrupt:
        midi.rt_stop()
        midi.flush()

midi.run(thunk)
