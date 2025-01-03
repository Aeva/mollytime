
import midi
from tiles import Piano, Instrument


if __name__ == "__main__":
    root = 60

    scale = [2, 1, 2, 2, 2, 1, 2]
    notes = 12 * 2 + 1

    plates = [
        Piano(0, 0, root - 12, scale, notes),
        Piano(0, 9, root, scale, notes),
        Piano(0, 18, root + 12, scale, notes)
    ]
    keyboards = Instrument(plates)
    midi.run(keyboards)
