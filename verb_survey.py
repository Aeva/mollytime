
import sys
import glob
from xml.etree import ElementTree
from mollytime.mollytime import OpCode


def verb_survey():
    names = [e.name.lower() for e in OpCode if e.name != "Count"]
    paths = glob.glob("examples/**/*.beep") + glob.glob("examples/*.beep")
    counts = {}
    pad_to = 0
    for name in names:
        counts[name] = 0
        pad_to = max(pad_to, len(name))

    markov = {}
    vokram = {}

    for path in paths:
        try:
            tree = ElementTree.parse(path)
        except:
            print(f'Unable to open file "{path}", because it is not a supported file type or it is malformed.')
            continue

        root = tree.getroot()
        if not root.tag == "mollytime":
            print(f'{path} is some random XML file?  Moving on.')
            continue

        tiles = {}
        wires = []

        visited_patch = False
        for root_child in root:
            if root_child.tag == "patch" and not visited_patch:
                visited_patch = True
                next_index = 1
                for patch_child in root_child:
                    if patch_child.tag == "tile":
                        tile_id = int(patch_child.attrib["id"])
                        symbol = patch_child.attrib["symbol"]
                        tiles[tile_id] = symbol
                        counts[symbol] += 1
                        #if symbol == "const":
                            #value = float(patch_child.attrib["value"])

                    if patch_child.tag == "wire":
                        src_tile = int(patch_child.attrib["from"].split(":")[0])
                        dst_tile = int(patch_child.attrib["to"].split(":")[0])
                        wires.append((src_tile, dst_tile))
        for src_tile, dst_tile in wires:
            src_symbol = tiles[src_tile]
            dst_symbol = tiles[dst_tile]

            markov.setdefault(src_symbol, {}).setdefault(dst_symbol, 0)
            markov[src_symbol][dst_symbol] += 1

            vokram.setdefault(dst_symbol, {}).setdefault(src_symbol, 0)
            vokram[dst_symbol][src_symbol] += 1

    queries = zip(sys.argv[1::2], sys.argv[2::2])

    for mode, opcode in queries:
        key = opcode.lower()
        if opcode in counts:
            if mode == "from":
                table = markov[key]
                print(f"Everything {opcode} ever connects to:")
            elif mode == "to":
                print(f"Everything that connects to {opcode}:")
                table = vokram[key]
            else:
                print(f"Invalid query mode: {mode}")
                continue

            for count, name in sorted([(count, name) for name, count in table.items()])[::-1]:
                pad = " " * (pad_to - len(name))
                print(f"{pad + name} : {count}")
        else:
            print(f"Ignoring query for unknown opcode: {opcode}")

    if len(sys.argv) == 1:
        print(f"There are currently {len(counts.keys())} different OpCodes.\n")
        print(f"OpCode Instance Counts:")

        for count, name in sorted([(count, name) for name, count in counts.items()])[::-1]:
            pad = " " * (pad_to - len(name))
            print(f"{pad + name} : {count}")


if __name__ == "__main__":
    verb_survey()
