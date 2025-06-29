
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

#include <format>
#include <vector>
#include "tiles.h"


uint32_t WireKeyTileId(WireKey Key)
{
	return std::get<0>(Key);
}


uint32_t WireKeyPort(WireKey Key)
{
	return std::get<1>(Key);
}


uint64_t WireKeyAsNumber(WireKey Key)
{
	return (uint64_t(WireKeyTileId(Key)) << 32) | uint64_t(WireKeyPort(Key));
}


WireKey WireKeyFromNumber(uint64_t Number)
{
	uint32_t TileId = uint32_t(Number >> 32);
	uint32_t Port = uint32_t(Number & 0xFFFFFFFF);
	return WireKey(TileId, Port);
}


struct TileInfo
{
	std::vector<OpCode> Combiners;
	std::vector<std::vector<std::string>> InputNames;
	std::vector<std::vector<std::string>> OutputNames;

	TileInfo()
	{
		Combiners.resize((int)OpCode::Count);
		InputNames.resize((int)OpCode::Count);
		OutputNames.resize((int)OpCode::Count);

		Set(OpCode::CONST, OpCode::ADD, {}, {"#"});
		Set(OpCode::OUT, OpCode::ADD, {"out"}, {});
		Set(OpCode::SIN, OpCode::ADD, {"hz"}, {"amp"});
		Set(OpCode::ADD, OpCode::ADD, {"+"}, {"="});
		Set(OpCode::MUL, OpCode::MUL, {"*"}, {"="});
		Set(OpCode::MIN, OpCode::MIN, {"min"}, {"="});
		Set(OpCode::MAX, OpCode::MAX, {"max"}, {"="});
	}

	void Set(OpCode Key, OpCode Combiner, std::vector<std::string> Inputs, std::vector<std::string> Outputs)
	{
		Combiners[(int)Key] = Combiner;
		InputNames[(int)Key] = Inputs;
		OutputNames[(int)Key] = Outputs;
	}
};

const TileInfo TileInfoMap;


static uint32_t AssignTileId()
{
	static uint32_t NextTileId = 0;
	return ++NextTileId;
}


MagicTile::MagicTile(OpCode InSymbol, std::string InName)
	: Id(AssignTileId())
	, Symbol(InSymbol)
	, Name(InName)
{
}


OpCode MagicTile::Combiner()
{
	return TileInfoMap.Combiners[(int)Symbol];
}


const std::vector<std::string>& MagicTile::InputNames()
{
	return TileInfoMap.InputNames[(int)Symbol];
}


const std::vector<std::string>& MagicTile::OutputNames()
{
	return TileInfoMap.OutputNames[(int)Symbol];
}


std::vector<uint64_t> MagicTile::InputKeys()
{
	int Count = InputNames().size();
	std::vector<uint64_t> Keys;
	Keys.reserve(Count);
	uint64_t Key = WireKeyAsNumber({Id, 0});
	for (int Port = 0; Port < Count; ++Port)
	{
		Keys.push_back(Key++);
	}
	return Keys;
}


std::vector<uint64_t> MagicTile::OutputKeys()
{
	int Count = OutputNames().size();
	std::vector<uint64_t> Keys;
	Keys.reserve(Count);
	uint64_t Key = WireKeyAsNumber({Id, 0});
	for (int Port = 0; Port < Count; ++Port)
	{
		Keys.push_back(Key++);
	}
	return Keys;
}


std::string MagicTile::Hint()
{
	return std::format("<tile {}: \'{}\'>", Id, Name);
}


std::string MagicTile::Label()
{
	return Name;
}


ConstTile::ConstTile(float InValue)
	: MagicTile(OpCode::CONST, "#")
	, Value(InValue)
{
}


std::string ConstTile::Hint()
{
	return std::format("<tile {}: const {}>", Id, Value);
}


std::string ConstTile::Label()
{
	return std::format("{}", Value);
}


OutTile::OutTile()
	: MagicTile(OpCode::OUT, "out")
{
}


SinTile::SinTile()
	: MagicTile(OpCode::SIN, "sin")
{
}


AddTile::AddTile()
	: MagicTile(OpCode::ADD, "add")
{
}


MulTile::MulTile()
	: MagicTile(OpCode::MUL, "mul")
{
}


MinTile::MinTile()
	: MagicTile(OpCode::MIN, "min")
{
}


MaxTile::MaxTile()
	: MagicTile(OpCode::MAX, "max")
{
}
