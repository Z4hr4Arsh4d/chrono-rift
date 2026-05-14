#pragma once



#define ROLL_PERSON_A           240630      
#define ROLL_PERSON_B           240701      
#define ROLL_PLAYER_SIDE        ROLL_PERSON_A
#define ROLL_ENEMY_SIDE         ROLL_PERSON_B
#define COMBINED_RNG_SEED       (ROLL_PERSON_A ^ ROLL_PERSON_B)

#define LAST_1_DIGIT(roll)      ((roll) % 10)
#define LAST_2_DIGITS(roll)     ((roll) % 100)
#define SECOND_LAST_DIGIT(roll) (((roll) / 10) % 10)

#define SCHED_TICK_MS           100     // stamina tick interval
#define NPC_TIMEOUT_SEC         3       // Arbiter treats no-move as Skip
#define STUN_DURATION_SEC       3       // spec-defined
#define ULTIMATE_PAUSE_SEC      10      // spec-defined
#define DEADLOCK_SCAN_MS        500     // deadlock detector wake interval

#define PLAYER_MAX_STAMINA      100
#define ENEMY_MAX_STAMINA       150

#define WEAPON_SOLAR_CORE       0
#define WEAPON_LUNAR_BLADE      1
#define WEAPON_IRON_HALBERD     2
#define WEAPON_VENOM_DAGGER     3
#define WEAPON_THUNDERSTAFF     4
#define WEAPON_OBSIDIAN_AXE     5
#define WEAPON_FROSTBOW         6
#define WEAPON_SPLINTER_STICK   7

#define ARTIFACT_SOLAR          0
#define ARTIFACT_LUNAR          1
#define ARTIFACT_ECLIPSE        2

#define STUN_DAMAGE_THRESHOLD   25

#define KILLS_TO_WIN            10