#pragma once

#include "../common/game_state.h"

bool try_acquire_artifact(GameState* state, int artifact_id, int entity_id);
void release_artifact(GameState* state, int artifact_id, int entity_id);
void introduce_eclipse_relic(GameState* state, int finder_id);
int weapon_to_artifact(int weapon_id);
void acquire_for_weapon(GameState* state, int weapon_id, int entity_id);
void release_for_weapon(GameState* state, int weapon_id, int entity_id);
void reconcile_artifacts_locked(GameState* state);
void start_deadlock_detector(GameState* state);
void stop_deadlock_detector();
