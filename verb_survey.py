
from mollytime.mollytime import OpCode
import glob
import re


def verb_survey():
	names = [e.name.lower() for e in OpCode if e.name != "Count"]
	paths = glob.glob("examples/**/*.beep") + glob.glob("examples/*.beep")
	counts = {}
	pad_to = 0
	for name in names:
		counts[name] = 0
		pad_to = max(pad_to, len(name))


	for path in paths:
		with open(path, "r") as raw:
			data = raw.read()
			found = re.findall(r'symbol=\"([a-zA-Z_]+)\"', data)
			for symbol in found:
				counts[symbol] += 1

	for count, name in sorted([(count, name) for name, count in counts.items()])[::-1]:
		pad = " " * (pad_to - len(name))
		print(f"{pad + name} : {count}")


if __name__ == "__main__":
	verb_survey()
