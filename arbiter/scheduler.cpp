#include "scheduler.h"
#include "../common/constants.h"
#include "signals.h"
#include "artifacts.h"
#include "../common/weapons.h"
#include "../common/inventory.h"
#include "../common/rng.h"


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <ctime>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>

static FILE* g_csv = NULL;
static int    g_tick = 0;
static double g_frac_stamina[TOTAL_ENTITIES];   // fractional stamina accumulator
static const char TURNAROUND_CSV[] = "turnaround.csv";

// Small helpers


static double monotonic_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void log_action_locked(GameState* state, const char* fmt, ...) {
    char buf[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    int slot = state->log_head % MAX_LOG_LINES;
    strncpy(state->action_log[slot], buf, LOG_LINE_LEN - 1);
    state->action_log[slot][LOG_LINE_LEN - 1] = '\0';
    state->log_head = (state->log_head + 1) % MAX_LOG_LINES;

    // Mirror to stdout so the Arbiter terminal shows a live trace
    printf("[arbiter:log] %s\n", buf);
    fflush(stdout);
}

static void csv_open() {
    g_csv = fopen(TURNAROUND_CSV, "w");
    if (!g_csv) {
        fprintf(stderr, "[scheduler] cannot open %s: %s\n",
            TURNAROUND_CSV, strerror(errno));
        return;
    }
    fprintf(g_csv, "tick,entity_id,role,predicted_arrival_s,actual_arrival_s,completion_s,turnaround_s,action\n");
    fflush(g_csv);
}

static void csv_log_turn(int eid, const char* role, int speed, int max_stam,
    double arr, double comp, const char* action) {
    if (!g_csv) return;
    double ta = (arr > 0.0) ? (comp - arr) : 0.0;
    double predicted = (speed > 0) ? ((double)max_stam / (double)speed) : 0.0;
    fprintf(g_csv, "%d,%d,%s,%.6f,%.6f,%.6f,%.6f,%s\n",
        g_tick, eid, role, predicted, arr, comp, ta, action);
    fflush(g_csv);
}

// Helper: empty out an inventory slot or LTS slot
static void clear_weapon(Weapon& w) {
    w.id = -1;
    w.slot_size = 0;
    w.damage = 0;
    w.start_slot = -1;
}

static const double STUN_PROC_CHANCE = 0.30;

static void maybe_stun_target_locked(GameState* state, int attacker_id,
                                     int target_id, int damage_dealt,
                                     const char* attack_label) {
    (void)attacker_id;
    if (target_id < 0 || target_id >= TOTAL_ENTITIES) return;
    Entity& tgt = state->entities[target_id];
    if (!tgt.alive)   return;
    if (tgt.stunned)  return;   // already stunned

    bool by_threshold = damage_dealt >= STUN_DAMAGE_THRESHOLD;
    bool by_proc      = rand_chance(STUN_PROC_CHANCE);
    if (!by_threshold && !by_proc) return;

    tgt.stunned         = true;
    tgt.stun_start_time = now_seconds();
    if (tgt.owner_pid > 0) {
        kill(tgt.owner_pid, SIGUSR1);   // notify the owning process
    }
    log_action_locked(state, "%s%d STUNNED by %s (%s)",
        is_player_id(target_id) ? "P" : "E", target_id, attack_label,
        by_threshold ? "high-damage" : "proc");
}

// Stat initialisation (spec Section 10)


void scheduler_init_entities(GameState* state, const SchedulerConfig& cfg) {
    sem_wait(&state->state_lock);

    int n_p = cfg.num_players;
    if (n_p < 1)            n_p = 1;
    if (n_p > MAX_PLAYERS)  n_p = MAX_PLAYERS;

    // Random enemy count [2..MAX_ENEMIES] unless CLI overrode it
    int n_e;
    if (cfg.num_enemies > 0) {
        n_e = cfg.num_enemies;      // CLI explicitly set it
    }
    else {
        // Use the seeded RNG (deterministic from roll numbers)
        n_e = rand_range(2, MAX_ENEMIES);
    }
    if (n_e < 2)            n_e = 2;
    if (n_e > MAX_ENEMIES)  n_e = MAX_ENEMIES;

    state->num_players = n_p;
    state->num_enemies = n_e;
    state->enemies_killed = 0;

    int player_damage = LAST_1_DIGIT(ROLL_PLAYER_SIDE) + 10;
    int player_speed = 100 / n_p;

    const char* hero_names[] = {"ALYA", "CHRONO", "FROG", "MAGNUS"};
    for (int i = 0; i < n_p; i++) {
        int hid = state->hero_slot[i];
        if (hid < 0 || hid >= 4) hid = i;
        strncpy(state->entities[i].hero_name, hero_names[hid], 15);
        state->entities[i].hero_name[15] = '\0';
        state->entities[i].hero_id = hid;
    }

    // Players: ids 0 .. MAX_PLAYERS - 1
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Entity& e = state->entities[i];
        e.id = i;
        e.kind = PLAYER;
        e.owner_pid = 0;       // HIP fills in when its thread attaches
        e.stunned = false;
        e.stun_start_time = 0.0;
        e.lts_count = 0;
        for (int s = 0; s < INVENTORY_SLOTS; ++s) clear_weapon(e.inventory[s]);
        for (int s = 0; s < LTS_CAPACITY; ++s) clear_weapon(e.long_term_storage[s]);

        if (i < n_p) {
            e.alive = true;
            e.max_hp = LAST_2_DIGITS(ROLL_PLAYER_SIDE) + rand_range(100, 1000);
            e.hp = e.max_hp;
            e.damage = player_damage;
            e.speed = player_speed;
            e.max_stamina = PLAYER_MAX_STAMINA;
            e.stamina = 0;
        }
        else {
            e.alive = false;
            e.hp = e.max_hp = e.damage = e.speed = 0;
            e.max_stamina = e.stamina = 0;
        }
        state->arrival_time[i] = -1.0;
        state->completion_time[i] = 0.0;
        g_frac_stamina[i] = 0.0;
    }

    // Enemies: ids MAX_PLAYERS .. MAX_PLAYERS + MAX_ENEMIES - 1
    int enemy_damage = SECOND_LAST_DIGIT(ROLL_ENEMY_SIDE) + 10;
    int enemy_hp_base = LAST_2_DIGITS(ROLL_ENEMY_SIDE);
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        int gid = MAX_PLAYERS + i;
        Entity& e = state->entities[gid];
        e.id = gid;
        e.kind = ENEMY;
        e.owner_pid = 0;       // ASP fills in when its NPC thread attaches
        e.stunned = false;
        e.stun_start_time = 0.0;
        e.lts_count = 0;
        for (int s = 0; s < INVENTORY_SLOTS; ++s) clear_weapon(e.inventory[s]);
        for (int s = 0; s < LTS_CAPACITY; ++s) clear_weapon(e.long_term_storage[s]);

        if (i < n_e) {
            e.alive = true;
            int hp_random = rand_range(50, 200);
            int spd_random = rand_range(10, 30);
            e.max_hp = enemy_hp_base + hp_random;
            e.hp = e.max_hp;
            e.damage = enemy_damage;
            e.speed = spd_random;
            e.max_stamina = ENEMY_MAX_STAMINA;
            e.stamina = 0;
        }
        else {
            e.alive = false;
            e.hp = e.max_hp = e.damage = e.speed = 0;
            e.max_stamina = e.stamina = 0;
        }
        state->arrival_time[gid] = -1.0;
        state->completion_time[gid] = 0.0;
        g_frac_stamina[gid] = 0.0;
    }

    state->current_turn_entity = -1;
    log_action_locked(state, "Game start: %dP vs %dE (target %d kills)",
        n_p, n_e, cfg.win_kill_target);


    //deadlock demo
    if (cfg.deadlock_demo && n_p >= 1 && n_e >= 1) {
        // Give P0 Solar Core
        give_weapon(state->entities[0], 0);  // WEAPON_SOLAR_CORE
        // Give E4 (first enemy) Lunar Blade
        int first_enemy = MAX_PLAYERS;
        give_weapon(state->entities[first_enemy], 1);  // WEAPON_LUNAR_BLADE

        log_action_locked(state, "DEMO: P0 holds Solar; E0 holds Lunar");
        log_action_locked(state, "DEMO: each will wait on the other...");
    }

    sem_post(&state->state_lock);

    if (cfg.deadlock_demo && n_p >= 1 && n_e >= 1) {
        sem_wait(&state->state_lock);
        reconcile_artifacts_locked(state);
        sem_post(&state->state_lock);

        int first_enemy = MAX_PLAYERS;
        // P0 wants Lunar (held by E0) -> records wait
        try_acquire_artifact(state, ARTIFACT_LUNAR, 0);
        // E0 wants Solar (held by P0) -> records wait
        try_acquire_artifact(state, ARTIFACT_SOLAR, first_enemy);
    }
}



static void tick_stamina_locked(GameState* state, int tick_ms) {
    clear_expired_stuns_locked(state);   // unstun entities whose 3s elapsed
    for (int i = 0; i < TOTAL_ENTITIES; ++i) {
        Entity& e = state->entities[i];
        if (!e.alive)                   continue;
        if (e.stunned)                  continue;
        if (e.stamina >= e.max_stamina) continue;

        
        g_frac_stamina[i] += (double)e.speed * tick_ms / 1000.0;
        int delta = (int)g_frac_stamina[i];
        if (delta > 0) {
            g_frac_stamina[i] -= delta;
            e.stamina += delta;
            if (e.stamina > e.max_stamina) e.stamina = e.max_stamina;
        }

        if (e.stamina >= e.max_stamina && state->arrival_time[i] < 0.0) {
            state->arrival_time[i] = monotonic_now();
        }
    }
}

static int pick_turn_locked(GameState* state) {
    for (int i = 0; i < TOTAL_ENTITIES; ++i) {
        Entity& e = state->entities[i];
        if (!e.alive)                  continue;
        if (e.stunned)                 continue;
        if (e.stamina < e.max_stamina) continue;
        return i;
    }
    return -1;
}



static const char* apply_action_locked(GameState* state, const Action& act) {
    Entity& actor = state->entities[act.entity_id];
    const char* role = is_player_id(act.entity_id) ? "P" : "E";

    switch (act.type) {

    case ACT_STRIKE: {
        if (act.target_id < 0 || act.target_id >= TOTAL_ENTITIES) {
            log_action_locked(state, "%s%d: invalid target -> Skip", role, act.entity_id);
            actor.stamina = actor.max_stamina / 2;
            g_frac_stamina[act.entity_id] = 0.0;
            return "InvalidStrike";
        }
        Entity& target = state->entities[act.target_id];
        if (!target.alive) {
            log_action_locked(state, "%s%d: Strike on dead %d -> Skip",
                role, act.entity_id, act.target_id);
            actor.stamina = actor.max_stamina / 2;
            g_frac_stamina[act.entity_id] = 0.0;
            return "StrikeMiss";
        }
        int dmg = actor.damage;
        target.hp -= dmg;
        const char* trole = is_player_id(act.target_id) ? "P" : "E";
        if (target.hp <= 0) {
            target.hp = 0;
            target.alive = false;
            // weapon drop code directly here
            bool enemy_has_weapon = false;
            if (is_enemy_id(act.target_id)) {
                for (int s = 0; s < INVENTORY_SLOTS; s++) {
                    if (target.inventory[s].id >= 0 && 
                        target.inventory[s].start_slot == s) {
                        enemy_has_weapon = true;
                        break;
                    }
                }
                if (enemy_has_weapon) {
                    // Spec section 10: "If an NPC holds a weapon, the weapon will not be dropped when it dies."
                    log_action_locked(state, "E%d held weapon -> no drop (spec rule)",
                        enemy_local_idx(act.target_id) + 1);
                }
                else if (!state->pending_drop.pending && rand_chance(0.50)) {
                    int drop_id = rand_range(0, NUM_WEAPON_DEFS - 1);
                    state->pending_drop.weapon_id  = drop_id;
                    state->pending_drop.for_player = act.entity_id;
                    state->pending_drop.pending    = true;
                    log_action_locked(state, "E%d dropped %s!",
                        enemy_local_idx(act.target_id) + 1,
                        WEAPON_DEFS[drop_id].name);
                }
            }
            log_action_locked(state, "%s%d Strike -> %s%d for %d (KILLED)",
                role, act.entity_id, trole, act.target_id, dmg);
        }
        else {
            log_action_locked(state, "%s%d Strike -> %s%d for %d (HP %d/%d)",
                role, act.entity_id, trole, act.target_id, dmg,
                target.hp, target.max_hp);
            // Try to stun on Strike ()
            maybe_stun_target_locked(state, act.entity_id, act.target_id,
                                     dmg, "Strike");
        }
        actor.stamina = 0;
        g_frac_stamina[act.entity_id] = 0.0;
        return "Strike";
    }

    case ACT_SKIP: {
        log_action_locked(state, "%s%d Skip", role, act.entity_id);
        actor.stamina = actor.max_stamina / 2;
        g_frac_stamina[act.entity_id] = 0.0;
        return "Skip";
    }

    case ACT_EXHAUST: {
        if (act.target_id < 0 || act.target_id >= TOTAL_ENTITIES ||
            !state->entities[act.target_id].alive) {
            log_action_locked(state, "%s%d Exhaust missed -> Skip",
                              role, act.entity_id);
            actor.stamina = actor.max_stamina / 2;
            g_frac_stamina[act.entity_id] = 0.0;
            return "ExhaustMiss";
        }
        Entity& tgt = state->entities[act.target_id];
        int reduction = actor.damage;
        tgt.stamina -= reduction;
        if (tgt.stamina < 0) tgt.stamina = 0;
        g_frac_stamina[act.target_id] = 0.0;
        log_action_locked(state, "%s%d Exhaust -> %s%d (-%d stamina)",
                          role, act.entity_id,
                          is_player_id(act.target_id) ? "P" : "E",
                          act.target_id, reduction);
        actor.stamina = 0;
        g_frac_stamina[act.entity_id] = 0.0;
        // Try to stun on Exhaust ()
        maybe_stun_target_locked(state, act.entity_id, act.target_id,
                                 reduction, "Exhaust");
        return "Exhaust";
    }

    case ACT_USE_WEAPON: {
        if (act.weapon_idx < 0 || act.weapon_idx >= INVENTORY_SLOTS ||
            actor.inventory[act.weapon_idx].id < 0) {
            log_action_locked(state, "%s%d UseWeapon: empty slot -> Skip",
                            role, act.entity_id);
            actor.stamina = actor.max_stamina / 2;
            g_frac_stamina[act.entity_id] = 0.0;
            return "WeaponMiss";
        }
        if (act.target_id < 0 || act.target_id >= TOTAL_ENTITIES ||
            !state->entities[act.target_id].alive) {
            log_action_locked(state, "%s%d UseWeapon bad target -> Skip",
                            role, act.entity_id);
            actor.stamina = actor.max_stamina / 2;
            g_frac_stamina[act.entity_id] = 0.0;
            return "WeaponBadTarget";
        }
        Entity& tgt = state->entities[act.target_id];
        int dmg = actor.inventory[act.weapon_idx].damage;
        tgt.hp -= dmg;
        const char* trole = is_player_id(act.target_id) ? "P" : "E";
        if (tgt.hp <= 0) {
            tgt.hp = 0;
            tgt.alive = false;
            // Weapon drop on kill
            if (is_enemy_id(act.target_id) && is_player_id(act.entity_id)) {
                bool enemy_has_weapon = false;
                for (int s = 0; s < INVENTORY_SLOTS; s++) {
                    if (tgt.inventory[s].id >= 0 &&
                        tgt.inventory[s].start_slot == s) {
                        enemy_has_weapon = true; break;
                    }
                }
                if (!enemy_has_weapon &&
                    !state->pending_drop.pending &&
                    rand_chance(0.50)) {
                    int drop_id = rand_range(0, NUM_WEAPON_DEFS - 1);
                    state->pending_drop.weapon_id  = drop_id;
                    state->pending_drop.for_player = act.entity_id;
                    state->pending_drop.pending    = true;
                    state->pending_drop.declined   = false;
                    log_action_locked(state, "E%d dropped %s!",
                        enemy_local_idx(act.target_id) + 1,
                        WEAPON_DEFS[drop_id].name);
                }
            }
            log_action_locked(state, "%s%d Weapon[%d] -> %s%d for %d (KILLED)",
                            role, act.entity_id, act.weapon_idx,
                            trole, act.target_id, dmg);
        }
        else {
            log_action_locked(state, "%s%d Weapon[%d] -> %s%d for %d (HP %d/%d)",
                            role, act.entity_id, act.weapon_idx,
                            trole, act.target_id, dmg,
                            tgt.hp, tgt.max_hp);
        }
        actor.stamina = 0;
        g_frac_stamina[act.entity_id] = 0.0;
        return "UseWeapon";
    }

    case ACT_HEAL: {
        int restore = actor.max_hp / 10;          // spec: 10% of max HP
        actor.hp += restore;
        if (actor.hp > actor.max_hp) actor.hp = actor.max_hp;
        log_action_locked(state, "%s%d Heal +%d (HP %d/%d)",
                          role, act.entity_id, restore,
                          actor.hp, actor.max_hp);
        actor.stamina = 0;
        g_frac_stamina[act.entity_id] = 0.0;
        return "Heal";
    }

    case ACT_SWAP_IN: {
        if (act.lts_idx >= 0 && act.lts_idx < actor.lts_count) {
        bool ok = swap_in(actor, act.lts_idx);   // capture return value
        log_action_locked(state, "%s%d SwapIn lts[%d] result=%s  lts_now=%d",
                        role, act.entity_id, act.lts_idx,
                        ok ? "OK" : "FAIL", actor.lts_count);
        } else {
            log_action_locked(state, "%s%d SwapIn invalid idx=%d lts_count=%d",
                            role, act.entity_id, act.lts_idx, actor.lts_count);
        }
        actor.stamina = 0;
        g_frac_stamina[act.entity_id] = 0.0;
        return "SwapIn";
    }

    case ACT_ULTIMATE: {
        bool has_solar = false, has_lunar = false;
        for (int s = 0; s < INVENTORY_SLOTS; s++) {
            if (actor.inventory[s].id == WEAPON_SOLAR_CORE)  has_solar = true;
            if (actor.inventory[s].id == WEAPON_LUNAR_BLADE) has_lunar = true;
        }
        if (!has_solar || !has_lunar) {

            if (!has_solar) try_acquire_artifact(state, ARTIFACT_SOLAR, act.entity_id);
            if (!has_lunar) try_acquire_artifact(state, ARTIFACT_LUNAR, act.entity_id);
            log_action_locked(state,
                "%s%d Ultimate denied (need Solar+Lunar) -> Skip",
                role, act.entity_id);
            actor.stamina = actor.max_stamina / 2;
            g_frac_stamina[act.entity_id] = 0.0;
            return "UltimateDenied";
        }
        if (act.target_id < 0 || act.target_id >= TOTAL_ENTITIES ||
            !state->entities[act.target_id].alive) {
            log_action_locked(state, "%s%d Ultimate bad target -> Skip",
                              role, act.entity_id);
            actor.stamina = actor.max_stamina / 2;
            g_frac_stamina[act.entity_id] = 0.0;
            return "UltimateBadTarget";
        }
        Entity& tgt = state->entities[act.target_id];
        int dmg = actor.damage * 5;               // big damage by design
        tgt.hp -= dmg;
        if (tgt.hp <= 0) { tgt.hp = 0; tgt.alive = false; }
        log_action_locked(state,
            "%s%d ULTIMATE -> %s%d for %d%s [ASP suspends 10s]",
            role, act.entity_id,
            is_player_id(act.target_id) ? "P" : "E",
            act.target_id, dmg,
            tgt.alive ? "" : " (KILLED)");
        // Trigger the signal-based pause AFTER applying damage
        trigger_ultimate_pause(state);
        actor.stamina = 0;
        g_frac_stamina[act.entity_id] = 0.0;
        return "Ultimate";
    }

    case ACT_NONE:
    default:
        log_action_locked(state, "%s%d unknown action %d -> Skip",
                          role, act.entity_id, (int)act.type);
        actor.stamina = actor.max_stamina / 2;
        g_frac_stamina[act.entity_id] = 0.0;
        return "Unknown";
    }
}

// Win / lose detection


static bool check_end_locked(GameState* state, const SchedulerConfig& cfg) {
    int alive_p = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (state->entities[i].alive) ++alive_p;

    if (alive_p == 0) {
        state->status = GS_LOSE;
        log_action_locked(state, "All players defeated. LOSE.");
        return true;
    }
    if (state->enemies_killed >= cfg.win_kill_target) {
        state->status = GS_WIN;
        log_action_locked(state, "Killed %d enemies. WIN.", state->enemies_killed);
        return true;
    }
    return false;
}



static void respawn_enemies_locked(GameState* state) {
    int enemy_damage = SECOND_LAST_DIGIT(ROLL_ENEMY_SIDE) + 10;
    int enemy_hp_base = LAST_2_DIGITS(ROLL_ENEMY_SIDE);

    for (int i = 0; i < MAX_ENEMIES; ++i) {
        int gid = MAX_PLAYERS + i;
        Entity& e = state->entities[gid];
        if (e.alive)        continue;
        if (e.max_hp == 0)  continue;  // slot was never enabled at game start

        int hp_random = rand_range(50, 200);
        int spd_random = rand_range(10, 30);

        e.alive = true;
        e.max_hp = enemy_hp_base + hp_random;
        e.hp = e.max_hp;
        e.damage = enemy_damage;
        e.speed = spd_random;
        e.max_stamina = ENEMY_MAX_STAMINA;
        e.stamina = 0;
        e.stunned = false;
        e.stun_start_time = 0.0;
        state->arrival_time[gid] = -1.0;
        state->completion_time[gid] = 0.0;
        g_frac_stamina[gid] = 0.0;
        log_action_locked(state, "E%d respawned (HP %d, SPD %d)",
            gid, e.max_hp, e.speed);
    }
}


static Action gen_test_action_locked(GameState* state, int actor_id) {
    Action a;
    memset(&a, 0, sizeof(a));
    a.entity_id = actor_id;
    a.target_id = -1;
    a.weapon_idx = -1;
    a.lts_idx = -1;
    a.type = ACT_SKIP;
    a.committed = false;

    if (is_player_id(actor_id)) {
        for (int i = MAX_PLAYERS; i < TOTAL_ENTITIES; ++i) {
            if (state->entities[i].alive) {
                a.target_id = i; a.type = ACT_STRIKE; break;
            }
        }
    }
    else {
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (state->entities[i].alive) {
                a.target_id = i; a.type = ACT_STRIKE; break;
            }
        }
    }
    return a;
}



static Action wait_for_action(GameState* state, int actor_id) {
    Action a;
    memset(&a, 0, sizeof(a));

    const int  POLL_MS           = 100;
    const bool is_npc            = is_enemy_id(actor_id);
    const int  PLAYER_TIMEOUT_MS = 60000;

    if (is_npc) {
        arm_npc_turn_timeout(NPC_TIMEOUT_SEC);
    }

    int waited_ms = 0;
    while (true) {
        if (state->status != GS_RUNNING) {
            if (is_npc) cancel_npc_turn_timeout();
            a.entity_id = actor_id; a.target_id = -1;
            a.weapon_idx = -1; a.lts_idx = -1;
            a.type = ACT_SKIP; a.committed = false;
            return a;
        }

        if (sem_trywait(&state->action_ready) == 0) {
            if (is_npc) cancel_npc_turn_timeout();
            sem_wait(&state->state_lock);
            a = state->pending_action;
            sem_post(&state->state_lock);
            if (a.entity_id != actor_id) {
                fprintf(stderr,
                    "[scheduler] WARN: pending_action.entity_id=%d, expected %d\n",
                    a.entity_id, actor_id);
                a.entity_id = actor_id;
            }
            return a;
        }

        // SIGALRM-driven timeout for NPCs (spec section 8)
        if (is_npc && g_npc_timeout_fired) {
            fprintf(stderr,
                "[scheduler] SIGALRM fired -> NPC %d timeout, treating as Skip\n",
                actor_id);
            a.entity_id = actor_id; a.target_id = -1;
            a.weapon_idx = -1; a.lts_idx = -1;
            a.type = ACT_SKIP; a.committed = false;
            return a;
        }

        // Player polling timeout (no signal needed; humans are humans)
        if (!is_npc && waited_ms >= PLAYER_TIMEOUT_MS) {
            fprintf(stderr, "[scheduler] player %d timeout -> Skip\n", actor_id);
            a.entity_id = actor_id; a.target_id = -1;
            a.weapon_idx = -1; a.lts_idx = -1;
            a.type = ACT_SKIP; a.committed = false;
            return a;
        }

        usleep(POLL_MS * 1000);
        waited_ms += POLL_MS;
    }
}



SchedulerConfig scheduler_default_config() {
    SchedulerConfig c;
    c.num_players = 1;
    c.num_enemies = -1;
    c.win_kill_target = KILLS_TO_WIN;
    c.tick_ms = SCHED_TICK_MS;
    c.test_mode = false;
    c.test_max_ticks = 6000;   // 10 minutes at 100ms ticks
    c.deadlock_demo = false;
    c.mp_mode = false;
    return c;
}

void scheduler_run(GameState* state, const SchedulerConfig& cfg) {
    csv_open();
    g_tick = 0;

    fprintf(stderr,
        "[scheduler] starting. mode=%s tick_ms=%d kill_target=%d\n",
        cfg.test_mode ? "TEST" : "PROD", cfg.tick_ms, cfg.win_kill_target);

    while (state->status == GS_RUNNING) {
        // Tick clock
        usleep(cfg.tick_ms * 1000);
        g_tick++;

        if (cfg.test_mode && g_tick >= cfg.test_max_ticks) {
            fprintf(stderr, "[scheduler] test cap reached (%d ticks)\n", g_tick);
            sem_wait(&state->state_lock);
            state->status = GS_QUIT;
            sem_post(&state->state_lock);
            break;
        }

        // Stamina + turn pick
        sem_wait(&state->state_lock);
        tick_stamina_locked(state, cfg.tick_ms);
        reconcile_artifacts_locked(state);   // keep artifact table in sync with inventories
        int actor = pick_turn_locked(state);
        if (actor != -1) state->current_turn_entity = actor;
        sem_post(&state->state_lock);

        if (actor == -1) continue;

        double arr = state->arrival_time[actor];

        // Get an action
        Action act;
        if (cfg.test_mode) {
            sem_wait(&state->state_lock);
            act = gen_test_action_locked(state, actor);
            sem_post(&state->state_lock);
        }
        else {
            // Wake the process that owns this entity
            sem_post(&state->turn_sem[actor]);
            act = wait_for_action(state, actor);
        }

        //  Apply
        sem_wait(&state->state_lock);
        const char* tag = apply_action_locked(state, act);

        //  Count enemy kills, mark commit, check end
        if ((act.type == ACT_STRIKE || act.type == ACT_USE_WEAPON || act.type == ACT_ULTIMATE) &&
            is_enemy_id(act.target_id) &&
            !state->entities[act.target_id].alive) {
            state->enemies_killed++;
            // Spawn Eclipse Relic after 3rd kill
            if (state->enemies_killed == 3 &&
                !state->artifacts[ARTIFACT_ECLIPSE].present) {
                sem_post(&state->state_lock);   // release before artifact_lock
                introduce_eclipse_relic(state, act.entity_id);
                sem_wait(&state->state_lock);
            }
        }
        state->pending_action.committed = true;
        bool ended = check_end_locked(state, cfg);

        //  Respawn dead enemy slots if game still going
        if (!ended) respawn_enemies_locked(state);

        double comp = monotonic_now();
        state->completion_time[actor] = comp;
        state->current_turn_entity = -1;
        sem_post(&state->state_lock);

        // Tell the actor we're done
        if (!cfg.test_mode) sem_post(&state->turn_done_sem[actor]);

        // Turnaround log
        csv_log_turn(actor, is_player_id(actor) ? "player" : "enemy",
            state->entities[actor].speed,
            state->entities[actor].max_stamina,
            arr, comp, tag);
        state->arrival_time[actor] = -1.0;
    }

    if (g_csv) { fclose(g_csv); g_csv = NULL; }
    fprintf(stderr,
        "[scheduler] exiting. final status=%d (1=run 2=win 3=lose 4=quit) tick=%d kills=%d\n",
        (int)state->status, g_tick, state->enemies_killed);
}