#pragma once
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>



#define SHM_NAME            "/chrono_rift_state"

#define MAX_PLAYERS         4
#define MAX_ENEMIES         9
#define TOTAL_ENTITIES      (MAX_PLAYERS + MAX_ENEMIES)
#define INVENTORY_SLOTS     20
#define LTS_CAPACITY        40      // long-term storage
#define NUM_ARTIFACTS       3

#define MAX_LOG_LINES       10
#define LOG_LINE_LEN        80


inline bool is_player_id(int id) {
    return id >= 0 && id < MAX_PLAYERS;
}
inline bool is_enemy_id(int id) {
    return id >= MAX_PLAYERS && id < TOTAL_ENTITIES;
}
inline int  enemy_local_idx(int id) { 
    return id - MAX_PLAYERS; 
} 

enum EntityKind { 
    PLAYER, ENEMY
};

enum ActionType {
    ACT_NONE = 0,
    ACT_STRIKE,
    ACT_EXHAUST,
    ACT_USE_WEAPON,
    ACT_SWAP_IN,
    ACT_HEAL,
    ACT_SKIP,
    ACT_ULTIMATE
};

//Game statuses 
enum GameStatus {
    GS_INIT = 0,
    GS_RUNNING,
    GS_WIN,
    GS_LOSE,
    GS_QUIT
};

struct Weapon {
    int id;              // -1 = empty
    int slot_size;
    int damage;
    int start_slot;     
};

struct Entity {
    int         id;              // 0 .. TOTAL_ENTITIES-1
    EntityKind  kind;
    pid_t       owner_pid;       // HIP for players, ASP for enemies
    int         hp, max_hp;
    int         damage;
    int         speed;
    int         stamina, max_stamina;
    bool        alive;
    bool        stunned;
    double      stun_start_time; 

    Weapon      inventory[INVENTORY_SLOTS];
    int         lts_count;
    Weapon      long_term_storage[LTS_CAPACITY];

    char hero_name[16];    // "ALYA", "CHRONO", "FROG", "MAGNUS"
    int  hero_id;          // 0=alya 1=chrono 2=frog 3=magnus
};

struct Action {
    int        entity_id;
    ActionType type;
    int        target_id;    // for STRIKE / EXHAUST / USE_WEAPON (-1 if N/A)
    int        weapon_idx;   // for USE_WEAPON (index into actor.inventory; -1 if N/A)
    int        lts_idx;      // for SWAP_IN (index into actor.long_term_storage; -1 if N/A)
    bool       committed;    // Arbiter sets true after applying the action
};

//2 types of artifacts that can be picked up by players or enemies
// Each slot tracks whether the artifact is present in the world, and if not, which entity has it locked (i.e. is holding it or has it on cooldown).
struct ArtifactSlot {
    int  artifact_id;        // ARTIFACT_SOLAR / _LUNAR / _ECLIPSE
    bool present;            // Eclipse starts false; flips true on pickup
    int  locked_by;          
};

// GameState 

struct GameState {

    sem_t   state_lock;                         // guards everything below
    sem_t   artifact_lock;                      // guards artifacts[]
    sem_t   action_ready;                       // posted by actor, waited by Arbiter
    sem_t   turn_sem[TOTAL_ENTITIES];           // Arbiter wakes entity X
    sem_t   turn_done_sem[TOTAL_ENTITIES];      // Arbiter signals action applied

    GameStatus  status;
    int         current_turn_entity;            // -1 if no one's turn
    int         enemies_killed;
    pid_t       arbiter_pid;                    // HIP sends SIGTERM here on quit
    pid_t       asp_pid;                        // Arbiter sends SIGSTOP/SIGCONT here

    int         num_players;
    int         num_enemies;
    Entity      entities[TOTAL_ENTITIES];       // indexed by global entity id

    int         hero_slot[MAX_PLAYERS];

    ArtifactSlot artifacts[NUM_ARTIFACTS];
    Action       pending_action;

    struct WeaponDrop {
        int  weapon_id;    // -1 = none
        int  for_player;   // entity_id of the killing player
        bool pending;      // true = waiting for player response
        bool declined;     // true = player said no, enemy should pick up
    } pending_drop;

    char        action_log[MAX_LOG_LINES][LOG_LINE_LEN];
    int         log_head;                       // index of next write

    double      arrival_time[TOTAL_ENTITIES];
    double      completion_time[TOTAL_ENTITIES];
};