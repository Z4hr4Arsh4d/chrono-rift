

#include "character_select.h"
#include "../common/game_state.h"
#include "../common/constants.h"
#include "renderer.h"
#include "player_thread.h"
#include "ui_event.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>

volatile sig_atomic_t g_stun_signaled = 0;

static void hip_sigusr1_handler(int /*sig*/) {
    g_stun_signaled = 1;
    // No printf — not async-signal-safe.
}

static void install_stun_handler() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = hip_sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, nullptr);
}

static GameState* attach_shared_memory() {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) {
        std::fprintf(stderr,
            "[hip] shm_open failed (errno=%d: %s). Is the arbiter running?\n",
            errno, std::strerror(errno));
        std::exit(1);
    }
    void* mem = mmap(nullptr, sizeof(GameState),
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { std::perror("[hip] mmap"); std::exit(1); }
    close(fd);
    return static_cast<GameState*>(mem);
}



static int parse_mp_slots(int argc, char** argv,
                         int* slots_out, int max_slots) {
    for (int i = 1; i < argc; i++) {
        const char* prefix = "--mp-slots=";
        size_t pl = std::strlen(prefix);
        if (std::strncmp(argv[i], prefix, pl) != 0) continue;
        const char* p = argv[i] + pl;
        int n = 0;
        while (*p && n < max_slots) {
            char* end;
            long v = std::strtol(p, &end, 10);
            if (end == p) break;
            if (v < 0 || v >= max_slots) {
                std::fprintf(stderr, "[hip] invalid slot index: %ld\n", v);
                std::exit(1);
            }
            slots_out[n++] = (int)v;
            p = end;
            while (*p == ',' || *p == ' ') p++;
        }
        return n;
    }
    return 0;   // not in MP mode
}

int main(int argc, char** argv) {
    std::printf("[hip] starting, pid=%d\n", getpid());

    int my_slots[MAX_PLAYERS];
    int my_slot_count = parse_mp_slots(argc, argv, my_slots, MAX_PLAYERS);
    bool mp_mode = (my_slot_count > 0);
    if (mp_mode) {
        std::printf("[hip] MULTIPLAYER mode — owning %d slot(s):", my_slot_count);
        for (int i = 0; i < my_slot_count; i++) std::printf(" %d", my_slots[i]);
        std::printf("\n");
    }

    install_stun_handler();   // install BEFORE attaching shm
    std::printf("[hip] SIGUSR1 stun handler installed\n");

    GameState* state = attach_shared_memory();
    std::printf("[hip] attached. arbiter_pid=%d  status=%d\n",
        state->arbiter_pid, (int)state->status);

    int num_players = 0;     // number of player threads THIS hip will spawn
    int my_thread_slots[MAX_PLAYERS];   // slots this hip owns (compacted)

    if (mp_mode) {

        sem_wait(&state->state_lock);
        for (int i = 0; i < my_slot_count; i++) {
            int s = my_slots[i];
            state->hero_slot[s] = s;     // claim it (any non -1 value works)
            my_thread_slots[i]  = s;
        }
        sem_post(&state->state_lock);
        num_players = my_slot_count;

        std::printf("[hip] MP slots claimed, waiting for arbiter to start...\n");

        // Wait for GS_RUNNING. Other HIPs may still be claiming theirs.
        for (int i = 0; i < 200 && state->status == GS_INIT; i++)
            usleep(100000);
    }
    else {

        sf::RenderWindow cs_window(
            sf::VideoMode(1280, 720),
            "Chrono Rift — Select Your Party",
            sf::Style::Titlebar | sf::Style::Close);

        sf::Font font;
        font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");

        bool selected[4] = {false, false, false, false};
        int party_size = run_character_select(cs_window, font, selected);
        cs_window.close();

        if (party_size == 0) {
            kill(state->arbiter_pid, SIGTERM);
            munmap(state, sizeof(GameState));
            return 0;
        }

        sem_wait(&state->state_lock);
        int slot = 0;
        for (int i = 0; i < 4 && slot < MAX_PLAYERS; i++) {
            if (selected[i]) state->hero_slot[slot++] = i;
        }
        for (; slot < MAX_PLAYERS; slot++) state->hero_slot[slot] = -1;
        state->num_players = party_size;
        sem_post(&state->state_lock);

        std::printf("[hip] party_size=%d sent to arbiter, waiting for GS_RUNNING...\n",
                    party_size);

        for (int i = 0; i < 100 && state->status == GS_INIT; i++)
            usleep(100000);

        num_players = party_size;
        for (int i = 0; i < num_players; i++) my_thread_slots[i] = i;
    }

    if (state->status != GS_RUNNING) {
        std::fprintf(stderr, "[hip] arbiter did not reach GS_RUNNING. Exiting.\n");
        munmap(state, sizeof(GameState));
        return 1;
    }

    ui_bus_init();
    char wtitle[128];
    if (mp_mode) {
        // Build "Chrono Rift — Slots 0,1" so each MP window is distinct
        char slot_list[64] = "";
        for (int i = 0; i < my_slot_count; i++) {
            char b[8]; std::snprintf(b, sizeof(b), "%s%d",
                i > 0 ? "," : "", my_thread_slots[i]);
            std::strncat(slot_list, b, sizeof(slot_list) - strlen(slot_list) - 1);
        }
        std::snprintf(wtitle, sizeof(wtitle),
            "Chrono Rift — MP Slots %s (pid %d)", slot_list, getpid());
        renderer_start(state, wtitle);
    } else {
        renderer_start(state);
    }
    usleep(200000);  // 200ms for window to open

    pthread_t        tids[MAX_PLAYERS];
    PlayerThreadArgs args[MAX_PLAYERS];

    for (int i = 0; i < num_players; i++) {
        int eid = my_thread_slots[i];
        args[i].state     = state;
        args[i].entity_id = eid;
        if (pthread_create(&tids[i], nullptr, player_thread_main, &args[i]) != 0) {
            std::perror("[hip] pthread_create");
            renderer_stop();
            ui_bus_destroy();
            munmap(state, sizeof(GameState));
            return 1;
        }
        std::printf("[hip] player thread %d created (entity_id=%d)\n", i, eid);
    }

    for (int i = 0; i < num_players; i++) {
        pthread_join(tids[i], nullptr);
        std::printf("[hip] player thread %d joined\n", i);
    }

    renderer_stop();
    ui_bus_destroy();

    std::printf("[hip] game ended. status=%d  kills=%d\n",
        (int)state->status, state->enemies_killed);

    munmap(state, sizeof(GameState));
    std::printf("[hip] goodbye\n");
    return 0;
}