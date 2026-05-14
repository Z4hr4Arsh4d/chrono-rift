#pragma once
#include "../common/game_state.h"


bool try_place_weapon(Entity& e, int weapon_id);
bool swap_out_and_place(Entity& e, int weapon_id);
bool swap_in(Entity& e, int lts_index);
bool give_weapon(Entity& e, int weapon_id);