
#include <fmt/format.h>
#include <fstream>
#include <vector>

#include "midi_file.h"


void ReadMidiFile(const std::string Path)
{
	std::vector<char> FileBytes;
	{
		std::ifstream File(Path, std::ios::binary);
		std::istreambuf_iterator<char> FileIterator{File}, FileEnd;
		FileBytes = std::vector<char>(FileIterator, FileEnd);
		File.close();
	}









	fmt::print("Opened {} ({} bytes)\n", Path, FileBytes.size());
}
