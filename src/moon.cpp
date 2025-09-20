
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
static const double J2000 = 2451545.0;
static const std::chrono::utc_clock::time_point JulianEpochUtc = ([]()
{
    // C++ is a beautiful language.
    std::istringstream DateString{"2000:01:01:12:00:00"};
    DateString.imbue(std::locale("en_US.utf-8"));
    std::chrono::utc_clock::time_point TimePoint;
    DateString >> std::chrono::parse("%Y:%m:%d:%H:%M:%S", TimePoint);
    return TimePoint;
})();


double GetCurrentJulianDate()
{
    std::chrono::utc_clock::time_point AnchorUtc = std::chrono::utc_clock::now();
    std::chrono::utc_clock::duration TimeSinceEpoch(AnchorUtc - JulianEpochUtc);
    double Days = std::chrono::duration_cast<std::chrono::duration<double, std::ratio<86400>>>(TimeSinceEpoch).count();

    // https://crf.usno.navy.mil/ut1-utc claims that UTC and UT1 are only
    // ever off by at most .9 seconds due to the magic of leap seconds added
    // to UTC.  Thus, if we just pretend that UT1 doesn't exist, then the
    // current Julian date is:
    return Days + J2000;

    // See also: https://aa.usno.navy.mil/data/JulianDate
}


static constexpr double ToRadians = std::numbers::pi / 180.0;
static constexpr double ToDegrees = 180.0 / std::numbers::pi;


// Approximate Moon math adapted from https://aa.quae.nl/en/reken/hemelpositie.html
static double MoonPosition(double JulianDate, double ObserverLatitude, double ObserverLongitude)
{
    // The number of days since noon (UTC) January 1st, 2000.
    const double Days = JulianDate - J2000;
    const double Hours = Days / 24.0;
    //const double Seconds = Hours / (60.0 * 60.0);

    // Obliquity of the ecliptic as of D == 0.0, in degrees:
    const double Epsilon = 23.4397;

    // Calculate the Moon's geocentric ecliptic coordinates: https://aa.quae.nl/en/reken/hemelpositie.html#4

    // Mean geocentric ecliptic longitude, in degrees:
    const double L = std::fmod(218.316 + 13.176396 * Days, 360.0);

    // Mean anomaly, in degrees:
    //const double M = std::fmod(134.963 + 13.064993 * Days, 360.0);

    // Mean distance of the Moon from its ascending node, in degrees:
    const double F = std::fmod(93.272 + 13.229350 * Days, 360.0);

    // Geocentric ecliptic longitude, in degrees:
    const double Lambda = L + 6.289 * std::sin(L * ToRadians);

    // Geocentric ecliptic latitude, in degrees:
    const double Beta = 5.128 * std::sin(F * ToRadians);

    // Distance from the Earth, in kilometers
    //const double Delta = 385001 - 20905 * std::cos(M * ToRadians);

    // Intermediaries:
    const double SinLambda = std::sin(Lambda * ToRadians);
    const double CosLambda = std::cos(Lambda * ToRadians);
    const double SinEpsilon = std::sin(Epsilon * ToRadians);
    const double CosEpsilon = std::cos(Epsilon * ToRadians);
    const double SinBeta = std::sin(Beta * ToRadians);
    const double CosBeta = std::cos(Beta * ToRadians);
    const double TanBeta = std::tan(Beta * ToRadians);

    // Right Ascension (lower case alpha), in degrees:
    const double RightAscension = std::atan2(SinLambda * CosEpsilon - TanBeta * SinEpsilon, CosLambda) * ToDegrees;

    // Declination (lower case delta), in degrees:
    const double Declination = std::atan(SinBeta * CosEpsilon + CosBeta * SinEpsilon * SinLambda) * ToDegrees;

    // TODO: calculate sidereal time: https://aa.quae.nl/en/reken/hemelpositie.html#1_8
    // Apparently lower case "l" is the observer's geographic longitude, uppercase theta
    // is sidereal time at the prime meridian, and lowercase theta is local sidereal time.
    const double EarthPie = 102.937 * Days; // Degrees
    const double EarthM = 357.529 * Days; // Degrees
    const double PrimeSiderealTime = std::fmod(EarthM + EarthPie + 15 * Hours, 360.0); // Degrees
    const double SiderealTime = std::fmod(PrimeSiderealTime - ObserverLongitude, 360.0); // Degrees

    // TODO: calculate hour angle: https://aa.quae.nl/en/reken/hemelpositie.html#1_9
    const double HourAngle = SiderealTime - RightAscension;

    // TODO: calculate alt/az: https://aa.quae.nl/en/reken/hemelpositie.html#1_10
    // Apparently cursive quake logo (lower case phi) is your geographic latitude.
    const double SinObsLat = std::sin(ObserverLatitude * ToRadians);
    const double CosObsLat = std::cos(ObserverLatitude * ToRadians);
    const double SinDeclination = std::sin(Declination * ToRadians);
    const double CosDeclination = std::cos(Declination * ToRadians);
    const double CosHourAngle = std::cos(HourAngle * ToRadians);

    // Degrees
    double AltitudeH = std::asin(SinObsLat * SinDeclination + CosObsLat * CosDeclination * CosHourAngle) * ToDegrees;
    return AltitudeH;
}


void MoonThunk::Crank(double SampleInterval)
{
    TRACEABLE_NAMED_SCOPE("MoonThunk");

    double ObserverLatitude = Combine(CombinerAdd, Latitude, 41.881944);
    double ObserverLongitude = Combine(CombinerAdd, Longitude, -87.627778);
    double TimeMultiplier = Combine(CombinerAdd, Speed, 1.0);

    double CurrentDate = Combine(CombinerAdd, JulianDate, -1.0);
    if (CurrentDate < 0.0)
    {
        // Stars did not exist prior to noon Universal Time on January 1, 4713 BC, so
        // we will use an impossible date to indicate the origin should be measured in
        // local time instead.
        CurrentDate = OriginDate->Get();
        if (CurrentDate == 0.0)
        {
            // As it is extremely unlikely that the operator of this program is starting
            // this patch at exactly noon Universal Time on January 1, 4713 BC, we use
            // a zero value to indicate that the current time needs to be recorded and
            // saved in the closure register.  You can express this date explicitly, in
            // which case the closure register is ignored.
            CurrentDate = GetCurrentJulianDate();
            OriginDate->Set(CurrentDate);
        }
    }
    {
        // Since samples are calculated in batches, we must assume that ::Crank is
        // called multiple times semisimultaneously.  As such, we have to advance
        // time and recorde the elapsed time in a closure register.
        double AccumulatedTime = ElapsedSeconds->Get() + SampleInterval * TimeMultiplier;
        ElapsedSeconds->Set(AccumulatedTime);
        // And then we convert that to a fraction of a day and add it to the origin date.
        CurrentDate += AccumulatedTime / 86400.0;
    }

    double AltitudeH = MoonPosition(CurrentDate, ObserverLatitude, ObserverLongitude);
    Altitude->Set(AltitudeH / 90.0);
}
