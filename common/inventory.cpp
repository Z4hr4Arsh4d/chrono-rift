#include "inventory.h"
#include "../common/weapons.h"

#include <cstring>
#include <cstdio>


static void clear_slot(Weapon& w) {
    w.id         = -1;
    w.slot_size  = 0;
    w.damage     = 0;
    w.start_slot = -1;
}

static Weapon evict_weapon(Entity& e, int start_slot) {
    Weapon removed;
    removed.id         = -1;
    removed.slot_size  = 0;
    removed.damage     = 0;
    removed.start_slot = -1;

    // Find the weapon sitting at start_slot to get its size
    if (e.inventory[start_slot].id < 0) return removed;

    removed = e.inventory[start_slot];

    // Clear every slot occupied by this weapon
    for (int s = 0; s < INVENTORY_SLOTS; s++) {
        if (e.inventory[s].start_slot == start_slot) {
            clear_slot(e.inventory[s]);
        }
    }
    return removed;
}

static bool push_lts(Entity& e, const Weapon& w) {
    if (e.lts_count >= LTS_CAPACITY) return false;
    e.long_term_storage[e.lts_count++] = w;
    return true;
}



bool try_place_weapon(Entity& e, int weapon_id) {
    const WeaponDef* def = get_weapon_def(weapon_id);
    if (!def) return false;

    int slot_size = def->slot_size;

    for (int start = 0; start <= INVENTORY_SLOTS - slot_size; start++) {
        bool fits = true;
        for (int s = start; s < start + slot_size; s++) {
            if (e.inventory[s].id != -1) { fits = false; break; }
        }
        if (!fits) continue;

        // Place the weapon across all slots
        for (int s = start; s < start + slot_size; s++) {
            e.inventory[s].id         = weapon_id;
            e.inventory[s].slot_size  = slot_size;
            e.inventory[s].damage     = def->damage;
            e.inventory[s].start_slot = start;
        }
        return true;
    }
    return false;
}



bool swap_out_and_place(Entity& e, int weapon_id) {
    const WeaponDef* def = get_weapon_def(weapon_id);
    if (!def) return false;

    int slot_size = def->slot_size;

    int best_start      = -1;
    int best_count      = INVENTORY_SLOTS + 1;  // sentinel: worse than any real window

    for (int start = 0; start <= INVENTORY_SLOTS - slot_size; start++) {
        int   seen[INVENTORY_SLOTS];
        int   seen_count = 0;

        for (int s = start; s < start + slot_size; s++) {
            if (e.inventory[s].id == -1) continue;  // empty slot, nothing to evict

            int ws = e.inventory[s].start_slot;
            // Check if already in seen[]
            bool found = false;
            for (int k = 0; k < seen_count; k++) {
                if (seen[k] == ws) { found = true; break; }
            }
            if (!found) seen[seen_count++] = ws;
        }

        if (seen_count < best_count ||
            (seen_count == best_count && start < best_start)) {
            best_count = seen_count;
            best_start = start;
        }
    }

    if (best_start < 0) return false;   // shouldn't happen if slot_size <= 20

    // Collect the distinct start_slots to evict (re-scan best window)
    int   evict_starts[INVENTORY_SLOTS];
    int   evict_count = 0;
    for (int s = best_start; s < best_start + slot_size; s++) {
        if (e.inventory[s].id == -1) continue;
        int ws = e.inventory[s].start_slot;
        bool found = false;
        for (int k = 0; k < evict_count; k++) {
            if (evict_starts[k] == ws) { found = true; break; }
        }
        if (!found) evict_starts[evict_count++] = ws;
    }

    // Evict each weapon to LTS
    for (int k = 0; k < evict_count; k++) {
        Weapon removed = evict_weapon(e, evict_starts[k]);
        if (removed.id < 0) continue;  // already gone (shouldn't happen)
        if (!push_lts(e, removed)) {
            fprintf(stderr, "[inventory] LTS full, cannot evict weapon %d\n",
                    removed.id);
            return false;
        }
    }

    // Now place the new weapon in the cleared window
    for (int s = best_start; s < best_start + slot_size; s++) {
        e.inventory[s].id         = weapon_id;
        e.inventory[s].slot_size  = slot_size;
        e.inventory[s].damage     = def->damage;
        e.inventory[s].start_slot = best_start;
    }
    return true;
}



bool swap_in(Entity& e, int lts_index) {
    if (lts_index < 0 || lts_index >= e.lts_count) return false;

    Weapon w = e.long_term_storage[lts_index];
    if (w.id < 0) return false;

    // Remove from LTS — shift entries left
    for (int i = lts_index; i < e.lts_count - 1; i++) {
        e.long_term_storage[i] = e.long_term_storage[i + 1];
    }
    clear_slot(e.long_term_storage[e.lts_count - 1]);
    e.lts_count--;

    // Try to place in inventory
    if (try_place_weapon(e, w.id)) return true;
    if (swap_out_and_place(e, w.id)) return true;

    // Both failed (LTS full during swap_out) — put weapon back in LTS
    push_lts(e, w);
    return false;
}



bool give_weapon(Entity& e, int weapon_id) {
    if (try_place_weapon(e, weapon_id)) return true;
    return swap_out_and_place(e, weapon_id);
}