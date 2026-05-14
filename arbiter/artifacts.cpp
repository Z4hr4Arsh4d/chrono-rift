
#include "artifacts.h"
#include "../common/constants.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

static pthread_t  g_detector_tid;
static bool       g_detector_running = false;
static int        g_wait_for[TOTAL_ENTITIES];   

static void log_to_arbiter(GameState* state, const char* msg) {

    int slot = state->log_head % MAX_LOG_LINES;
    strncpy(state->action_log[slot], msg, LOG_LINE_LEN - 1);
    state->action_log[slot][LOG_LINE_LEN - 1] = '\0';
    state->log_head = (state->log_head + 1) % MAX_LOG_LINES;
    printf("[arbiter:log] %s\n", msg);
    fflush(stdout);
}

// Public API


bool try_acquire_artifact(GameState* state, int artifact_id, int entity_id) {
    if (artifact_id < 0 || artifact_id >= NUM_ARTIFACTS) return false;

    sem_wait(&state->artifact_lock);
    ArtifactSlot& slot = state->artifacts[artifact_id];

    if (!slot.present) {
        sem_post(&state->artifact_lock);
        return false;
    }
    if (slot.locked_by == -1) {
        slot.locked_by = entity_id;
        g_wait_for[entity_id] = -1;
        sem_post(&state->artifact_lock);
        return true;
    }
    // Held by someone else -> record the wait
    g_wait_for[entity_id] = artifact_id;
    sem_post(&state->artifact_lock);
    return false;
}

void release_artifact(GameState* state, int artifact_id, int entity_id) {
    if (artifact_id < 0 || artifact_id >= NUM_ARTIFACTS) return;
    sem_wait(&state->artifact_lock);
    ArtifactSlot& slot = state->artifacts[artifact_id];
    if (slot.locked_by == entity_id) slot.locked_by = -1;
    sem_post(&state->artifact_lock);
}


int weapon_to_artifact(int weapon_id) {
    switch (weapon_id) {
        case 0: return ARTIFACT_SOLAR;     // WEAPON_SOLAR_CORE
        case 1: return ARTIFACT_LUNAR;     // WEAPON_LUNAR_BLADE
        default: return -1;
    }
}

void acquire_for_weapon(GameState* state, int weapon_id, int entity_id) {
    int aid = weapon_to_artifact(weapon_id);
    if (aid < 0) return;
  
    bool got = try_acquire_artifact(state, aid, entity_id);
    char msg[LOG_LINE_LEN];
    snprintf(msg, sizeof(msg),
        "Artifact %d %s by %s%d",
        aid,
        got ? "ACQUIRED" : "WAIT",
        entity_id < MAX_PLAYERS ? "P" : "E", entity_id);
    sem_wait(&state->state_lock);
    log_to_arbiter(state, msg);
    sem_post(&state->state_lock);
}

void release_for_weapon(GameState* state, int weapon_id, int entity_id) {
    int aid = weapon_to_artifact(weapon_id);
    if (aid < 0) return;
    release_artifact(state, aid, entity_id);
    char msg[LOG_LINE_LEN];
    snprintf(msg, sizeof(msg),
        "Artifact %d RELEASED by %s%d",
        aid,
        entity_id < MAX_PLAYERS ? "P" : "E", entity_id);
    sem_wait(&state->state_lock);
    log_to_arbiter(state, msg);
    sem_post(&state->state_lock);
}




static int find_holder_in_inventories(GameState* state, int weapon_id) {
    for (int e = 0; e < TOTAL_ENTITIES; e++) {
        if (!state->entities[e].alive) continue;
        for (int s = 0; s < INVENTORY_SLOTS; s++) {
            if (state->entities[e].inventory[s].id == weapon_id) {
                return e;
            }
        }
    }
    return -1;
}

void reconcile_artifacts_locked(GameState* state) {
    sem_wait(&state->artifact_lock);

    // SOLAR
    {
        int holder = find_holder_in_inventories(state, 0);  // WEAPON_SOLAR_CORE
        ArtifactSlot& slot = state->artifacts[ARTIFACT_SOLAR];
        if (holder >= 0) {
            if (slot.locked_by != holder) slot.locked_by = holder;
            slot.present = true;
        } else {
            slot.locked_by = -1;
            slot.present = true;
        }
    }
    // LUNAR
    {
        int holder = find_holder_in_inventories(state, 1);  // WEAPON_LUNAR_BLADE
        ArtifactSlot& slot = state->artifacts[ARTIFACT_LUNAR];
        if (holder >= 0) {
            if (slot.locked_by != holder) slot.locked_by = holder;
            slot.present = true;
        } else {
            slot.locked_by = -1;
            slot.present = true;
        }
    }
    {
        ArtifactSlot& slot = state->artifacts[ARTIFACT_ECLIPSE];
        if (slot.present && slot.locked_by >= 0) {
            if (!state->entities[slot.locked_by].alive) {
                slot.locked_by = -1;
                slot.present = false;
            }
        }
    }

    sem_post(&state->artifact_lock);
}

void introduce_eclipse_relic(GameState* state, int finder_id) {
    sem_wait(&state->artifact_lock);
    ArtifactSlot& slot = state->artifacts[ARTIFACT_ECLIPSE];
    if (!slot.present) {
        slot.present   = true;
        slot.locked_by = finder_id;
        char msg[LOG_LINE_LEN];
        snprintf(msg, sizeof(msg),
                 "Eclipse Relic introduced (held by %s%d)",
                 finder_id < MAX_PLAYERS ? "P" : "E", finder_id);
        sem_wait(&state->state_lock);
        log_to_arbiter(state, msg);
        sem_post(&state->state_lock);
    }
    sem_post(&state->artifact_lock);
}


static int find_cycle_locked(GameState* state) {
    for (int start = 0; start < TOTAL_ENTITIES; start++) {
        if (g_wait_for[start] == -1) continue;

        int cur = start;
        int hops = 0;
        while (hops < TOTAL_ENTITIES) {
            int art = g_wait_for[cur];
            if (art == -1) break;             // no more wait
            int holder = state->artifacts[art].locked_by;
            if (holder == -1) break;          // resource is free now
            if (holder == start) return start; // cycle detected
            cur = holder;
            hops++;
        }
    }
    return -1;
}

static void* detector_loop(void* arg) {
    GameState* state = (GameState*)arg;

    while (g_detector_running && state->status == GS_INIT) {
        usleep(50 * 1000);
    }

    while (g_detector_running && state->status == GS_RUNNING) {
        usleep(DEADLOCK_SCAN_MS * 1000);

        sem_wait(&state->artifact_lock);
        int cyc = find_cycle_locked(state);
        if (cyc != -1) {
            int loser = cyc;
            for (int a = 0; a < NUM_ARTIFACTS; a++) {
                if (state->artifacts[a].locked_by == loser) {
                    state->artifacts[a].locked_by = -1;
                    g_wait_for[loser] = -1;
                    char msg[LOG_LINE_LEN];
                    snprintf(msg, sizeof(msg),
                             "DEADLOCK DETECTED: forced %s%d to release artifact %d",
                             loser < MAX_PLAYERS ? "P" : "E", loser, a);
                    sem_wait(&state->state_lock);
                    log_to_arbiter(state, msg);
                    sem_post(&state->state_lock);
                    break;
                }
            }
        }
        sem_post(&state->artifact_lock);
    }
    return NULL;
}

void start_deadlock_detector(GameState* state) {
    for (int i = 0; i < TOTAL_ENTITIES; i++) g_wait_for[i] = -1;
    g_detector_running = true;
    pthread_create(&g_detector_tid, NULL, detector_loop, state);
}

void stop_deadlock_detector() {
    g_detector_running = false;
    pthread_join(g_detector_tid, NULL);
}
