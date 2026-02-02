// include/rfui/icons.h
// FontAwesome 6 Free Solid icon codepoints for rayforce-ui UI

#ifndef RFUI_ICONS_H
#define RFUI_ICONS_H

// Icon range for font merging
#define ICON_MIN_FA 0xf000
#define ICON_MAX_FA 0xf8ff

// Grid widget
#define ICON_GEAR       "\xef\x80\x93"   // f013 - settings
#define ICON_PLUS       "\xef\x81\xa7"   // f067 - add rule
#define ICON_XMARK      "\xef\x80\x8d"   // f00d - delete/remove
#define ICON_ERASER     "\xef\x84\xad"   // f12d - clear

// REPL widget
#define ICON_FOLDER_OPEN "\xef\x81\xbc"  // f07c - fa-folder-open
#define ICON_TERMINAL   "\xef\x84\xa0"   // f120 - terminal
#define ICON_CHEVRON_R  "\xef\x81\x94"   // f054 - prompt chevron
#define ICON_PROMPT     "\xc2\xbb"       // U+00BB - right double angle quote (») for prompt

// Widget types (for tabs/headers)
#define ICON_TABLE       "\xef\x83\x8e"  // f0ce - fa-table
#define ICON_CHART_LINE  "\xef\x88\x81"  // f201 - chart
#define ICON_FILE_LINES  "\xef\x85\x9c"  // f15c - fa-file-lines (text display)
#define ICON_PALETTE     "\xef\x94\xbf"  // f53f - color picker
#define ICON_CHECK       "\xef\x80\x8c"  // f00c - enabled/checkbox
#define ICON_EYE         "\xef\x81\xae"  // f06e - visible/enabled
#define ICON_FILTER      "\xef\x82\xb0"  // f0b0 - filter
#define ICON_SORT_UP     "\xef\x83\x9e"  // f0de - fa-sort-up (caret-up)
#define ICON_SORT_DOWN   "\xef\x83\x9d"  // f0dd - fa-sort-down (caret-down)
#define ICON_CHEVRON_DN  "\xef\x81\xb8"  // f078 - fa-chevron-down
#define ICON_BELL        "\xef\x83\xb3"  // f0f3 - fa-bell

// Window controls (custom title bar)
#define ICON_MINIMIZE    "\xef\x8b\x91"  // f2d1 - fa-window-minimize
#define ICON_MAXIMIZE    "\xef\x8b\x90"  // f2d0 - fa-window-maximize
#define ICON_RESTORE     "\xef\x8b\x92"  // f2d2 - fa-window-restore
#define ICON_CLOSE       "\xef\x80\x8d"  // f00d - fa-xmark

#endif // RFUI_ICONS_H
