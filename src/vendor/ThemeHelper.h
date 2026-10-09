// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
//
// Vendored from microsoft/PowerToys:
//   src/modules/LightSwitch/LightSwitchLib/ThemeHelper.h
// Modifications: none (content identical to upstream).
#pragma once

inline constexpr wchar_t PERSONALIZATION_REGISTRY_PATH[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
inline constexpr wchar_t NIGHT_LIGHT_REGISTRY_PATH[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\default$windows.data.bluelightreduction.bluelightreductionstate\\windows.data.bluelightreduction.bluelightreductionstate";

void SetSystemTheme(bool isLight);
void SetAppsTheme(bool isLight);
bool GetCurrentSystemTheme();
bool GetCurrentAppsTheme();
bool IsNightLightEnabled();
