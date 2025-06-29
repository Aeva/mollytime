
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

#include <cstdint>
#include <string>
#include <vector>
#include <tuple>


enum class OpCode : uint32_t
{
	CONST = 0,
	OUT,
	SIN,
	ADD,
	MUL,
	MIN,
	MAX,
	Count
};


// Tile Id, port index
using WireKey = std::tuple<uint32_t, uint32_t>;


uint64_t WireKeyAsNumber(WireKey Key);
WireKey WireKeyFromNumber(uint64_t Number);


struct MagicTile
{
	MagicTile(OpCode InSymbol, std::string InName);

	const uint32_t Id;
	const OpCode Symbol;

	std::string Name;

	OpCode Combiner();
	const std::vector<std::string>& InputNames();
	const std::vector<std::string>& OutputNames();
	std::vector<uint64_t> InputKeys();
	std::vector<uint64_t> OutputKeys();
	virtual std::string Hint();
	virtual std::string Label();
	virtual ~MagicTile() = default;
};


struct ConstTile : public MagicTile
{
	ConstTile(float InValue);
	float Value;

	virtual std::string Hint() override;
	virtual std::string Label() override;
};


struct OutTile : public MagicTile
{
	OutTile();
};


struct SinTile : public MagicTile
{
	SinTile();
};


struct AddTile : public MagicTile
{
	AddTile();
};


struct MulTile : public MagicTile
{
	MulTile();
};


struct MinTile : public MagicTile
{
	MinTile();
};


struct MaxTile : public MagicTile
{
	MaxTile();
};
