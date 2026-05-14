#include "../common/game_state.h"
#include "../common/constants.h"
#include "scheduler.h"
#include "signals.h"
#include "artifacts.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

static GameState* g_state = nullptr;

static double monotonic_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void die(const char* what) {
    perror(what);
    std::exit(1);
}


static void sigterm_handler(int sig) {
    (void)sig;
    if (g_state) {
        g_state->status = GS_QUIT;
    }
}


static GameState* create_shared_memory() {
    // Defensive: erase any stale segment from a previous (crashed) run.
    shm_unlink(SHM_NAME);

    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd < 0) die("arbiter: shm_open");
    if (ftruncate(fd, sizeof(GameState)) < 0) die("arbiter: ftruncate");

    void* mem = mmap(nullptr, sizeof(GameState),
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) die("arbiter: mmap");
    close(fd);

    GameState* s = static_cast<GameState*>(mem);
    std::memset(s, 0, sizeof(GameState));
    return s;
}


static void init_semaphores(GameState* s) {
    if (sem_init(&s->state_lock, 1, 1) < 0)
        die("sem_init state_lock");
    if (sem_init(&s->artifact_lock, 1, 1) < 0)
        die("sem_init artifact_lock");
    if (sem_init(&s->action_ready, 1, 0) < 0)
        die("sem_init action_ready");

    for (int i = 0; i < TOTAL_ENTITIES; i++) {
        if (sem_init(&s->turn_sem[i], 1, 0) < 0)
            die("sem_init turn_sem");
        if (sem_init(&s->turn_done_sem[i], 1, 0) < 0)
            die("sem_init turn_done_sem");
    }
}

static void destroy_semaphores(GameState* s) {
    sem_destroy(&s->state_lock);
    sem_destroy(&s->artifact_lock);
    sem_destroy(&s->action_ready);
    for (int i = 0; i < TOTAL_ENTITIES; i++) {
        sem_destroy(&s->turn_sem[i]);
        sem_destroy(&s->turn_done_sem[i]);
    }
}

static void init_game_state(GameState* s) {
    s->status = GS_INIT;
    s->current_turn_entity = -1;
    s->enemies_killed = 0;
    s->arbiter_pid = getpid();
    s->asp_pid = 0;
    s->num_players = 0;            // scheduler_init_entities sets the real value
    s->num_enemies = 0;
    s->pending_drop = {-1, -1, false, false};

   
    for (int i = 0; i < MAX_PLAYERS; ++i) s->hero_slot[i] = -1;

    s->artifacts[ARTIFACT_SOLAR]   = { ARTIFACT_SOLAR,   true,  -1 };
    s->artifacts[ARTIFACT_LUNAR]   = { ARTIFACT_LUNAR,   true,  -1 };
    s->artifacts[ARTIFACT_ECLIPSE] = { ARTIFACT_ECLIPSE, false, -1 };

    s->pending_action.type = ACT_NONE;
    s->pending_action.committed = false;

    s->log_head = 0;
    for (int i = 0; i < MAX_LOG_LINES; i++) {
        s->action_log[i][0] = '\0';
    }
}


static bool parse_int_arg(const char* arg, const char* prefix, int* out) {
    size_t pl = std::strlen(prefix);
    if (std::strncmp(arg, prefix, pl) != 0)
        return false;

    *out = std::atoi(arg + pl);
    return true;
}

static SchedulerConfig parse_cli(int argc, char** argv) {
    SchedulerConfig cfg = scheduler_default_config();
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--test") == 0)
            cfg.test_mode = true;
        else if (std::strcmp(argv[i], "--deadlock-demo") == 0)
            cfg.deadlock_demo = true;
        else if (std::strcmp(argv[i], "--mp") == 0)
            cfg.mp_mode = true;
        else if (parse_int_arg(argv[i], "--players=", &cfg.num_players)) {}
        else if (parse_int_arg(argv[i], "--enemies=", &cfg.num_enemies)) {}
        else if (parse_int_arg(argv[i], "--tick-ms=", &cfg.tick_ms)) {}
        else if (parse_int_arg(argv[i], "--kills=", &cfg.win_kill_target)) {}
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("Usage: %s [--test] [--players=N] [--enemies=N] "
                "[--tick-ms=N] [--kills=N] [--deadlock-demo] [--mp]\n", argv[0]);
            std::exit(0);
        }
        else {
            std::fprintf(stderr, "[arbiter] unknown arg: %s\n", argv[i]);
            std::exit(1);
        }
    }
    return cfg;
}


int main(int argc, char** argv) {
    SchedulerConfig cfg = parse_cli(argc, argv);

    std::printf("[arbiter] starting, pid=%d\n", getpid());
    std::printf("[arbiter] seeds: A=%d, B=%d, combined=%d\n",
        ROLL_PERSON_A, ROLL_PERSON_B, COMBINED_RNG_SEED);

    std::printf("[arbiter] config: players=%d enemies=%d tick=%dms kills=%d %s\n",
        cfg.num_players, cfg.num_enemies, cfg.tick_ms, cfg.win_kill_target,
        cfg.test_mode ? "[TEST MODE]" : "");

    g_state = create_shared_memory();
    init_semaphores(g_state);
    init_game_state(g_state);

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);   

    install_signal_handlers(g_state);
    start_deadlock_detector(g_state);

   
    if (cfg.test_mode) {
        // nothing to wait on
    }
    else if (cfg.mp_mode) {
        if (cfg.num_players <= 0) {
            std::fprintf(stderr,
                "[arbiter] --mp requires --players=N (total across all HIPs)\n");
            std::exit(1);
        }
        std::printf("[arbiter] MULTIPLAYER mode: waiting for %d slots to be "
            "claimed by HIPs...\n", cfg.num_players);
        g_state->num_players = cfg.num_players;
        const int MAX_WAIT_S = 120;
        int waited_ms = 0;
        while (waited_ms < MAX_WAIT_S * 1000) {
            int claimed = 0;
            for (int i = 0; i < cfg.num_players; i++)
                if (g_state->hero_slot[i] >= 0) claimed++;
            if (claimed >= cfg.num_players) break;
            usleep(100 * 1000);
            waited_ms += 100;
            if (g_state->status == GS_QUIT) break;
        }
        int final_claimed = 0;
        for (int i = 0; i < cfg.num_players; i++)
            if (g_state->hero_slot[i] >= 0) final_claimed++;
        std::printf("[arbiter] MP claim complete: %d/%d slots filled\n",
            final_claimed, cfg.num_players);
        if (final_claimed < cfg.num_players) {
            std::fprintf(stderr,
                "[arbiter] timed out waiting for all HIPs to claim slots\n");
            std::exit(1);
        }
    }
    else {
        std::printf("[arbiter] waiting for HIP to send party size...\n");
        g_state->num_players = 0;
        const int MAX_WAIT_S = 120;
        int waited_ms = 0;
        while (g_state->num_players == 0 && waited_ms < MAX_WAIT_S * 1000) {
            usleep(100 * 1000);
            waited_ms += 100;
            if (g_state->status == GS_QUIT) break;
        }
        if (g_state->num_players > 0) {
            cfg.num_players = g_state->num_players;
            std::printf("[arbiter] HIP selected party size = %d\n",
                cfg.num_players);
        }
        else {
            std::printf("[arbiter] timed out waiting for HIP, using CLI default\n");
            cfg.num_players = 1;
        }
    }

    scheduler_init_entities(g_state, cfg);

    g_state->status = GS_RUNNING;
    std::printf("[arbiter] shared memory ready, status=RUNNING (t=%.2f)\n",
        monotonic_now());

    if (!cfg.test_mode) {
        std::printf("[arbiter] waiting for ASP to attach (1s)...\n");
        sleep(1);
    }

    scheduler_run(g_state, cfg);

    std::printf("[arbiter] cleaning up (final status=%d)\n", (int)g_state->status);
    stop_deadlock_detector();
    destroy_semaphores(g_state);
    munmap(g_state, sizeof(GameState));
    shm_unlink(SHM_NAME);
    std::printf("[arbiter] goodbye\n");
    return 0;
}