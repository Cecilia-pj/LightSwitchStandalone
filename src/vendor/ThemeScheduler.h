// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
//
// Vendored from microsoft/PowerToys:
//   src/modules/LightSwitch/LightSwitchService/ThemeScheduler.h
// Modifications: none (content identical to upstream).
#pragma once
#include <cmath>
#include <ctime>
#include <windows.h>

// Struct to hold calculated sunrise/sunset times
struct SunTimes
{
    int sunriseHour;
    int sunriseMinute;
    int sunsetHour;
    int sunsetMinute;
};

constexpr double PI = 3.14159265358979323846;
constexpr double deg2rad(double deg)
{
    return deg * PI / 180.0;
}
constexpr double rad2deg(double rad)
{
    return rad * 180.0 / PI;
}

SunTimes CalculateSunriseSunset(double latitude, double longitude, int year, int month, int day);
