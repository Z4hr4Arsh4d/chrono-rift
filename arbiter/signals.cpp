
#include "signals.h"
#include "../common/constants.h"

#include <cstdio>
#include <csignal>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>

static GameState* g_state = NULL;




enum AlarmKind { AK_NONE = 0, AK_ULTIMATE = 1, AK_NPC = 2 };
static volatile sig_atomic_t g_alarm_kind        = AK_NONE;
volatile sig_atomic_t        g_npc_timeout_fired = 0;   // visible to scheduler

double now_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// --- SIGUSR1 -----------------------------------------------------------

static void on_sigusr1(int /*sig*/) {
    if (!g_state) return;
    int tid = g_state->pending_action.target_id;
    if (tid < 0 || tid >= TOTAL_ENTITIES) return;

    g_state->entities[tid].stunned         = true;
    g_state->entities[tid].stun_start_time = now_seconds();
    // No printf in a signal handler -- not async-signal-safe.
}

// --- SIGALRM (multiplexed: Ultimate end OR NPC turn timeout) ----------

static void on_sigalrm(int /*sig*/) {
    int kind = g_alarm_kind;
    g_alarm_kind = AK_NONE;
    if (kind == AK_ULTIMATE) {
        if (g_state && g_state->asp_pid > 0) {
            kill(g_state->asp_pid, SIGCONT);
        }
    } else if (kind == AK_NPC) {
        g_npc_timeout_fired = 1;
    }
}

// --- Public API --------------------------------------------------------

void install_signal_handlers(GameState* state) {
    g_state = state;

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigalrm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;             // do NOT restart -- we want sleeps to break
    sigaction(SIGALRM, &sa, NULL);
}

void trigger_ultimate_pause(GameState* state) {
    if (state->asp_pid <= 0) {
        fprintf(stderr, "[ultimate] asp_pid=0, skipping suspend\n");
        return;
    }
    kill(state->asp_pid, SIGSTOP);
    g_alarm_kind = AK_ULTIMATE;
    alarm(ULTIMATE_PAUSE_SEC);   // delivers SIGALRM after 10s -> SIGCONT
    fprintf(stderr, "[ultimate] ASP suspended for %d seconds\n",
            ULTIMATE_PAUSE_SEC);
}

void arm_npc_turn_timeout(int seconds) {
    g_npc_timeout_fired = 0;
    g_alarm_kind = AK_NPC;
    alarm(seconds);
}

void cancel_npc_turn_timeout() {
    alarm(0);
    g_alarm_kind = AK_NONE;
    g_npc_timeout_fired = 0;
}

void clear_expired_stuns_locked(GameState* state) {
    double t = now_seconds();
    for (int i = 0; i < TOTAL_ENTITIES; i++) {
        if (!state->entities[i].stunned) continue;
        if (t - state->entities[i].stun_start_time >= STUN_DURATION_SEC) {
            state->entities[i].stunned = false;
        }
    }
}

