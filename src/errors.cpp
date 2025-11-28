
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

#include "errors.h"

#include <stdexcept>
#include <string>

#ifdef ENABLE_STACK_TRACES
#include <sstream>

// Requires package `boost-stacktrace` on Fedora
// see also: https://www.boost.org/doc/libs/1_88_0/doc/html/stacktrace.html
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma warning(push)
#pragma warning(disable : 4668 5039)
#include <boost/stacktrace.hpp>
#pragma warning(pop)
#pragma clang diagnostic pop
#endif


void TraceBack()
{
#ifdef ENABLE_STACK_TRACES
	std::stringstream TraceBackBuffer;
	TraceBackBuffer << "thrown by C++\n\nC++ traceback (most recent call first):\n" << boost::stacktrace::stacktrace() << "\n";
	std::string ErrorString = TraceBackBuffer.str();
#else
	std::string ErrorString = "thrown by C++\n\nC++ traceback unavailable.\n";
#endif
	throw std::runtime_error(ErrorString);
}
