
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

#if 0
static constexpr double ToRadians = std::numbers::pi / 180.0;
static constexpr double ToDegrees = 180.0 / std::numbers::pi;


struct LongitudeLutRow
{
    double D;
    double M;
    double Mp;
    double F;
    double Longitude;
    double Radius;
};


// See note in TruncatedElp for origin of these constants.
static const std::array<LongitudeLutRow, 60> LongitudeLut = \
{{
    { 0.0,  0.0,  1.0,  0.0,  6288774.0, -20905355.0 },
    { 2.0,  0.0, -1.0,  0.0,  1274027.0,  -3699111.0 },
    { 2.0,  0.0,  0.0,  0.0,   658314.0,  -2955968.0 },
    { 0.0,  0.0,  2.0,  0.0,   213618.0,   -569925.0 },
    { 0.0,  1.0,  0.0,  0.0,  -185116.0,     48888.0 },
    { 0.0,  0.0,  0.0,  2.0,  -114332.0,     -3149.0 },
    { 2.0,  0.0, -2.0,  0.0,    58793.0,    246158.0 },
    { 2.0, -1.0, -1.0,  0.0,    57066.0,   -152138.0 },
    { 2.0,  0.0,  1.0,  0.0,    53322.0,   -170733.0 },
    { 2.0, -1.0,  0.0,  0.0,    45758.0,   -204586.0 },
    { 0.0,  1.0, -1.0,  0.0,   -40923.0,   -129620.0 },
    { 1.0,  0.0,  0.0,  0.0,   -34720.0,    108743.0 },
    { 0.0,  1.0,  1.0,  0.0,   -30383.0,    104755.0 },
    { 2.0,  0.0,  0.0, -2.0,    15327.0,     10321.0 },
    { 0.0,  0.0,  1.0,  2.0,   -12528.0,         0.0 },
    { 0.0,  0.0,  1.0, -2.0,    10980.0,     79661.0 },
    { 4.0,  0.0, -1.0,  0.0,    10675.0,    -34782.0 },
    { 0.0,  0.0,  3.0,  0.0,    10034.0,    -23210.0 },
    { 4.0,  0.0, -2.0,  0.0,     8548.0,    -21636.0 },
    { 2.0,  1.0, -1.0,  0.0,    -7888.0,     24208.0 },
    { 2.0,  1.0,  0.0,  0.0,    -6766.0,     30824.0 },
    { 1.0,  0.0, -1.0,  0.0,    -5163.0,     -8379.0 },
    { 1.0,  1.0,  0.0,  0.0,     4987.0,    -16675.0 },
    { 2.0, -1.0,  1.0,  0.0,     4036.0,    -12831.0 },
    { 2.0,  0.0,  2.0,  0.0,     3994.0,    -10445.0 },
    { 4.0,  0.0,  0.0,  0.0,     3861.0,    -11650.0 },
    { 2.0,  0.0, -3.0,  0.0,     3665.0,     14403.0 },
    { 0.0,  1.0, -2.0,  0.0,    -2689.0,     -7003.0 },
    { 2.0,  0.0, -1.0,  2.0,    -2602.0,         0.0 },
    { 2.0, -1.0, -2.0,  0.0,     2390.0,     10056.0 },
    { 1.0,  0.0,  1.0,  0.0,    -2348.0,      6322.0 },
    { 2.0, -2.0,  0.0,  0.0,     2236.0,     -9884.0 },
    { 0.0,  1.0,  2.0,  0.0,    -2120.0,      5751.0 },
    { 0.0,  2.0,  0.0,  0.0,    -2069.0,         0.0 },
    { 2.0, -2.0, -1.0,  0.0,     2048.0,     -4950.0 },
    { 2.0,  0.0,  1.0, -2.0,    -1773.0,      4130.0 },
    { 2.0,  0.0,  0.0,  2.0,    -1595.0,         0.0 },
    { 4.0, -1.0, -1.0,  0.0,     1215.0,     -3958.0 },
    { 0.0,  0.0,  2.0,  2.0,    -1110.0,         0.0 },
    { 3.0,  0.0, -1.0,  0.0,     -892.0,      3258.0 },
    { 2.0,  1.0,  1.0,  0.0,     -810.0,      2616.0 },
    { 4.0, -1.0, -2.0,  0.0,      759.0,     -1897.0 },
    { 0.0,  2.0, -1.0,  0.0,     -713.0,     -2117.0 },
    { 2.0,  2.0, -1.0,  0.0,     -700.0,      2354.0 },
    { 2.0,  1.0, -2.0,  0.0,      691.0,         0.0 },
    { 2.0, -1.0,  0.0, -2.0,      596.0,         0.0 },
    { 4.0,  0.0,  1.0,  0.0,      549.0,     -1423.0 },
    { 0.0,  0.0,  4.0,  0.0,      537.0,     -1117.0 },
    { 4.0, -1.0,  0.0,  0.0,      520.0,     -1571.0 },
    { 1.0,  0.0, -2.0,  0.0,     -487.0,     -1739.0 },
    { 2.0,  1.0,  0.0, -2.0,     -399.0,         0.0 },
    { 0.0,  0.0,  2.0, -2.0,     -381.0,     -4421.0 },
    { 1.0,  1.0,  1.0,  0.0,      351.0,         0.0 },
    { 3.0,  0.0, -2.0,  0.0,     -340.0,         0.0 },
    { 4.0,  0.0, -3.0,  0.0,      330.0,         0.0 },
    { 2.0, -1.0,  2.0,  0.0,      327.0,         0.0 },
    { 0.0,  2.0,  1.0,  0.0,     -323.0,      1165.0 },
    { 1.0,  1.0, -1.0,  0.0,      299.0,         0.0 },
    { 2.0,  0.0,  3.0,  0.0,      294.0,         0.0 },
    { 2.0,  0.0, -1.0, -2.0,        0.0,      8752.0 }
}};


struct LatitudeLutRow
{
    double D;
    double M;
    double Mp;
    double F;
    double Latitude;
};


// See note in TruncatedElp for origin of these constants.
static const std::array<LatitudeLutRow, 60> LatitudeLut = \
{{
    { 0.0,  0.0,  0.0,  1.0, 5128122.0 },
    { 0.0,  0.0,  1.0,  1.0,  280602.0 },
    { 0.0,  0.0,  1.0, -1.0,  277693.0 },
    { 2.0,  0.0,  0.0, -1.0,  173237.0 },
    { 2.0,  0.0, -1.0,  1.0,   55413.0 },
    { 2.0,  0.0, -1.0, -1.0,   46271.0 },
    { 2.0,  0.0,  0.0,  1.0,   32573.0 },
    { 0.0,  0.0,  2.0,  1.0,   17198.0 },
    { 2.0,  0.0,  1.0, -1.0,    9266.0 },
    { 0.0,  0.0,  2.0, -1.0,    8822.0 },
    { 2.0, -1.0,  0.0, -1.0,    8216.0 },
    { 2.0,  0.0, -2.0, -1.0,    4324.0 },
    { 2.0,  0.0,  1.0,  1.0,    4200.0 },
    { 2.0,  1.0,  0.0, -1.0,   -3359.0 },
    { 2.0, -1.0, -1.0,  1.0,    2463.0 },
    { 2.0, -1.0,  0.0,  1.0,    2211.0 },
    { 2.0, -1.0, -1.0, -1.0,    2065.0 },
    { 0.0,  1.0, -1.0, -1.0,   -1870.0 },
    { 4.0,  0.0, -1.0, -1.0,    1828.0 },
    { 0.0,  1.0,  0.0,  1.0,   -1794.0 },
    { 0.0,  0.0,  0.0,  3.0,   -1749.0 },
    { 0.0,  1.0, -1.0,  1.0,   -1565.0 },
    { 1.0,  0.0,  0.0,  1.0,   -1491.0 },
    { 0.0,  1.0,  1.0,  1.0,   -1475.0 },
    { 0.0,  1.0,  1.0, -1.0,   -1410.0 },
    { 0.0,  1.0,  0.0, -1.0,   -1344.0 },
    { 1.0,  0.0,  0.0, -1.0,   -1335.0 },
    { 0.0,  0.0,  3.0,  1.0,    1107.0 },
    { 4.0,  0.0,  0.0, -1.0,    1021.0 },
    { 4.0,  0.0, -1.0,  1.0,     833.0 },
    { 0.0,  0.0,  1.0, -3.0,     777.0 },
    { 4.0,  0.0, -2.0,  1.0,     671.0 },
    { 2.0,  0.0,  0.0, -3.0,     607.0 },
    { 2.0,  0.0,  2.0, -1.0,     596.0 },
    { 2.0, -1.0,  1.0, -1.0,     491.0 },
    { 2.0,  0.0, -2.0,  1.0,    -451.0 },
    { 0.0,  0.0,  3.0, -1.0,     439.0 },
    { 2.0,  0.0,  2.0,  1.0,     422.0 },
    { 2.0,  0.0, -3.0, -1.0,     421.0 },
    { 2.0,  1.0, -1.0,  1.0,    -366.0 },
    { 2.0,  1.0,  0.0,  1.0,    -351.0 },
    { 4.0,  0.0,  0.0,  1.0,     331.0 },
    { 2.0, -1.0,  1.0,  1.0,     315.0 },
    { 2.0, -2.0,  0.0, -1.0,     302.0 },
    { 0.0,  0.0,  1.0,  3.0,    -283.0 },
    { 2.0,  1.0,  1.0, -1.0,    -229.0 },
    { 1.0,  1.0,  0.0, -1.0,     223.0 },
    { 1.0,  1.0,  0.0,  1.0,     223.0 },
    { 0.0,  1.0, -2.0, -1.0,    -220.0 },
    { 2.0,  1.0, -1.0, -1.0,    -220.0 },
    { 1.0,  0.0,  1.0,  1.0,    -185.0 },
    { 2.0, -1.0, -2.0, -1.0,     181.0 },
    { 0.0,  1.0,  2.0,  1.0,    -177.0 },
    { 4.0,  0.0, -2.0, -1.0,     176.0 },
    { 4.0, -1.0, -1.0, -1.0,     166.0 },
    { 1.0,  0.0,  1.0, -1.0,    -164.0 },
    { 4.0,  0.0,  1.0, -1.0,     132.0 },
    { 1.0,  0.0, -1.0, -1.0,    -119.0 },
    { 4.0, -1.0,  0.0, -1.0,     115.0 },
    { 2.0, -2.0,  0.0,  1.0,     107.0 }
}};


static void TruncatedElp(const double T, double& EclipticLongitude, double& EclipticLatitude, double& RadiusKm)
{
    // Adapted from https://www.celestialprogramming.com/meeus-elp82.html, which a code comment
    // indicates that the author (Greg Miller aka gmiller@gregmiller.net) released it as public
    // domain.  This is in turn an adaptation of the algorithm ELP2000-85, which is either from
    // the book Astronomical Algorithms or is truncated from a more elaborate algorithm in that
    // book.

    // Geocentric ecliptic coordinates, expressed in degrees.
    // https://github.com/mourner/suncalc/blob/7ccde2118968e21e47db573e34757258275943ae/suncalc.js#L166
    // suggests to me that these should be named "right ascension" and "declination", but further
    // review of the source of https://www.celestialprogramming.com/meeus-elp82.html suggests that
    // no, this is not the case, and an additional transform would be needed for this to be the case.

    // Elsewhere on the celestialprogramming webpage is mention of a lower precision algorithm
    // from the Astronomical Almanac.  The page makes no mention of license or public domain, so
    // I will not be transcribing it.  However, it looks like a printed version of the almanac is
    // available https://bookstore.gpo.gov/products/astronomical-almanac-year-2025 though it is
    // unclear if that will make anything easier.

    // It occurs to me that if I have topocentric(?) horizontal coordinates ("altitude h" and
    // "azimuth alpha" mentioned at the start of http://www.geoastro.de/elevazmoon/basics/index.htm)
    // then I can get something similar to what I wanted in the first place from just the altitude h.
    // So, std::cos(DegreesToRadians(90 - AltitudeH))

    EclipticLongitude = 0.0;
    EclipticLatitude = 0.0;
    RadiusKm = 0.0;

    auto Rad = [](double X) -> double
    {
        X = std::fmod(X, 360.0);
        if (X < 0.0)
        {
            X += 360.0;
        }
        return X * ToRadians;
    };

    const double T2 = T * T;
    const double T3 = T2 * T;
    const double T4 = T3 * T;

    const double Lp = Rad(218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + 1.0 / 538841.0 * T3 - 1.0 / 65194000.0 * T4);
    const double D = Rad(297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + 1.0 / 545868.0 * T3 - 1.0 / 113065000.0 * T4);
    const double M = Rad(357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + 1.0 / 24490000.0 * T3);
    const double Mp = Rad(134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + 1.0 / 69699.0 * T3 - 1.0 / 14712000.0 * T4);
    const double F = Rad(93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - 1.0 / 3526000.0 * T3 + 1.0 / 863310000.0 * T4);
    const double E = 1 - .002516 * T - 0.0000074 * T2;
    const double E2 = E * E;
    const double A1 = Rad(119.75 + 131.849 * T);
    const double A2 = Rad(53.09 + 479264.290 * T);
    const double A3 = Rad(313.45 + 481266.484 * T);

    for (const LongitudeLutRow& Row : LongitudeLut)
    {
        double Angle = D * Row.D + M * Row.M + Mp * Row.Mp + F * Row.F;
        double e = 1.0;
        if (std::abs(Row.M) == 1.0)
        {
            e = E;
        }
        else if (std::abs(Row.M) == 2.0)
        {
            e = E2;
        }
        EclipticLongitude += e * Row.Longitude * std::sin(Angle);
        RadiusKm += e * Row.Radius * std::cos(Angle);
    }

    for (const LatitudeLutRow& Row : LatitudeLut)
    {
        double Angle = D * Row.D + M * Row.M + Mp * Row.Mp + F * Row.F;
        double e = 1.0;
        if (std::abs(Row.M) == 1.0)
        {
            e = E;
        }
        else if (std::abs(Row.M) == 2.0)
        {
            e = E2;
        }
        EclipticLatitude += e * Row.Latitude * std::sin(Angle);
    }

    double aLon = \
        3958.0 * std::sin(A1)
        + 1962.0 * std::sin(Lp-F)
        + 318.0 * std::sin(A2);

    double aLat = \
        -2235.0 * std::sin(Lp)
        + 382.0 * std::sin(A3)
        + 175.0 * std::sin(A1-F)
        + 175.0 * std::sin(A1+F)
        + 127.0 * std::sin(Lp-Mp)
        - 115.0 * std::sin(Lp+Mp);

    EclipticLongitude = Lp * ToDegrees + (EclipticLongitude + aLon) / 1000000.0;
    EclipticLatitude = (EclipticLatitude + aLat) / 1000000.0;
    RadiusKm = 385000.56 + RadiusKm / 1000.0;
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
