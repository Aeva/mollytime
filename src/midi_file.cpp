
#include "midi_file.h"

#include <fmt/format.h>

#include <cstdint>
#include <fstream>
#include <vector>
#include <map>


std::string ReadString(std::vector<char>& FileBytes, size_t& Cursor, size_t Count)
{
	const char* SliceStart = FileBytes.data() + Cursor;
	Cursor += sizeof(char) * Count;

	return std::string(SliceStart, Count);
}


template<typename T>
T ReadTWord(std::vector<char>& FileBytes, size_t& Cursor)
{
	std::uint8_t* SliceStart = (std::uint8_t*)(FileBytes.data() + Cursor);
	Cursor += sizeof(T);

	T Parsed = 0;
	for (int i = 0; i < sizeof(T); ++i)
	{
		T Byte = (*(SliceStart + i));
		Parsed = (Parsed << 8) | Byte;
	}

	return Parsed;
}


std::uint32_t ReadVLQ(std::vector<char>& FileBytes, size_t& Cursor)
{
	std::uint8_t* SliceStart = (std::uint8_t*)(FileBytes.data() + Cursor);

	std::uint32_t Parsed = 0;
	for (int i = 0; i < sizeof(std::uint32_t); ++i)
	{
		++Cursor;
		std::uint8_t Byte = (*(SliceStart + i));
		Parsed = (Parsed << 8) | (Byte & 0x7F);
		if ((Byte & 0x80) == 0)
		{
			break;
		}
	}

	return Parsed;
}


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

	std::map<std::string, std::vector<char>> FileChunks;

	std::uint16_t TrackFormat = -1;
	std::uint16_t TrackCount = 0;

	bool UseSMPTE = false;
	std::uint16_t TicksPerQuarterNote = 0;
	std::uint16_t TimeCodeFormat = 0;
	std::uint16_t TicksPerFrame = 0;

	size_t Cursor = 0;
	while (Cursor < FileBytes.size())
	{
		std::string Chunk = ReadString(FileBytes, Cursor, 4);
		std::uint32_t Size = ReadTWord<std::uint32_t>(FileBytes, Cursor);

		if (Chunk == "MThd")
		{
			size_t ChunkCursor = Cursor;
			TrackFormat = ReadTWord<std::uint16_t>(FileBytes, ChunkCursor);
			TrackCount = ReadTWord<std::uint16_t>(FileBytes, ChunkCursor);

			std::uint16_t Division = ReadTWord<std::uint16_t>(FileBytes, ChunkCursor);

			fmt::print(" + MThd:\n");
			fmt::print("   - Format: {}\n", TrackFormat);
			fmt::print("   - Tracks: {}\n", TrackCount);

			UseSMPTE = (Division & 0x4000) == 0x4000;
			if (!UseSMPTE)
			{
				TicksPerQuarterNote = Division;
				fmt::print("   - Ticks per quarter note: {}\n", TicksPerQuarterNote);
			}
			else
			{
				TimeCodeFormat = (Division & 0x3F00) >> 8;
				TicksPerFrame = (Division & 0xFF);

				fmt::print("   - Time code format: {}\n", TimeCodeFormat);
				fmt::print("   - Ticks per frame: {}\n", TicksPerFrame);
			}

			break;
		}

		Cursor += Size;
	}

	std::uint8_t LastStatusByte = 0;

	Cursor = 0;
	while (Cursor < FileBytes.size())
	{
		std::string Chunk = ReadString(FileBytes, Cursor, 4);
		std::uint32_t Size = ReadTWord<std::uint32_t>(FileBytes, Cursor);

		if (Chunk == "MTrk")
		{
			fmt::print(" - {} {}\n", Chunk, Size);
			size_t TrackCursor = Cursor;
			size_t TrackEnd = Cursor + Size;

			while (TrackCursor < TrackEnd)
			{
				std::uint32_t DeltaTime = ReadVLQ(FileBytes, TrackCursor);

				std::uint8_t NextByte = ReadTWord<std::uint8_t>(FileBytes, TrackCursor);
				std::uint8_t StatusByte;
				if ((NextByte & 0x80) == 0x80)
				{
					StatusByte = NextByte;
				}
				else
				{
					StatusByte = LastStatusByte;
					--TrackCursor;
				}

				bool SystemMessage = false;
				std::uint8_t DataBytes[2] = {0, 0};
				switch(StatusByte & 0xF0)
				{
				case 0x80:
				case 0x90:
				case 0xA0:
				case 0xB0:
				case 0xE0:
					LastStatusByte = StatusByte;
					DataBytes[0] = ReadTWord<std::uint8_t>(FileBytes, TrackCursor);
					DataBytes[1] = ReadTWord<std::uint8_t>(FileBytes, TrackCursor);
					break;

				case 0xC0:
				case 0xD0:
					LastStatusByte = StatusByte;
					DataBytes[0] = ReadTWord<std::uint8_t>(FileBytes, TrackCursor);
					break;

				case 0xF1:
				case 0xF3:
					SystemMessage = true;
					DataBytes[0] = ReadTWord<std::uint8_t>(FileBytes, TrackCursor);
					break;

				case 0xF2:
					SystemMessage = true;
					DataBytes[0] = ReadTWord<std::uint8_t>(FileBytes, TrackCursor);
					DataBytes[1] = ReadTWord<std::uint8_t>(FileBytes, TrackCursor);
					break;

				case 0xF4:
				case 0xF5:
				case 0xF6:
					SystemMessage = true;
					break;

				case 0xF8:
				case 0xF9:
				case 0xFA:
				case 0xFB:
				case 0xFC:
				case 0xFD:
					// system realtime messages
					SystemMessage = true;
					break;

				case 0xF0:
				case 0xF7:
					SystemMessage = true;
					TrackCursor += ReadVLQ(FileBytes, TrackCursor);

					break;
				}
			}
		}

		Cursor += Size;
	}
}
