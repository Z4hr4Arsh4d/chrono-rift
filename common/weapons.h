#pragma once
#include "game_state.h"




struct WeaponDef {
    int         id;
    const char* name;
    int         slot_size;
    int         damage;
};

static const WeaponDef WEAPON_DEFS[] = {
    { 0, "Solar Core",    10, 95 },
    { 1, "Lunar Blade",   10, 90 },
    { 2, "Iron Halberd",   7, 55 },
    { 3, "Venom Dagger",   4, 30 },
    { 4, "Thunderstaff",   6, 50 },
    { 5, "Obsidian Axe",   5, 45 },
    { 6, "Frostbow",       6, 48 },
    { 7, "Splinter Stick", 2, 12 },
};
static const int NUM_WEAPON_DEFS = 8;

inline const WeaponDef* get_weapon_def(int id) {
    if (id < 0 || id >= NUM_WEAPON_DEFS) return nullptr;
    return &WEAPON_DEFS[id];
}