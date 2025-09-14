
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

#include <numbers>
#include <cmath>
#include <array>

#include "patch.h"
#include "moon.h"


// Approximate Moon math adapted from https://aa.quae.nl/en/reken/hemelpositie.html

// TODO: WIP stuff macro'd out to prevent compiler warning noise
#if 0
static constexpr double ToRadians = std::numbers::pi / 180.0;
static constexpr double ToDegrees = 180.0 / std::numbers::pi;


static void MoonPosition()
{
    // The number of days since noon (UTC) January 1st, 2000.
    const double D = 0.0;

    // Obliquity of the ecliptic as of D == 0.0, in degrees:
    const double Epsilon = 23.4397;

    // Calculate the Moon's geocentric ecliptic coordinates: https://aa.quae.nl/en/reken/hemelpositie.html#4

    // Mean geocentric ecliptic longitude, in degrees:
    const double L = std::fmod(218.316 + 13.176396 * D, 360.0);

    // Mean anomaly, in degrees:
    const double M = std::fmod(134.963 + 13.064993 * D, 360.0);

    // Mean distance of the Moon from its ascending node, in degrees:
    const double F = std::fmod(93.272 + 13.229350 * D, 360.0);

    // Geocentric ecliptic longitude, in degrees:
    const double Lambda = L + 6.289 * std::sin(L * ToRadians);

    // Geocentric ecliptic latitude, in degrees:
    const double Beta = 5.128 * std::sin(F * ToRadians);

    // Distance from the Earth, in kilometers
    const double Delta = 385001 - 20905 * std::cos(M * ToRadians);

    // Intermediaries:
    const double SinLambda = std::sin(Lambda * ToRadians);
    const double CosLambda = std::cos(Lambda * ToRadians);
    const double SinEpsilon = std::sin(Epsilon * ToRadians);
    const double CosEpsilon = std::cos(Epsilon * ToRadians);
    const double SinBeta = std::sin(Beta * ToRadians);
    const double CosBeta = std::cos(Beta * ToRadians);
    const double TanBeta = std::tan(Beta * ToRadians);

    // Right Ascension, in degrees:
    const double RightAscension = std::atan2(SinLambda * CosEpsilon - TanBeta * SinEpsilon, CosLambda) * ToDegrees;

    // Declination (lower case delta in some texts), in degrees:
    const double Declination = std::atan(SinBeta * CosEpsilon + CosBeta * SinEpsilon * SinLambda) * ToDegrees;

    // TODO: calculate sidreal time: https://aa.quae.nl/en/reken/hemelpositie.html#1_8
    // Apparently lower case "l" is the observer's geographic longitude.

    // TODO: calculate hour angle: https://aa.quae.nl/en/reken/hemelpositie.html#1_9

    // TODO: calculate alt/az: https://aa.quae.nl/en/reken/hemelpositie.html#1_10
    // Apparently cursive case quake logo (lower case phi) is your geographic latitude.
}
#endif


void TideThunk::Crank(double SampleInterval)
{
    TRACEABLE_NAMED_SCOPE("TideThunk");
#if 0
    /* TODO: Maybe it would be better to calculate the Julian date at program start, and corresponding
     * std::chrono::steady_clock time point, and just use that internally?  Can steady_clock be assumed
     * to be consistent between threads?  A date time node would have a more complex conversion.
     */
    double UnixTime = Combine(CombinerAdd, TimePoint, 0.0);
    /*
     * TODO: Convert from unix time (or whatever) to Julian century?
     * http://www.geoastro.de/elevazmoon/basics/meeus.htm says how to do the first part
     * https://www.celestialprogramming.com/meeus-elp82.html says how to do the second part
     */
    double JulianCentury = 0.0;

    double GeographicLongitude = Combine(CombinerAdd, Longitude, 41.881944);
    double GeographicLatitude = Combine(CombinerAdd, Latitude, -87.627778);
    /* TODO: Convert from geographic coordinates to overhead ecliptic coordinates.
     * A comment in the `convert` function in https://www.celestialprogramming.com/meeus-elp82.html
     * advises that the longitude might be flipped relative to popular convention, however this is
     * only passed into their geocentric2Topocentric function, and I have no intention of using
     * topocentric coordinates so maybe it's a non-issue, but a sign flip might still be needed.
     */
    double ObserverEclipticLongitude = 0.0;
    double ObserverEclipticLatitude = 0.0;
    double ObserverDistanceKm = 6371.0; // Approximate average radius of Earth.

    double MoonEclipticLongitude = 0.0;
    double MoonEclipticLatitude = 0.0;
    double MoonDistanceKm = 0.0;
    TruncatedElp(JulianCentury, MoonEclipticLongitude, MoonEclipticLatitude, MoonDistanceKm);

    /*
     * TODO: Translate the ecliptic coordinates into a pair of vectors, normalize them, and then stuff
     * their dot product into the output register.
     */
#endif

    OutCosine->Set(1.0);
}
