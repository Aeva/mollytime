
import midi
import time
import random


def thunk():
    midi.rt_start()
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
