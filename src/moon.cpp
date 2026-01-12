
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
#include <chrono>
#include <sstream>

#include "patch.h"
#include "moon.h"


// 2451545 is the Julian date for January 1, 2000 12:00:00.0 UT1.
static const AudioSample J2000 = 2451545.0;
#if 0
static const std::chrono::tai_clock::time_point JulianEpochUtc = ([]()
{
    // C++ is a beautiful language.
    std::istringstream DateString{"2000:01:01:12:00:00"};
    DateString.imbue(std::locale("en_US.utf-8"));
    std::chrono::tai_clock::time_point TimePoint;
    DateString >> std::chrono::parse("%Y:%m:%d:%H:%M:%S", TimePoint);
    return TimePoint;
})();


AudioSample GetCurrentJulianDate()
{
    std::chrono::tai_clock::time_point AnchorUtc = std::chrono::tai_clock::now();
    std::chrono::tai_clock::duration TimeSinceEpoch(AnchorUtc - JulianEpochUtc);
    AudioSample Days = std::chrono::duration_cast<std::chrono::duration<AudioSample, std::ratio<86400>>>(TimeSinceEpoch).count();

    // https://crf.usno.navy.mil/ut1-utc claims that UTC and UT1 are only
    // ever off by at most .9 seconds due to the magic of leap seconds added
    // to UTC.  Thus, if we just pretend that UT1 doesn't exist, then the
    // current Julian date is:
    return Days + J2000;

    // See also: https://aa.usno.navy.mil/data/JulianDate
}
#else
AudioSample GetCurrentJulianDate()
{
    // Time is broken :sunglasses:
    return J2000;
}
#endif

static constexpr AudioSample ToRadians = std::numbers::pi / 180.0;
static constexpr AudioSample ToDegrees = 180.0 / std::numbers::pi;


// Approximate Moon math adapted from https://aa.quae.nl/en/reken/hemelpositie.html
static AudioSample MoonPosition(AudioSample JulianDate, AudioSample ObserverLatitude, AudioSample ObserverLongitude)
{
    // The number of days since noon (UTC) January 1st, 2000.
    const AudioSample Days = JulianDate - J2000;
    const AudioSample Hours = Days / 24.0;
    //const AudioSample Seconds = Hours / (60.0 * 60.0);

    // Obliquity of the ecliptic as of D == 0.0, in degrees:
    const AudioSample Epsilon = 23.4397;

    // Calculate the Moon's geocentric ecliptic coordinates: https://aa.quae.nl/en/reken/hemelpositie.html#4

    // Mean geocentric ecliptic longitude, in degrees:
    const AudioSample L = std::fmod(218.316 + 13.176396 * Days, 360.0);

    // Mean anomaly, in degrees:
    //const AudioSample M = std::fmod(134.963 + 13.064993 * Days, 360.0);

    // Mean distance of the Moon from its ascending node, in degrees:
    const AudioSample F = std::fmod(93.272 + 13.229350 * Days, 360.0);

    // Geocentric ecliptic longitude, in degrees:
    const AudioSample Lambda = L + 6.289 * std::sin(L * ToRadians);

    // Geocentric ecliptic latitude, in degrees:
    const AudioSample Beta = 5.128 * std::sin(F * ToRadians);

    // Distance from the Earth, in kilometers
    //const AudioSample Delta = 385001 - 20905 * std::cos(M * ToRadians);

    // Intermediaries:
    const AudioSample SinLambda = std::sin(Lambda * ToRadians);
    const AudioSample CosLambda = std::cos(Lambda * ToRadians);
    const AudioSample SinEpsilon = std::sin(Epsilon * ToRadians);
    const AudioSample CosEpsilon = std::cos(Epsilon * ToRadians);
    const AudioSample SinBeta = std::sin(Beta * ToRadians);
    const AudioSample CosBeta = std::cos(Beta * ToRadians);
    const AudioSample TanBeta = std::tan(Beta * ToRadians);

    // Right Ascension (lower case alpha), in degrees:
    const AudioSample RightAscension = std::atan2(SinLambda * CosEpsilon - TanBeta * SinEpsilon, CosLambda) * ToDegrees;

    // Declination (lower case delta), in degrees:
    const AudioSample Declination = std::atan(SinBeta * CosEpsilon + CosBeta * SinEpsilon * SinLambda) * ToDegrees;

    // TODO: calculate sidereal time: https://aa.quae.nl/en/reken/hemelpositie.html#1_8
    // Apparently lower case "l" is the observer's geographic longitude, uppercase theta
    // is sidereal time at the prime meridian, and lowercase theta is local sidereal time.
    const AudioSample EarthPie = 102.937 * Days; // Degrees
    const AudioSample EarthM = 357.529 * Days; // Degrees
    const AudioSample PrimeSiderealTime = std::fmod(EarthM + EarthPie + 15 * Hours, 360.0); // Degrees
    const AudioSample SiderealTime = std::fmod(PrimeSiderealTime - ObserverLongitude, 360.0); // Degrees

    // TODO: calculate hour angle: https://aa.quae.nl/en/reken/hemelpositie.html#1_9
    const AudioSample HourAngle = SiderealTime - RightAscension;

    // TODO: calculate alt/az: https://aa.quae.nl/en/reken/hemelpositie.html#1_10
    // Apparently cursive quake logo (lower case phi) is your geographic latitude.
    const AudioSample SinObsLat = std::sin(ObserverLatitude * ToRadians);
    const AudioSample CosObsLat = std::cos(ObserverLatitude * ToRadians);
    const AudioSample SinDeclination = std::sin(Declination * ToRadians);
    const AudioSample CosDeclination = std::cos(Declination * ToRadians);
    const AudioSample CosHourAngle = std::cos(HourAngle * ToRadians);

    // Degrees
    AudioSample AltitudeH = std::asin(SinObsLat * SinDeclination + CosObsLat * CosDeclination * CosHourAngle) * ToDegrees;
    return AltitudeH;
}


void MoonThunk::Crank(AudioSample SampleInterval)
{
    TRACEABLE_NAMED_SCOPE("MoonThunk");
    CrankLanes([&](uint32_t Lane)
    {
        AudioSample Latitude = Registers.CombineInput(Lane, 0, 41.881944);
        AudioSample Longitude = Registers.CombineInput(Lane, 1, -87.627778);
        AudioSample JulianDate = Registers.CombineInput(Lane, 2, -1.0);
        AudioSample Speed = Registers.CombineInput(Lane, 3, 1.0);
        AudioSample& Altitude = Registers.OutputRef(Lane, 0);

        // If JulianDate was unset, then this will cache the current Julian Date
        // at the time the tile was activated.
        AudioSample& OriginDate = Registers.ClosureRef(Lane, 0);
        AudioSample& ElapsedSeconds = Registers.ClosureRef(Lane, 1);

        if (JulianDate < 0.0)
        {
            // Stars did not exist prior to noon Universal Time on January 1, 4713 BC, so
            // we will use an impossible date to indicate the origin should be measured in
            // local time instead.
            JulianDate = OriginDate;
            if (JulianDate == 0.0)
            {
                // As it is extremely unlikely that the operator of this program is starting
                // this patch at exactly noon Universal Time on January 1, 4713 BC, we use
                // a zero value to indicate that the current time needs to be recorded and
                // saved in the closure register.  You can express this date explicitly, in
                // which case the closure register is ignored.
                JulianDate = GetCurrentJulianDate();
                OriginDate = JulianDate;
            }
        }
        {
            // Since samples are calculated in batches, we must assume that ::Crank is
            // called multiple times semisimultaneously.  As such, we have to advance
            // time and recorde the elapsed time in a closure register.
            ElapsedSeconds = ElapsedSeconds + SampleInterval * Speed;
            // And then we convert that to a fraction of a day and add it to the origin date.
            JulianDate += ElapsedSeconds / 86400.0;
        }

        AudioSample AltitudeH = MoonPosition(JulianDate, Latitude, Longitude);
        Altitude = AltitudeH / 90.0;
    });
}
