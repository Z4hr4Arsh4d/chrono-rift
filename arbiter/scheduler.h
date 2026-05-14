#pragma once

#include "../common/game_state.h"

struct SchedulerConfig {
    int  num_players;     // 1..4 (clamped)
    int  num_enemies;     // 2..9 (clamped)
    int  win_kill_target; // default KILLS_TO_WIN (10)
    int  tick_ms;         // default SCHED_TICK_MS (100)
    bool test_mode;       // true: scheduler auto-acts (no HIP/ASP needed)
    int  test_max_ticks;  // safety cap in test mode
    bool deadlock_demo;   // true: pre-stage a circular wait so detector fires
    bool mp_mode;         // true: multiplayer mode — wait for multiple HIPs
                          // to claim hero_slot[] entries instead of waiting
                          // on a single HIP to set num_players
};

SchedulerConfig scheduler_default_config();
void scheduler_init_entities(GameState* state, const SchedulerConfig& cfg);
void scheduler_run(GameState* state, const SchedulerConfig& cfg);