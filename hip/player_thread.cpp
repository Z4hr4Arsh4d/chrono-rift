#include "player_thread.h"
#include "ui_event.h"
#include "../common/game_state.h"
#include "../common/constants.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <csignal>

extern volatile sig_atomic_t g_stun_signaled;

void* player_thread_main(void* arg) {
    PlayerThreadArgs* pargs = static_cast<PlayerThreadArgs*>(arg);
    GameState* state        = pargs->state;
    int        eid          = pargs->entity_id;

    // Register PID into the entity slot so Arbiter can signal us later.
    sem_wait(&state->state_lock);
    state->entities[eid].owner_pid = getpid();
    sem_post(&state->state_lock);

    std::printf("[hip] player thread %d registered (entity_id=%d, pid=%d)\n",
                eid, eid, getpid());

    while (state->status == GS_RUNNING) {

        sem_wait(&state->turn_sem[eid]);

        if (state->status != GS_RUNNING) break;

    
        sem_wait(&state->state_lock);
        bool stunned_now = state->entities[eid].stunned;
        sem_post(&state->state_lock);
        if (stunned_now) {
            std::printf("[hip] player %d stunned -> sleeping %ds\n",
                        eid, STUN_DURATION_SEC);
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

        g_ui_bus.active_player = eid;

        sem_wait(&g_ui_bus.event_ready);

        UIEvent ue = g_ui_bus.event;
        g_ui_bus.active_player = -1;   // clear immediately

        Action a;
        std::memset(&a, 0, sizeof(a));
        a.entity_id  = eid;
        a.type       = ue.type;
        a.target_id  = ue.target_id;
        a.weapon_idx = ue.weapon_idx;
        a.lts_idx    = ue.lts_idx;
        a.committed  = false;

        sem_wait(&state->state_lock);
        state->pending_action = a;
        sem_post(&state->state_lock);

        sem_post(&state->action_ready);

        sem_wait(&state->turn_done_sem[eid]);

        std::printf("[hip] player %d turn done (action=%d)\n", eid, (int)a.type);
    }

    g_ui_bus.active_player = -1;   // safety clear on exit
    std::printf("[hip] player thread %d exiting\n", eid);
    return nullptr;
}