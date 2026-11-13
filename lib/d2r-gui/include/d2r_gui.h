// D2R GUI 0.0.1: the in-game window parts shared by wild-healer's D2RLoader
// plugins (taken from Grail On! and I Hate TOMLs). Header-only: include it
// in the plugin's one .cpp and it compiles into the plugin's own DLL, so the
// plugin stays a single file with nothing else to install.
//
// See lib/d2r-gui/README.md.
#pragma once
#include "d2r_gui/core.h"
#include "d2r_gui/text.h"
#include "d2r_gui/picture.h"
#include "d2r_gui/game_files.h"
#include "d2r_gui/resources.h"
#include "d2r_gui/art.h"
#include "d2r_gui/kit.h"
#include "d2r_gui/layout.h"
#include "d2r_gui/commands.h"
#include "d2r_gui/panel_pool.h"
#include "d2r_gui/text_input.h"
#include "d2r_gui/window_hook.h"
#include "d2r_gui/scroll_bar.h"
#include "d2r_gui/font.h"
#include "d2r_gui/widgets.h"
#include "d2r_gui/left_panel.h"
