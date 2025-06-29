
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


struct TileInfo
{
	std::vector<OpCode> Combiners;
	std::vector<std::vector<std::string>> InputEdges;
	std::vector<std::vector<std::string>> OutputEdges;

	TileInfo()
	{
		Combiners.resize((int)OpCode::Count);
		InputEdges.resize((int)OpCode::Count);
		OutputEdges.resize((int)OpCode::Count);

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
		InputEdges[(int)Key] = Inputs;
		OutputEdges[(int)Key] = Outputs;
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


const std::vector<std::string>& MagicTile::Inputs()
{
	return TileInfoMap.InputEdges[(int)Symbol];
}


const std::vector<std::string>& MagicTile::Outputs()
{
	return TileInfoMap.OutputEdges[(int)Symbol];
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
