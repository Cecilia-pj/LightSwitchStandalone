// LightSwitch Standalone - resource identifiers.
//
// Note: every control whose text is translated needs a real id, because
// SettingsDialog.cpp fills the dialog from the L10n table by id. That is why
// the plain labels are IDC_STATIC_* instead of the usual -1.
#pragma once

#define IDD_SETTINGS 1000

#define IDC_CMB_MODE 1001
#define IDC_DTP_LIGHT 1002
#define IDC_DTP_DARK 1003
#define IDC_STATIC_SUN 1004
// 1005 and 1006 were IDC_CHK_SYSTEM / IDC_CHK_APPS ("apply to Windows /
// applications"). Retired in 1.4.0 when the two theme packs replaced the
// boolean pair; the numbers are deliberately not reused, so an id in an old
// screenshot or log still means what it used to.
#define IDC_EDT_LAT 1007
#define IDC_EDT_LON 1008
#define IDC_EDT_SUNRISE_OFF 1009
#define IDC_EDT_SUNSET_OFF 1010
#define IDC_CHK_WIN 1011
#define IDC_HOTKEY 1012
#define IDC_CHK_STARTUP 1013
#define IDC_STATIC_STARTUP 1014
#define IDC_BTN_TOGGLE 1015
#define IDC_BTN_RESTART_EXPLORER 1016
#define IDC_BTN_OPEN_FOLDER 1017
#define IDC_STATIC_CURRENT 1018
#define IDC_STATIC_HINT 1019
#define IDC_BTN_RESET 1020

// ---- interface language ---------------------------------------------------
#define IDC_CMB_LANG 1021

// ---- interface theme ------------------------------------------------------
#define IDC_CMB_THEME 1038
#define IDC_CHK_TRAY_CLICK 1041

// ---- the two scheduled Windows themes -------------------------------------
// Fills the rectangle the old "apply to" group used to occupy (7,86,326,42),
// so the dialog keeps its height. See the note above the button row in the .rc.
#define IDC_STATIC_GROUP_THEMES 1042
#define IDC_STATIC_LBL_THEME_LIGHT 1043
#define IDC_CMB_THEME_LIGHT 1044
#define IDC_STATIC_LBL_THEME_DARK 1045
#define IDC_CMB_THEME_DARK 1046

// ---- translated labels / group boxes --------------------------------------
#define IDC_STATIC_GROUP_SCHEDULE 1022
#define IDC_STATIC_LBL_MODE 1023
#define IDC_STATIC_LBL_LIGHT 1024
#define IDC_STATIC_LBL_DARK 1025
// 1026 was IDC_STATIC_GROUP_APPLY, retired with the checkboxes it framed.
#define IDC_STATIC_GROUP_SUN 1027
#define IDC_STATIC_LBL_LAT 1028
#define IDC_STATIC_LBL_LON 1029
#define IDC_STATIC_LBL_SUNRISE_OFF 1030
#define IDC_STATIC_LBL_SUNSET_OFF 1031
#define IDC_STATIC_HINT_OFFSETS 1032
#define IDC_STATIC_GROUP_LANGUAGE 1033
#define IDC_STATIC_LBL_LANGUAGE 1034
#define IDC_STATIC_GROUP_SHORTCUT 1035
#define IDC_STATIC_HINT_HOTKEY 1036
#define IDC_STATIC_GROUP_STARTUP 1037
#define IDC_STATIC_GROUP_THEME 1039
#define IDC_STATIC_LBL_THEME 1040

#define IDI_APPICON 2000
