#pragma once
#include "../common/game_state.h"
#include <semaphore.h>


struct UIEvent {
    ActionType type;
    int        target_id;   // global entity id for Strike/Exhaust; -1 otherwise
    int        weapon_idx;  // inventory slot for Use Weapon; -1 otherwise
    int        lts_idx;     // LTS index for Swap In; -1 otherwise
};


struct UIBus {
    UIEvent            event;
    sem_t              event_ready;   // renderer posts, player thread waits
    volatile int       active_player; // entity_id of the player whose turn it is
    bool               initialised;
};

extern UIBus g_ui_bus;

void ui_bus_init();
void ui_bus_destroy();