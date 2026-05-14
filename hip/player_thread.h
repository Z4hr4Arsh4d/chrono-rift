#pragma once
#include "../common/game_state.h"

struct PlayerThreadArgs {
    GameState* state;
    int        entity_id;   // 0 .. MAX_PLAYERS-1
};

void* player_thread_main(void* arg);