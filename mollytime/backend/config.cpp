
// Copyright 2026 Aeva Palecek
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

#include <filesystem>
#include <cstdlib>
#include <cassert>
#include <string>

#include <fmt/format.h>

#include "config.h"

// Generic folders specified by the OS:
static std::filesystem::path HomeFolder;
static std::filesystem::path DataFolder;
static std::filesystem::path ConfigFolder;

// Game-specific folders within above:
static std::filesystem::path GameDataFolder;
static std::filesystem::path GameConfigFolder;

static bool ReadOnlyMode = true;


#if defined(_MSC_VER)
// Microsoft decided to deprecate std::getenv, which is not actually deprecated in any C++ standard,
// and worse, it'll refuse to compile if you use it, which appears to be a gross misunderstanding
// of what "deprecation" means.
#include <stdlib.h>
static void GetEnvironmentVar(const char* Name, std::filesystem::path& Value)
{
    char* Found;
    size_t NumberOfElements;
    _dupenv_s(&Found, &NumberOfElements, Name);
    if (Found != nullptr && NumberOfElements > 0)
    {
        Value = Found;
    }
    else
    {
        Value.clear();
    }
    free(Found);
}
#else
static void GetEnvironmentVar(const char* Name, std::filesystem::path& Value)
{
    char* Found = std::getenv(Name);
    if (Found != nullptr)
    {
        Value = Found;
    }
    else
    {
        Value.clear();
    }
}
#endif


void Config::Init(const char* ApplicationName)
{
    static bool Initialized = false;
    if (Initialized)
    {
        return;
    }
    Initialized = true;

    {
        const char* PossibleHomeVars[] = \
        {
            // Windows-specific vars, unlikely to be set on other operating systems:
            "UserProfile",
            "HomePath",

            // Expected for Unix and Unix-adjacent operating systems:
            "HOME",
        };

        for (const char* HomeVar : PossibleHomeVars)
        {
            std::filesystem::path MaybeHomeFolder;
            GetEnvironmentVar(HomeVar, MaybeHomeFolder);
            if (!MaybeHomeFolder.empty() && MaybeHomeFolder.is_absolute() && std::filesystem::exists(MaybeHomeFolder))
            {
                HomeFolder = MaybeHomeFolder;
                break;
            }
        }

        if (HomeFolder.empty())
        {
            HomeFolder = std::filesystem::current_path();
            fmt::print(
                "WARNING: Unable to determine the user home folder from standard environment vars!\n"
                "\tThe current working directory will be used instead: {}\n",
                HomeFolder.string());
        }
    }

    assert(!HomeFolder.empty());
    assert(HomeFolder.is_absolute());

    {
        const char* PossibleConfigVars[] = \
        {
            // Windows-specific vars, unlikely to be set on other operating systems:
            "LocalAppData",

            // Recommended by https://specifications.freedesktop.org/basedir/latest/,
            // but may be unset if the user prefers the default location:
            "XDG_CONFIG_HOME",
        };

        for (const char* ConfigVar : PossibleConfigVars)
        {
            std::filesystem::path MaybeConfigFolder;
            GetEnvironmentVar(ConfigVar, MaybeConfigFolder);

            if (!MaybeConfigFolder.empty())
            {
                if (MaybeConfigFolder.is_relative())
                {
                    MaybeConfigFolder = HomeFolder / MaybeConfigFolder;
                }
                if (MaybeConfigFolder.is_absolute() && std::filesystem::exists(MaybeConfigFolder))
                {
                    ConfigFolder = MaybeConfigFolder;
                    break;
                }
            }
        }

        if (ConfigFolder.empty())
        {
            // https://specifications.freedesktop.org/basedir/latest/ states this should be used if
            // XDG_CONFIG_HOME is unset, which is probably as good a default as anything.
            ConfigFolder = HomeFolder / ".config";
        }

        assert(!ConfigFolder.empty());
        if (!std::filesystem::exists(ConfigFolder))
        {
            ConfigFolder = HomeFolder;
            GameConfigFolder = ConfigFolder / fmt::format(".{}", ApplicationName);
        }
        else
        {
            GameConfigFolder = ConfigFolder / ApplicationName;
        }
    }

    assert(std::filesystem::exists(ConfigFolder));
    assert(ConfigFolder.is_absolute());

    {
        const char* PossibleDataVars[] = \
        {
            // Windows-specific vars, unlikely to be set on other operating systems:
            "LocalAppData",

            // Recommended by https://specifications.freedesktop.org/basedir/latest/,
            // but may be unset if the user prefers the default location:
            "XDG_DATA_HOME",
        };

        for (const char* DataVar : PossibleDataVars)
        {
            std::filesystem::path MaybeDataFolder;
            GetEnvironmentVar(DataVar, MaybeDataFolder);

            if (!MaybeDataFolder.empty())
            {
                if (MaybeDataFolder.is_relative())
                {
                    MaybeDataFolder = HomeFolder / MaybeDataFolder;
                }
                if (MaybeDataFolder.is_absolute() && std::filesystem::exists(MaybeDataFolder))
                {
                    DataFolder = MaybeDataFolder;
                    break;
                }
            }
        }

        if (DataFolder.empty())
        {
            // https://specifications.freedesktop.org/basedir/latest/ states this should be used if
            // XDG_DATA_HOME is unset, which is probably as good a default as anything.
            DataFolder = HomeFolder / ".local" / "share";
        }

        assert(!DataFolder.empty());
        if (!std::filesystem::exists(DataFolder))
        {
            DataFolder = HomeFolder;
            GameDataFolder = DataFolder / fmt::format(".{}", ApplicationName);
        }
        else
        {
            GameDataFolder = DataFolder / ApplicationName;
        }
    }

    if (!std::filesystem::exists(GameDataFolder))
    {
        if (!std::filesystem::create_directory(GameDataFolder))
        {
            fmt::print("WARNING: Unable create data folder, settings will not be saved: {}\n", GameDataFolder.string());
        }
    }

    if (!std::filesystem::exists(GameConfigFolder))
    {
        if (!std::filesystem::create_directory(GameConfigFolder))
        {
            fmt::print("WARNING: Unable create config folder, settings will not be saved: {}\n", GameConfigFolder.string());
        }
    }

    ReadOnlyMode = !(std::filesystem::exists(GameDataFolder) && std::filesystem::exists(GameConfigFolder));
}


std::string_view Config::GetGameDataFolder()
{
    // Windows paths use 16 bit characters >:(
    static std::string ThisIsInAvoidanceOfLongCompileTimesInOtherFiles;
    ThisIsInAvoidanceOfLongCompileTimesInOtherFiles = GameDataFolder.string();
    return ThisIsInAvoidanceOfLongCompileTimesInOtherFiles;
}


std::string_view Config::GetGameConfigFolder()
{
    // Windows paths use 16 bit characters >:(
    static std::string ThisIsInAvoidanceOfLongCompileTimesInOtherFiles;
    ThisIsInAvoidanceOfLongCompileTimesInOtherFiles = GameConfigFolder.string();
    return ThisIsInAvoidanceOfLongCompileTimesInOtherFiles;
}


bool Config::GetReadOnly()
{
    return ReadOnlyMode;
}
