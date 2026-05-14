
#include "../common/game_state.h"
#include "../common/constants.h"
#include "npc_thread.h"

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

static void asp_sigusr1_handler(int /*sig*/) {
    g_stun_signaled = 1;
}

static void install_stun_handler() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = asp_sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, nullptr);
}

static GameState* attach_shared_memory() {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) {
        std::fprintf(stderr,
            "[asp] shm_open failed (errno=%d: %s). Is arbiter running?\n",
            errno, std::strerror(errno));
        std::exit(1);
    }
    void* mem = mmap(nullptr, sizeof(GameState),
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { std::perror("[asp] mmap"); std::exit(1); }
    close(fd);
    return static_cast<GameState*>(mem);
}

int main() {
    std::printf("[asp] starting, pid=%d\n", getpid());

    install_stun_handler();   // install BEFORE attaching shm
    std::printf("[asp] SIGUSR1 stun handler installed\n");

    GameState* state = attach_shared_memory();

    sem_wait(&state->state_lock);
    state->asp_pid = getpid();
    sem_post(&state->state_lock);

    std::printf("[asp] attached. asp_pid=%d registered, waiting for GS_RUNNING...\n",
                getpid());

   
    for (int i = 0; i < 1200 && state->status == GS_INIT; i++)
        usleep(100000);

    if (state->status != GS_RUNNING) {
        std::fprintf(stderr, "[asp] arbiter not RUNNING. Exiting.\n");
        munmap(state, sizeof(GameState));
        return 1;
    }

    std::printf("[asp] GS_RUNNING reached. num_enemies=%d\n", state->num_enemies);

    int num_enemies = state->num_enemies;
    if (num_enemies < 2 || num_enemies > MAX_ENEMIES) {
        std::fprintf(stderr, "[asp] unexpected num_enemies=%d\n", num_enemies);
        munmap(state, sizeof(GameState));
        return 1;
    }

    pthread_t*     tids = new pthread_t[num_enemies];
    NpcThreadArgs* args = new NpcThreadArgs[num_enemies];

    for (int i = 0; i < num_enemies; i++) {
        args[i].state     = state;
        args[i].entity_id = MAX_PLAYERS + i;
        if (pthread_create(&tids[i], nullptr, npc_thread_main, &args[i]) != 0) {
            std::perror("[asp] pthread_create");
            // Unblock already-waiting threads before bailing
            for (int j = 0; j < i; j++)
                sem_post(&state->turn_sem[MAX_PLAYERS + j]);
            delete[] tids; delete[] args;
            munmap(state, sizeof(GameState));
            return 1;
        }
        std::printf("[asp] NPC thread %d created (entity_id=%d)\n",
                    i, MAX_PLAYERS + i);
    }

    for (int i = 0; i < num_enemies; i++) {
        pthread_join(tids[i], nullptr);
        std::printf("[asp] NPC thread %d joined\n", i);
    }

    std::printf("[asp] game ended. status=%d  kills=%d\n",
                (int)state->status, state->enemies_killed);

    delete[] tids;
    delete[] args;
    munmap(state, sizeof(GameState));
    std::printf("[asp] goodbye\n");
    return 0;
}