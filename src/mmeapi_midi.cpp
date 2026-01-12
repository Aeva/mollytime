
// Copyright 2025 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef MIDI_MMEAPI

#include "mmeapi_midi.h"

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <windows.h>
#include <mmeapi.h>
#undef NOMINMAX
#undef VC_EXTRALEAN
#undef WIN32_LEAN_AND_MEAN

#include <map>
#include <mutex>
#include <print>


static std::map<int, HMIDIIN> LiveInputs;


static void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	MmeApiMidiDriver* Backend = (MmeApiMidiDriver*)dwInstance;
	if (dwParam1 != 0)
	{
		const WORD LowWord = LOWORD(dwParam1);
		const WORD HighWord = HIWORD(dwParam1);
		const BYTE StatusByte = LOBYTE(LowWord);

		if (StatusByte > 0)
		{
			uint32_t Message = StatusByte >> 4;
			uint32_t Channel = StatusByte & 0xF;
			uint32_t Param1 = HIBYTE(LowWord);
			uint32_t Param2 = LOBYTE(HighWord);

			uint32_t Packet = (Message << 24) | (Channel << 16) | (Param1 << 8) | Param2;
			Backend->NewMidiInputPacket(Packet);
		}
	}
}


void MmeApiMidiDriver::CloseInputPort(int Port)
{
	auto Found = LiveInputs.find(Port);
	if (Found != LiveInputs.end())
	{
		HMIDIIN Handle = Found->second;
		midiInStop(Handle);
		midiInClose(Handle);
		LiveInputs.erase(Found);
	}
}


bool MmeApiMidiDriver::OpenInputPort(int Port)
{
	CloseInputPort(Port);
	HMIDIIN& Handle = LiveInputs[Port];

	MMRESULT Result = midiInOpen(&Handle, Port, (DWORD_PTR)MidiInProc, (DWORD_PTR)this, CALLBACK_FUNCTION);
	if (Result == MMSYSERR_NOERROR)
	{
		Result = midiInStart(Handle);
	}

	if (Result != MMSYSERR_NOERROR)
	{
		CloseInputPort(Port);
	}
	return Result == MMSYSERR_NOERROR;
}


MmeApiMidiDriver::MmeApiMidiDriver()
{
	bool Connected = false;
	if (midiInGetNumDevs() > 0)
	{
		int Port = 0;
		Connected = OpenInputPort(Port);
	}
	if (!Connected)
	{
		std::print("No MIDI input device found!\n");
	}
}


MmeApiMidiDriver::~MmeApiMidiDriver()
{
	for (const auto& [Port, _] : LiveInputs)
	{
		CloseInputPort(Port);
	}
}


void MmeApiMidiDriver::NewMidiInputPacket(uint32_t Packet)
{
	TRACEABLE_LOCK_GUARD(PendingPacketsCrit);
	PendingPackets.push_back(Packet);
}


void MmeApiMidiDriver::ProcessEvents(MidiHandler* Handler)
{
	TRACEABLE_LOCK_GUARD(PendingPacketsCrit);
	for (uint32_t Packet : PendingPackets)
	{
		uint8_t Message = uint8_t(Packet >> 24);
		uint8_t Channel = uint8_t(Packet >> 16);
		uint8_t Param1 = uint8_t(Packet >> 8);
		uint8_t Param2 = uint8_t(Packet);

		if (Message == 0x8)
		{
			// Note Off
			Handler->NoteOff(Param1, Channel);
		}
		else if (Message == 0x9)
		{
			// Note On
			Handler->NoteOn(Param1, Param2, Channel);
		}
		else if (Message == 0xA)
		{
			// Polyphonic Pressure
			Handler->NotePressure(Param1, Param2, Channel);
		}
		else if (Message == 0xB)
		{
			// Control Change
			Handler->ControlChange7Bit(Param1, Param2, Channel);
		}
		else if (Message == 0xC)
		{
			// Program Change
			Handler->ProgramChange(Param1, Channel);
		}
		else if (Message == 0xD)
		{
			// Channel Pressure
			Handler->ChannelPressure(Param1, Channel);
		}
		else if (Message == 0xE)
		{
			// Pitch Bend
			int16_t Value = Param1 | (Param2 << 7);
			constexpr int16_t Split = 0x2000;
			int16_t Divisor = (Value < Split) ? Split : (Split - 1);
			AudioSample Bend = AudioSample(Value - Split) / AudioSample(Divisor);
			Handler->PitchBend(Bend, Channel);
		}
	}
	PendingPackets.clear();
}

#endif
