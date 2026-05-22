#pragma once

enum class GameState   { MainMenu, SettingsMenu, JoinMenu, CharacterEditor, HouseEditor, Playing, Paused };
enum class SessionMode { None, Host, Join, Singleplayer };
enum class EditorTool  { Paint, Add, Erase };

static constexpr int   WINDOW_WIDTH      = 1280;
static constexpr int   WINDOW_HEIGHT     = 720;
static constexpr float DAY_CYCLE_SECONDS = 120.0f;
static constexpr float REACH             = 5.0f;
static constexpr float PLAYER_HEIGHT     = 1.8f;
static constexpr float PLAYER_WIDTH      = 0.4f;
static constexpr float WATER_LEVEL_Y     = 65.0f;
static constexpr int   MAX_LANTERNS      = 48;
