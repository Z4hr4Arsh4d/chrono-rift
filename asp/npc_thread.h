#pragma once
#include "../common/game_state.h"

struct NpcThreadArgs {
    GameState* state;
    int        entity_id;   // MAX_PLAYERS .. TOTAL_ENTITIES-1
};

void* npc_thread_main(void* arg);