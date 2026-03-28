
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

#include <fmt/format.h>

#include "config.h"

static std::filesystem::path HomeFolder;
static std::filesystem::path ConfigFolder;
static std::filesystem::path GameConfigFolder;
static bool ReadOnlyMode = true;


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
            char* Found = std::getenv(HomeVar);
            if (Found)
            {
                std::filesystem::path MaybeHomeFolder = Found;
                if (!MaybeHomeFolder.empty() && MaybeHomeFolder.is_absolute() && std::filesystem::exists(MaybeHomeFolder))
                {
                    HomeFolder = MaybeHomeFolder;
                    break;
                }
            }
        }

        if (HomeFolder.empty())
        {
            HomeFolder = std::filesystem::current_path();
            fmt::print(
                "WARNING: Unable to determine the user home folder from standard environment vars!\n"
                "\tThe current working directory will be used instead: {}\n",
                HomeFolder.c_str());
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
            char* Found = std::getenv(ConfigVar);
            if (Found)
            {
                std::filesystem::path MaybeConfigFolder = Found;
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

    if (!std::filesystem::exists(GameConfigFolder))
    {
        if (!std::filesystem::create_directory(GameConfigFolder))
        {
            fmt::print("WARNING: Unable create config folder, settings will not be saved: {}\n", GameConfigFolder.c_str());
        }
    }

    ReadOnlyMode = !std::filesystem::exists(GameConfigFolder);
}

