#include "npc_thread.h"
#include "../common/game_state.h"
#include "../common/constants.h"
#include "../common/rng.h"

#include <cstdio>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include "../common/inventory.h"
#include "../common/weapons.h"



static Action decide_action(GameState* state, int eid) {
    Action a;
    std::memset(&a, 0, sizeof(a));
    a.entity_id  = eid;
    a.target_id  = -1;
    a.weapon_idx = -1;
    a.lts_idx    = -1;
    a.type       = ACT_SKIP;
    a.committed  = false;

    int alive[MAX_PLAYERS], count = 0;
    int finisher_target = -1;
    int my_hp = 0, my_max = 1;
    sem_wait(&state->state_lock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!state->entities[i].alive) continue;
        alive[count++] = i;
        // Pick first low-HP player as finisher target
        if (finisher_target < 0 &&
            state->entities[i].max_hp > 0 &&
            (double)state->entities[i].hp / state->entities[i].max_hp < 0.30) {
            finisher_target = i;
        }
    }
    my_hp  = state->entities[eid].hp;
    my_max = state->entities[eid].max_hp;
    sem_post(&state->state_lock);

    if (count == 0) return a;   // no targets

    // Skip-retreat heuristic: if HP < 20%, 35% chance to Skip (regen stamina)
    bool low_hp_self = (my_max > 0 && (double)my_hp / my_max < 0.20);
    double skip_chance = low_hp_self ? 0.35 : 0.15;

    if (rand_chance(skip_chance)) {
        return a;   // Skip
    }

    // Strike: prefer the finisher target if any, else random
    a.type = ACT_STRIKE;
    if (finisher_target >= 0 && rand_chance(0.70)) {
        a.target_id = finisher_target;
    } else {
        a.target_id = alive[rand_range(0, count - 1)];
    }
    return a;
}

extern volatile sig_atomic_t g_stun_signaled;

void* npc_thread_main(void* arg) {
    NpcThreadArgs* nargs = static_cast<NpcThreadArgs*>(arg);
    GameState* state     = nargs->state;
    int        eid       = nargs->entity_id;
    int        local     = enemy_local_idx(eid);

    sem_wait(&state->state_lock);
    state->entities[eid].owner_pid = getpid();
    sem_post(&state->state_lock);

    std::printf("[asp] NPC thread started  entity_id=%d (E%d)\n", eid, local + 1);

    while (state->status == GS_RUNNING) {
        sem_wait(&state->turn_sem[eid]);
        if (state->status != GS_RUNNING) break;

        sem_wait(&state->state_lock);
        bool stunned_now = state->entities[eid].stunned;
        sem_post(&state->state_lock);
        if (stunned_now) {
            std::printf("[asp] E%d stunned -> sleeping %ds\n",
                        local + 1, STUN_DURATION_SEC);
            g_stun_signaled = 0;
            sleep(STUN_DURATION_SEC);
            sem_wait(&state->state_lock);
            state->pending_action = {eid, ACT_SKIP, -1, -1, -1, false};
            sem_post(&state->state_lock);
            sem_post(&state->action_ready);
            sem_wait(&state->turn_done_sem[eid]);
            continue;
        }

        {
            sem_wait(&state->state_lock);
            bool alive = state->entities[eid].alive;
            sem_post(&state->state_lock);
            if (!alive) {
                sem_wait(&state->state_lock);
                state->pending_action = {eid, ACT_SKIP, -1, -1, -1, false};
                sem_post(&state->state_lock);
                sem_post(&state->action_ready);
                sem_wait(&state->turn_done_sem[eid]);
                continue;
            }
        }

        sem_wait(&state->state_lock);
        if (state->pending_drop.declined) {
            give_weapon(state->entities[eid], state->pending_drop.weapon_id);
            std::printf("[asp] E%d picked up declined %s\n",
                local + 1,
                get_weapon_def(state->pending_drop.weapon_id) ?
                    get_weapon_def(state->pending_drop.weapon_id)->name : "?");
            state->pending_drop.declined  = false;
            state->pending_drop.weapon_id = -1;
        }
        sem_post(&state->state_lock);


        Action a = decide_action(state, eid);
        std::printf("[asp] E%d decided action=%d target=%d\n",
                    local + 1, (int)a.type, a.target_id);

        sem_wait(&state->state_lock);
        state->pending_action = a;
        sem_post(&state->state_lock);

        sem_post(&state->action_ready);
        sem_wait(&state->turn_done_sem[eid]);
    }

    std::printf("[asp] NPC thread entity_id=%d (E%d) exiting\n", eid, local + 1);
    return nullptr;
}