// arbiter/signals.h

//
//   SIGUSR1   -> stun a target entity for STUN_DURATION_SEC seconds
//   SIGSTOP   -> sent BY arbiter TO asp_pid when a player uses Ultimate
//   SIGCONT   -> sent BY arbiter TO asp_pid when the 10s window expires
//   SIGALRM   -> raised by alarm() to time Ultimate window AND NPC turn timeout

#pragma once

#include <csignal>
#include "../common/game_state.h"

void install_signal_handlers(GameState* state);

void trigger_ultimate_pause(GameState* state);
double now_seconds();

void arm_npc_turn_timeout(int seconds);
void cancel_npc_turn_timeout();
extern volatile sig_atomic_t g_npc_timeout_fired;
void clear_expired_stuns_locked(GameState* state);
