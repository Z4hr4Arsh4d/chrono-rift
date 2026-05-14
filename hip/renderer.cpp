#include "renderer.h"
#include "ui_event.h"
#include "../common/game_state.h"
#include "../common/constants.h"

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <pthread.h>
#include <semaphore.h>
#include <csignal>
#include <unistd.h>

#include "../common/inventory.h"
#include "../common/weapons.h"

// UIBus singleton

UIBus g_ui_bus;

void ui_bus_init() {
    sem_init(&g_ui_bus.event_ready, 0, 0);
    g_ui_bus.active_player = -1;
    g_ui_bus.initialised = true;
}

void ui_bus_destroy() {
    if (g_ui_bus.initialised) {
        sem_destroy(&g_ui_bus.event_ready);
        g_ui_bus.initialised = false;
    }
}

// Renderer thread state

static pthread_t         g_render_tid;
static volatile int g_render_run = 0;
static GameState*        g_state_ptr = nullptr;
static char              g_window_title[128] = "Chrono Rift — CS2006 OS Spring 2026";

// Palette

static const sf::Color C_BG        {10,  12,  16,  255};
static const sf::Color C_PANEL     {18,  22,  30,  255};
static const sf::Color C_BORDER    {45,  50,  65,  255};
static const sf::Color C_AMBER     {255, 176, 0,   255};
static const sf::Color C_AMBER_DIM {140, 90,  0,   255};
static const sf::Color C_TEXT      {210, 215, 225, 255};
static const sf::Color C_DIM       {80,  85,  95,  255};
static const sf::Color C_GREEN     {60,  200, 100, 255};
static const sf::Color C_YELLOW    {220, 190, 40,  255};
static const sf::Color C_RED       {220, 60,  60,  255};
static const sf::Color C_CYAN      {60,  200, 220, 255};
static const sf::Color C_MAGENTA   {200, 60,  200, 255};
static const sf::Color C_DEAD      {40,  40,  50,  255};
static const sf::Color C_BTN_NORM  {30,  36,  50,  255};
static const sf::Color C_BTN_HOV   {50,  60,  85,  255};
static const sf::Color C_BTN_DIS   {20,  23,  30,  255};

// Layout constants

static const int WIN_W = 1280;
static const int WIN_H = 720;

static const int TITLE_H   = 36;
static const int LOG_H     = 120;
static const int LOG_Y     = WIN_H - LOG_H;

static const int LEFT_X    = 8;
static const int LEFT_W    = 580;
static const int RIGHT_X   = 600;
static const int RIGHT_W   = 672;

// Player card dimensions
static const int PC_H      = 100;   // card height
static const int PC_GAP    = 8;

// Enemy card dimensions
static const int EC_H      = 80;
static const int EC_GAP    = 6;

// Action button strip (shown below the active player card)
static const int BTN_W     = 120;
static const int BTN_H     = 34;
static const int BTN_GAP   = 6;


static const int HERO_FRAMES  = 3;
static const int ENEMY_FRAMES = 3;

static sf::Texture g_hero_tex[4][HERO_FRAMES];
static bool        g_hero_tex_ok[4][HERO_FRAMES] = {{false}};

static sf::Texture g_enemy_tex[MAX_ENEMIES][ENEMY_FRAMES];
static bool        g_enemy_tex_ok[MAX_ENEMIES][ENEMY_FRAMES] = {{false}};

static sf::Texture g_bg_tex;
static bool        g_bg_tex_ok = false;

static const char* g_hero_base[4] = {
    "hip/assets/alya",
    "hip/assets/chrono",
    "hip/assets/frog",
    "hip/assets/magnus",
};
static const char* g_enemy_base[MAX_ENEMIES] = {
    "hip/assets/enemy_imp",
    "hip/assets/enemy_cybot",
    "hip/assets/enemy_blob",
    "hip/assets/enemy_ghost",
    "hip/assets/enemy_brain",
    "hip/assets/enemy_jinn",
    "hip/assets/enemy_lancer",
    "hip/assets/enemy_sun",
    "hip/assets/enemy_mage",
};
static bool g_sprites_loaded = false;

static void load_sprites_once() {
    if (g_sprites_loaded) return;
    char path[128];
    for (int i = 0; i < 4; i++) {
        for (int f = 0; f < HERO_FRAMES; f++) {
            std::snprintf(path, sizeof(path), "%s_%d.png", g_hero_base[i], f);
            if (g_hero_tex[i][f].loadFromFile(path)) {
                g_hero_tex[i][f].setSmooth(false);
                g_hero_tex_ok[i][f] = true;
            } else {
                std::fprintf(stderr, "[renderer] WARN: failed to load %s\n", path);
            }
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        for (int f = 0; f < ENEMY_FRAMES; f++) {
            std::snprintf(path, sizeof(path), "%s_%d.png", g_enemy_base[i], f);
            if (g_enemy_tex[i][f].loadFromFile(path)) {
                g_enemy_tex[i][f].setSmooth(false);
                g_enemy_tex_ok[i][f] = true;
            } else {
                std::fprintf(stderr, "[renderer] WARN: failed to load %s\n", path);
            }
        }
    }
    if (g_bg_tex.loadFromFile("hip/assets/background.png")) {
        g_bg_tex.setSmooth(true);
        g_bg_tex_ok = true;
    } else {
        std::fprintf(stderr, "[renderer] WARN: failed to load hip/assets/background.png\n");
    }
    g_sprites_loaded = true;
}

// Return the right frame for an entity at the current animation tick.
// Stagger by entity id so they don't all bob in lockstep.
static const sf::Texture* hero_frame_for(int hero_id, int anim_tick, int stagger) {
    if (hero_id < 0 || hero_id >= 4) hero_id = 0;
    int f = ((anim_tick + stagger) / 8) % HERO_FRAMES;   // ~250ms per frame at 30fps
    if (g_hero_tex_ok[hero_id][f]) return &g_hero_tex[hero_id][f];
    if (g_hero_tex_ok[hero_id][0]) return &g_hero_tex[hero_id][0];
    return nullptr;
}
static const sf::Texture* enemy_frame_for(int local, int anim_tick, int stagger) {
    if (local < 0 || local >= MAX_ENEMIES) return nullptr;
    int f = ((anim_tick + stagger) / 8) % ENEMY_FRAMES;
    if (g_enemy_tex_ok[local][f]) return &g_enemy_tex[local][f];
    if (g_enemy_tex_ok[local][0]) return &g_enemy_tex[local][0];
    return nullptr;
}

static void draw_sprite_fit(sf::RenderWindow& w, const sf::Texture& tex,
                            float cx, float bottom_y,
                            float max_w, float max_h,
                            sf::Color tint = sf::Color::White) {
    sf::Vector2u sz = tex.getSize();
    if (sz.x == 0 || sz.y == 0) return;
    float scale = std::min(max_w / (float)sz.x, max_h / (float)sz.y);
    if (scale <= 0.f) return;
    sf::Sprite spr(tex);
    spr.setScale(scale, scale);
    float draw_w = sz.x * scale;
    float draw_h = sz.y * scale;
    spr.setPosition(cx - draw_w / 2.f, bottom_y - draw_h);
    spr.setColor(tint);
    w.draw(spr);
}

// Helpers


// Draw a filled rectangle with an optional 1-px border
static void draw_rect(sf::RenderWindow& w,
                      float x, float y, float bw, float bh,
                      sf::Color fill, sf::Color border = sf::Color::Transparent) {
    sf::RectangleShape r({bw, bh});
    r.setPosition(x, y);
    r.setFillColor(fill);
    if (border != sf::Color::Transparent) {
        r.setOutlineColor(border);
        r.setOutlineThickness(1.f);
    }
    w.draw(r);
}

// Draw a horizontal progress bar
static void draw_bar(sf::RenderWindow& w,
                     float x, float y, float bw, float bh,
                     float fraction, sf::Color fg, sf::Color bg) {
    draw_rect(w, x, y, bw, bh, bg);
    float filled = bw * std::max(0.f, std::min(1.f, fraction));
    if (filled > 0.f) draw_rect(w, x, y, filled, bh, fg);
    // Border
    sf::RectangleShape border({bw, bh});
    border.setPosition(x, y);
    border.setFillColor(sf::Color::Transparent);
    border.setOutlineColor(C_BORDER);
    border.setOutlineThickness(1.f);
    w.draw(border);
}

// HP bar colour based on fraction
static sf::Color hp_color(float pct) {
    if (pct > 0.5f) return C_GREEN;
    if (pct > 0.25f) return C_YELLOW;
    return C_RED;
}

// Button

struct Button {
    sf::FloatRect  rect;
    const char*    label;
    ActionType     action_type;
    bool           enabled;
};

static bool btn_hovered(const Button& b, sf::Vector2i mouse) {
    return b.rect.contains((float)mouse.x, (float)mouse.y);
}

static void draw_button(sf::RenderWindow& w, const sf::Font& font,
                        const Button& b, sf::Vector2i mouse) {
    sf::Color fill = b.enabled
        ? (btn_hovered(b, mouse) ? C_BTN_HOV : C_BTN_NORM)
        : C_BTN_DIS;
    sf::Color border = b.enabled ? C_AMBER : C_BORDER;
    sf::Color text_c = b.enabled ? C_TEXT : C_DIM;

    draw_rect(w, b.rect.left, b.rect.top, b.rect.width, b.rect.height,
              fill, border);

    sf::Text t;
    t.setFont(font);
    t.setString(b.label);
    t.setCharacterSize(13);
    t.setFillColor(text_c);
    sf::FloatRect tb = t.getLocalBounds();
    t.setPosition(
        b.rect.left + (b.rect.width  - tb.width)  / 2.f - tb.left,
        b.rect.top  + (b.rect.height - tb.height) / 2.f - tb.top);
    w.draw(t);
}


[[maybe_unused]]
static void draw_player_card(sf::RenderWindow& w, const sf::Font& font,
                             const Entity& e, int card_y,
                             bool is_current, bool show_buttons,
                             sf::Vector2i mouse,
                             std::vector<Button>& buttons_out) {
    sf::Color border_c = is_current ? C_AMBER : C_BORDER;
    draw_rect(w, LEFT_X, card_y, LEFT_W, PC_H, C_PANEL, border_c);

    if (is_current) {
        // Amber left-edge accent
        draw_rect(w, LEFT_X, card_y, 3, PC_H, C_AMBER);
    }

    if (!e.alive) {
        sf::Text t;
        t.setFont(font);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "P%d — DEFEATED", e.id + 1);
        t.setString(buf);
        t.setCharacterSize(16);
        t.setFillColor(C_DEAD);
        t.setPosition(LEFT_X + 16, card_y + 38);
        w.draw(t);
        return;
    }

    float px = LEFT_X + 14;
    float py = card_y + 10;

    // Name
    {
        sf::Text t;
        t.setFont(font);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "PLAYER %d", e.id + 1);
        t.setString(buf);
        t.setCharacterSize(15);
        t.setStyle(sf::Text::Bold);
        t.setFillColor(is_current ? C_AMBER : C_TEXT);
        t.setPosition(px, py);
        w.draw(t);
    }

    // Stunned badge
    if (e.stunned) {
        sf::Text t;
        t.setFont(font);
        t.setString("[STUNNED]");
        t.setCharacterSize(13);
        t.setFillColor(C_RED);
        t.setPosition(LEFT_X + LEFT_W - 90, py);
        w.draw(t);
    }

    // HP bar
    float bar_x = px;
    float bar_y = py + 24;
    float bar_w = LEFT_W - 28;
    float hp_frac = (e.max_hp > 0) ? (float)e.hp / e.max_hp : 0.f;
    draw_bar(w, bar_x, bar_y, bar_w, 14, hp_frac, hp_color(hp_frac),
             sf::Color{20, 25, 35, 255});

    {
        sf::Text t;
        t.setFont(font);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "HP %d/%d", e.hp, e.max_hp);
        t.setString(buf);
        t.setCharacterSize(11);
        t.setFillColor(C_DIM);
        t.setPosition(bar_x, bar_y + 16);
        w.draw(t);
    }

    // Stamina bar
    float st_y = bar_y + 32;
    float st_frac = (e.max_stamina > 0) ? (float)e.stamina / e.max_stamina : 0.f;
    draw_bar(w, bar_x, st_y, bar_w, 10, st_frac, C_CYAN,
             sf::Color{15, 20, 30, 255});

    {
        sf::Text t;
        t.setFont(font);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "SP %d/%d  SPD %d",
                      e.stamina, e.max_stamina, e.speed);
        t.setString(buf);
        t.setCharacterSize(11);
        t.setFillColor(C_DIM);
        t.setPosition(bar_x, st_y + 12);
        w.draw(t);
    }

    if (!show_buttons) return;

    float bx = LEFT_X + 14;
    float by = card_y + PC_H + 6;

    struct BtnDef { const char* label; ActionType type; bool enabled; };
    BtnDef defs[] = {
        {"1 Strike",  ACT_STRIKE,  true},
        {"2 Exhaust", ACT_EXHAUST, true},  // 
        {"3 Weapon",  ACT_USE_WEAPON, false},
        {"4 Swap In", ACT_SWAP_IN,    false},
        {"5 Heal",    ACT_HEAL,       true},
        {"6 Skip",    ACT_SKIP,       true},
        {"U Ultimate",ACT_ULTIMATE,   false},
    };
    int n = sizeof(defs) / sizeof(defs[0]);

    for (int i = 0; i < n; i++) {
        Button b;
        b.rect    = sf::FloatRect(bx + i * (BTN_W + BTN_GAP), by, BTN_W, BTN_H);
        b.label   = defs[i].label;
        b.action_type = defs[i].type;
        b.enabled = defs[i].enabled;
        draw_button(w, font, b, mouse);
        buttons_out.push_back(b);
    }
}



[[maybe_unused]]
static int pick_enemy_overlay(sf::RenderWindow& w, const sf::Font& font,
                              const GameState& snap) {
    // Collect alive enemies
    int alive[MAX_ENEMIES];
    int count = 0;
    for (int i = 0; i < MAX_ENEMIES && count < MAX_ENEMIES; i++) {
        int gid = MAX_PLAYERS + i;
        if (snap.entities[gid].alive) alive[count++] = gid;
    }
    if (count == 0) return -1;

    // Overlay dimensions
    float ox = 300, oy = 180, ow = 400;
    float oh = 60.f + count * 44.f + 16.f;

    while (w.isOpen()) {
        sf::Event ev;
        while (w.pollEvent(ev)) {
            if (ev.type == sf::Event::Closed) return -1;
            if (ev.type == sf::Event::KeyPressed &&
                ev.key.code == sf::Keyboard::Escape) return -1;
            if (ev.type == sf::Event::MouseButtonReleased &&
                ev.mouseButton.button == sf::Mouse::Left) {
                int mx = ev.mouseButton.x, my = ev.mouseButton.y;
                for (int i = 0; i < count; i++) {
                    float by2 = oy + 54 + i * 44;
                    sf::FloatRect br(ox + 16, by2, ow - 32, 36);
                    if (br.contains((float)mx, (float)my)) return alive[i];
                }
            }
        }

        // Re-render background (simple dim)
        w.clear(C_BG);
        // Dim overlay
        sf::RectangleShape dim({(float)WIN_W, (float)WIN_H});
        dim.setFillColor({0, 0, 0, 160});
        w.draw(dim);

        draw_rect(w, ox, oy, ow, oh, sf::Color{22, 28, 40, 255}, C_AMBER);

        {
            sf::Text t;
            t.setFont(font);
            t.setString("SELECT TARGET");
            t.setCharacterSize(16);
            t.setStyle(sf::Text::Bold);
            t.setFillColor(C_AMBER);
            t.setPosition(ox + 16, oy + 12);
            w.draw(t);
        }

        sf::Vector2i mouse = sf::Mouse::getPosition(w);
        for (int i = 0; i < count; i++) {
            int gid = alive[i];
            const Entity& en = snap.entities[gid];
            float by2 = oy + 54 + i * 44;
            bool hov = sf::FloatRect(ox + 16, by2, ow - 32, 36)
                           .contains((float)mouse.x, (float)mouse.y);
            draw_rect(w, ox + 16, by2, ow - 32, 36,
                      hov ? C_BTN_HOV : C_BTN_NORM, C_BORDER);

            sf::Text t;
            t.setFont(font);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "E%d   HP %d/%d   SPD %d",
                          en.id - MAX_PLAYERS + 1, en.hp, en.max_hp, en.speed);
            t.setString(buf);
            t.setCharacterSize(13);
            t.setFillColor(C_TEXT);
            t.setPosition(ox + 26, by2 + 10);
            w.draw(t);
        }

        // ESC hint
        {
            sf::Text t;
            t.setFont(font);
            t.setString("[ESC] cancel");
            t.setCharacterSize(11);
            t.setFillColor(C_DIM);
            t.setPosition(ox + 16, oy + oh - 20);
            w.draw(t);
        }

        w.display();
    }
    return -1;
}




static void* render_loop(void*) {
    char status_msg[128] = "";
    sf::RenderWindow window(
        sf::VideoMode(WIN_W, WIN_H),
        g_window_title,
        sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(30);

    sf::Font font;
    bool font_loaded = font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    if (!font_loaded) font_loaded = font.loadFromFile("/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf");
    if (!font_loaded) font_loaded = font.loadFromFile("/app/assets/font.ttf");

    load_sprites_once();

    GameState* s = g_state_ptr;

    bool       waiting_for_target  = false;
    bool       waiting_for_weapon  = false;
    bool       waiting_for_lts     = false;
    ActionType pending_action_type = ACT_NONE;
    int        selected_weapon_idx = -1;

    int anim_tick = 0;

    while (window.isOpen() && g_render_run) {

        GameState snap;
        sem_wait(&s->state_lock);
        snap = *s;
        sem_post(&s->state_lock);

        int active = g_ui_bus.active_player;

        sf::Event ev;
        while (window.pollEvent(ev)) 
        {

            if (ev.type == sf::Event::Closed) {
                kill(snap.arbiter_pid, SIGTERM);
                g_render_run = 0;
                window.close();
                return nullptr;
            }

            if (ev.type == sf::Event::KeyPressed) {
                if (snap.pending_drop.pending) {
                    if (ev.key.code == sf::Keyboard::Y) {
                        sem_wait(&s->state_lock);
                        give_weapon(s->entities[snap.pending_drop.for_player],
                                    snap.pending_drop.weapon_id);
                        s->pending_drop.pending  = false;
                        s->pending_drop.declined = false;
                        sem_post(&s->state_lock);
                    } else if (ev.key.code == sf::Keyboard::N) {
                        sem_wait(&s->state_lock);
                        s->pending_drop.pending  = false;
                        s->pending_drop.declined = true;
                        sem_post(&s->state_lock);
                    }
                    continue;
                }

                if (active < 0) continue;

                
            UIEvent ue;
            ue.type       = ACT_NONE;
            ue.target_id  = -1;
            ue.weapon_idx = -1;
            ue.lts_idx    = -1;

            // -- LTS selection ----------------------------------------
            if (waiting_for_lts) {
                if (ev.key.code >= sf::Keyboard::Num1 &&
                    ev.key.code <= sf::Keyboard::Num9) {
                    int pick = ev.key.code - sf::Keyboard::Num1;
                    if (pick < snap.entities[active].lts_count) {
                        ue.type         = ACT_SWAP_IN;
                        ue.lts_idx      = pick;
                        waiting_for_lts = false;
                    }
                } else if (ev.key.code == sf::Keyboard::Escape) {
                    waiting_for_lts = false;
                }
            }
            // -- Weapon selection -------------------------------------
            else if (waiting_for_weapon) {
                if (ev.key.code >= sf::Keyboard::Num1 &&
                    ev.key.code <= sf::Keyboard::Num9) {
                    int pick  = ev.key.code - sf::Keyboard::Num1;
                    int found = 0;
                    for (int sl = 0; sl < INVENTORY_SLOTS; sl++) {
                        if (snap.entities[active].inventory[sl].id >= 0 &&
                            snap.entities[active].inventory[sl].start_slot == sl) {
                            if (found == pick) {
                                selected_weapon_idx = sl;
                                waiting_for_weapon  = false;
                                waiting_for_target  = true;
                                break;
                            }
                            found++;
                        }
                    }
                } else if (ev.key.code == sf::Keyboard::Escape) {
                    waiting_for_weapon  = false;
                    pending_action_type = ACT_NONE;
                }
            }
            // -- Target selection -------------------------------------
            else if (waiting_for_target) {
                if (ev.key.code >= sf::Keyboard::Num1 &&
                    ev.key.code <= sf::Keyboard::Num9) {
                    int enemy_idx = ev.key.code - sf::Keyboard::Num1;
                    int gid = MAX_PLAYERS + enemy_idx;
                    if (enemy_idx < snap.num_enemies && snap.entities[gid].alive) {
                        ue.type             = pending_action_type;
                        ue.target_id        = gid;
                        ue.weapon_idx       = selected_weapon_idx;
                        waiting_for_target  = false;
                        pending_action_type = ACT_NONE;
                        selected_weapon_idx = -1;
                    }
                } else if (ev.key.code == sf::Keyboard::Escape) {
                    waiting_for_target  = false;
                    pending_action_type = ACT_NONE;
                    selected_weapon_idx = -1;
                }
            }
            // -- Normal mode ------------------------------------------
            else {
                if (ev.key.code == sf::Keyboard::Num1) {
                    waiting_for_target  = true;
                    pending_action_type = ACT_STRIKE;
                }
                else if (ev.key.code == sf::Keyboard::Num2) {
                    waiting_for_target  = true;
                    pending_action_type = ACT_EXHAUST;
                }
                else if (ev.key.code == sf::Keyboard::Num3) {
                    bool has_weapon = false;
                    for (int sl = 0; sl < INVENTORY_SLOTS; sl++) {
                        if (snap.entities[active].inventory[sl].id >= 0 &&
                            snap.entities[active].inventory[sl].start_slot == sl) {
                            has_weapon = true; break;
                        }
                    }
                    if (has_weapon) {
                        waiting_for_weapon  = true;
                        pending_action_type = ACT_USE_WEAPON;
                    } else {
                        std::snprintf(status_msg, sizeof(status_msg),
                            "No weapons in inventory!");
                    }
                }
                else if (ev.key.code == sf::Keyboard::Num4) {
                    if (snap.entities[active].lts_count > 0) {
                        waiting_for_lts = true;
                    } else {
                        std::snprintf(status_msg, sizeof(status_msg),
                            "Long-term storage is empty!");
                    }
                }
                else if (ev.key.code == sf::Keyboard::Num5) {
                    ue.type      = ACT_HEAL;
                    ue.target_id = active;
                }
                else if (ev.key.code == sf::Keyboard::Num6) {
                    ue.type = ACT_SKIP;
                }
                else if (ev.key.code == sf::Keyboard::U) {
                    bool has_solar = false, has_lunar = false;
                    for (int sl = 0; sl < INVENTORY_SLOTS; sl++) {
                        if (snap.entities[active].inventory[sl].id == WEAPON_SOLAR_CORE)  has_solar = true;
                        if (snap.entities[active].inventory[sl].id == WEAPON_LUNAR_BLADE) has_lunar = true;
                    }
                    if (has_solar && has_lunar) {
                        waiting_for_target  = true;
                        pending_action_type = ACT_ULTIMATE;
                    } else {
                        std::snprintf(status_msg, sizeof(status_msg),
                            "Ultimate requires Solar Core + Lunar Blade in inventory!");
                    }
                }
                else if (ev.key.code == sf::Keyboard::Q) {
                    kill(snap.arbiter_pid, SIGTERM);
                    g_render_run = 0;
                    window.close();
                    return nullptr;
                }
            }

            if (ue.type != ACT_NONE) {
                g_ui_bus.event = ue;
                sem_post(&g_ui_bus.event_ready);
                status_msg[0] = '\0';
            }
        }
    }    // end pollEvent

        anim_tick++;
        window.clear(C_BG);

        if (g_bg_tex_ok) {
            sf::Sprite bg(g_bg_tex);
            sf::Vector2u sz = g_bg_tex.getSize();
            if (sz.x > 0 && sz.y > 0) {
                bg.setScale((float)WIN_W / (float)sz.x,
                            (float)WIN_H / (float)sz.y);
                bg.setPosition(0, 0);
                window.draw(bg);
            }
        }

        {
            draw_rect(window, 0, 0, WIN_W, TITLE_H,
                      sf::Color{14,18,26,220}, C_BORDER);
            sf::Text t; t.setFont(font);
            t.setString("CHRONO RIFT");
            t.setCharacterSize(18); t.setStyle(sf::Text::Bold); t.setFillColor(C_AMBER);
            t.setPosition(14, 8); window.draw(t);

            char sbuf[120];
            const char* stat = "RUNNING";
            if (snap.status == GS_WIN)  stat = "VICTORY";
            if (snap.status == GS_LOSE) stat = "DEFEAT";
            if (snap.status == GS_QUIT) stat = "QUIT";
            std::snprintf(sbuf, sizeof(sbuf),
                "STATUS: %-8s   KILLS: %d / %d   TURN: %s",
                stat, snap.enemies_killed, KILLS_TO_WIN,
                snap.current_turn_entity < 0 ? "-" :
                    (is_player_id(snap.current_turn_entity) ? "PLAYER" : "ENEMY"));
            sf::Text ts; ts.setFont(font); ts.setString(sbuf);
            ts.setCharacterSize(13); ts.setFillColor(C_TEXT);
            ts.setPosition(200, 11); window.draw(ts);
        }

        const int INV_TOP    = TITLE_H + 4;
        const int INV_HEIGHT = 70;
        {
            int inv_who = -1;
            if (active >= 0 && active < MAX_PLAYERS &&
                snap.entities[active].alive) {
                inv_who = active;
            } else {
                for (int i = 0; i < snap.num_players; i++) {
                    if (snap.entities[i].alive) { inv_who = i; break; }
                }
            }

            // Panel background
            draw_rect(window, 0, INV_TOP, WIN_W, INV_HEIGHT,
                      sf::Color{12, 15, 22, 210}, C_BORDER);

            if (inv_who >= 0) {
                const Entity& ie = snap.entities[inv_who];

                // Label
                {
                    sf::Text t; t.setFont(font);
                    char tbuf[64];
                    std::snprintf(tbuf, sizeof(tbuf),
                        "INVENTORY: %s%s   (LTS: %d/%d)",
                        ie.hero_name[0] ? ie.hero_name : "PLAYER",
                        (active == inv_who) ? "  [active turn]" : "",
                        ie.lts_count, LTS_CAPACITY);
                    t.setString(tbuf);
                    t.setCharacterSize(11); t.setStyle(sf::Text::Bold);
                    t.setFillColor(C_AMBER_DIM);
                    t.setPosition(10, INV_TOP + 4);
                    window.draw(t);
                }

                // Per-weapon colors
                static const sf::Color WCOLORS[] = {
                    {255, 193,  7, 255},   // 0 Solar Core - yellow
                    {103,  58, 183, 255},  // 1 Lunar Blade - purple
                    {158, 158, 158, 255},  // 2 Iron Halberd - grey
                    { 76, 175,  80, 255},  // 3 Venom Dagger - green
                    { 33, 150, 243, 255},  // 4 Thunderstaff - blue
                    {145,  85,  35, 255},  // 5 Obsidian Axe - brown
                    {255, 255, 255, 255},  // 6 Frostbow - white
                    {183, 109,  60, 255}   // 7 Splinter Stick - tan
                };

                // 20-slot active inventory (centered)
                const float SLOT_W = 28.f;
                const float SLOT_H = 26.f;
                const float SLOT_GAP = 2.f;
                const float TOTAL_W = 20 * SLOT_W + 19 * SLOT_GAP;
                const float INV_X0  = (WIN_W - TOTAL_W) / 2.f;
                const float INV_Y0  = INV_TOP + 18;

                for (int sl = 0; sl < INVENTORY_SLOTS; sl++) {
                    float sx = INV_X0 + sl * (SLOT_W + SLOT_GAP);
                    int wid = ie.inventory[sl].id;
                    bool is_first = (wid >= 0 && ie.inventory[sl].start_slot == sl);
                    sf::Color fill = (wid >= 0 && wid < 8)
                        ? WCOLORS[wid] : sf::Color{40, 45, 60, 200};
                    draw_rect(window, sx, INV_Y0, SLOT_W, SLOT_H, fill,
                              sf::Color{60, 65, 80, 255});
                    if (is_first) {
                        const WeaponDef* def = get_weapon_def(wid);
                        if (def) {
                            sf::Text c; c.setFont(font);
                            char ch[2] = { def->name[0], 0 };
                            c.setString(ch); c.setCharacterSize(13);
                            c.setStyle(sf::Text::Bold);
                            c.setFillColor(sf::Color::Black);
                            c.setPosition(sx + 4, INV_Y0 + 5); window.draw(c);
                        }
                    }
                }

                {
                    sf::Text t; t.setFont(font); t.setString("LTS:");
                    t.setCharacterSize(10); t.setFillColor(C_DIM);
                    t.setPosition(INV_X0, INV_Y0 + SLOT_H + 4); window.draw(t);
                }
                float lts_x = INV_X0 + 30;
                float lts_y = INV_Y0 + SLOT_H + 4;
                float lts_sw = 13.f, lts_sh = 13.f;
                int max_show = (int)((TOTAL_W - 30) / (lts_sw + 1));
                for (int li = 0; li < ie.lts_count && li < max_show; li++) {
                    int wid = ie.long_term_storage[li].id;
                    sf::Color fill = (wid >= 0 && wid < 8)
                        ? WCOLORS[wid] : sf::Color{60, 65, 80, 200};
                    draw_rect(window, lts_x + li * (lts_sw + 1), lts_y,
                              lts_sw, lts_sh, fill, sf::Color{40, 45, 60, 255});
                }
            }
        }

        const int HINT_Y = INV_TOP + INV_HEIGHT + 2;
        {
            sf::Text t; t.setFont(font);
            if (waiting_for_lts)
                t.setString("SWAP IN: Press 1-9 to pick from storage  [ESC]=Cancel");
            else if (waiting_for_weapon)
                t.setString("SELECT WEAPON: Press 1-9  [ESC]=Cancel");
            else if (waiting_for_target)
                t.setString("SELECT ENEMY: Press 1-9 to target  [ESC]=Cancel");
            else
                t.setString("[1]Strike [2]Exhaust [3]Weapon [4]SwapIn [5]Heal [6]Skip [U]Ult [Q]Quit");
            t.setCharacterSize(11);
            t.setFillColor(waiting_for_target ? C_RED :
                           waiting_for_weapon ? C_GREEN :
                           waiting_for_lts    ? C_CYAN : C_AMBER);
            t.setPosition(LEFT_X, HINT_Y); window.draw(t);
        }

        const int FIELD_TOP = HINT_Y + 18;
        const int FIELD_BOT = LOG_Y - 50;     // leave room for modal + artifact strip
        const int FIELD_H   = FIELD_BOT - FIELD_TOP;

        {
            const int HERO_X       = 90;          // center x of hero column
            const int HERO_BOX_W   = 160;
            const int n            = std::max(1, snap.num_players);
            const int SLOT_H       = FIELD_H / n;

            for (int i = 0; i < snap.num_players && i < MAX_PLAYERS; i++) {
                const Entity& e = snap.entities[i];
                int slot_y_top = FIELD_TOP + i * SLOT_H;
                int slot_y_bot = slot_y_top + SLOT_H - 8;
                bool is_cur    = (snap.current_turn_entity == i);
                bool is_active = (active == i);

                // Highlight ring for the active turn
                if (is_cur || is_active) {
                    sf::Color ring = is_active ? C_AMBER : sf::Color{255,176,0,140};
                    draw_rect(window,
                              HERO_X - HERO_BOX_W/2, slot_y_top + 2,
                              HERO_BOX_W, SLOT_H - 12,
                              sf::Color{0,0,0,60}, ring);
                }

                float bar_w = HERO_BOX_W - 30;
                float bar_x = HERO_X - bar_w / 2.f;
                float hp_y  = slot_y_top + 18;
                float sp_y  = slot_y_top + 32;

                // Name above the bars
                {
                    sf::Text nm; nm.setFont(font);
                    char nb[48];
                    std::snprintf(nb, sizeof(nb), "P%d  %s", i+1,
                        e.hero_name[0] ? e.hero_name : "PLAYER");
                    nm.setString(nb); nm.setCharacterSize(12);
                    nm.setStyle(sf::Text::Bold);
                    nm.setFillColor(is_cur ? C_AMBER : C_TEXT);
                    sf::FloatRect tb = nm.getLocalBounds();
                    nm.setPosition(HERO_X - tb.width/2.f - tb.left,
                                   slot_y_top + 2);
                    window.draw(nm);
                }

                if (e.alive) {
                    float hp_frac = e.max_hp > 0 ? (float)e.hp / e.max_hp : 0.f;
                    draw_bar(window, bar_x, hp_y, bar_w, 9, hp_frac,
                             hp_color(hp_frac), sf::Color{20,25,35,210});
                    {
                        sf::Text t; t.setFont(font);
                        char b[32];
                        std::snprintf(b, sizeof(b), "%d/%d", e.hp, e.max_hp);
                        t.setString(b); t.setCharacterSize(9);
                        t.setFillColor(C_TEXT);
                        t.setPosition(bar_x + 2, hp_y - 1);
                        window.draw(t);
                    }
                    float sp_frac = e.max_stamina > 0 ? (float)e.stamina / e.max_stamina : 0.f;
                    draw_bar(window, bar_x, sp_y, bar_w, 7, sp_frac,
                             C_CYAN, sf::Color{15,20,30,210});

                    // STUNNED badge
                    if (e.stunned) {
                        sf::Text st; st.setFont(font); st.setString("[STUN]");
                        st.setCharacterSize(10); st.setFillColor(C_RED);
                        st.setStyle(sf::Text::Bold);
                        sf::FloatRect tb = st.getLocalBounds();
                        st.setPosition(HERO_X - tb.width/2.f, sp_y + 9);
                        window.draw(st);
                    }
                } else {
                    sf::Text t; t.setFont(font); t.setString("DEFEATED");
                    t.setCharacterSize(11); t.setStyle(sf::Text::Bold);
                    t.setFillColor(C_DEAD);
                    sf::FloatRect tb = t.getLocalBounds();
                    t.setPosition(HERO_X - tb.width/2.f, hp_y);
                    window.draw(t);
                }

                const sf::Texture* tex = hero_frame_for(e.hero_id, anim_tick, i * 3);
                if (tex) {
                    sf::Color tint = e.alive ? sf::Color::White
                                             : sf::Color(60, 60, 60, 200);
                    if (e.stunned) tint = sf::Color(220, 200, 80, 255);
                    draw_sprite_fit(window, *tex,
                                    HERO_X, slot_y_bot,
                                    HERO_BOX_W - 30, SLOT_H - 60, tint);
                }
            }
        }

        {
            const int GRID_X0 = 480;
            const int GRID_X1 = WIN_W - 20;
            const int GRID_W  = GRID_X1 - GRID_X0;
            const int N_COLS  = 3;
            const int N_ROWS  = 3;
            const int CELL_W  = GRID_W / N_COLS;
            const int CELL_H  = FIELD_H / N_ROWS;

            for (int i = 0; i < snap.num_enemies && i < MAX_ENEMIES; i++) {
                int gid = MAX_PLAYERS + i;
                const Entity& e = snap.entities[gid];
                int local = enemy_local_idx(gid);
                int col = i % N_COLS;
                int row = i / N_COLS;
                int cx = GRID_X0 + col * CELL_W + CELL_W / 2;
                int cy_top = FIELD_TOP + row * CELL_H;
                int cy_bot = cy_top + CELL_H - 8;
                bool is_cur = (snap.current_turn_entity == gid);

                // Highlight ring for current turn
                if (is_cur) {
                    draw_rect(window,
                              cx - CELL_W/2 + 4, cy_top + 4,
                              CELL_W - 8, CELL_H - 12,
                              sf::Color{40,0,40,80}, C_MAGENTA);
                }

                float bar_w = CELL_W - 30;
                float bar_x = cx - bar_w / 2.f;
                float lbl_y = cy_top + 2;
                float hp_y  = cy_top + 16;
                float sp_y  = cy_top + 28;

                {
                    sf::Text nm; nm.setFont(font);
                    char nb[24];
                    std::snprintf(nb, sizeof(nb), "E%d", i + 1);
                    nm.setString(nb); nm.setCharacterSize(11);
                    nm.setStyle(sf::Text::Bold);
                    nm.setFillColor(is_cur ? C_MAGENTA : C_TEXT);
                    sf::FloatRect tb = nm.getLocalBounds();
                    nm.setPosition(cx - tb.width/2.f, lbl_y);
                    window.draw(nm);
                }

                if (e.alive) {
                    float hp_frac = e.max_hp > 0 ? (float)e.hp / e.max_hp : 0.f;
                    draw_bar(window, bar_x, hp_y, bar_w, 8, hp_frac,
                             hp_color(hp_frac), sf::Color{20,25,35,210});
                    float sp_frac = e.max_stamina > 0 ? (float)e.stamina / e.max_stamina : 0.f;
                    draw_bar(window, bar_x, sp_y, bar_w, 6, sp_frac,
                             C_CYAN, sf::Color{15,20,30,210});

                    if (e.stunned) {
                        sf::Text st; st.setFont(font); st.setString("[STUN]");
                        st.setCharacterSize(9); st.setFillColor(C_RED);
                        st.setStyle(sf::Text::Bold);
                        sf::FloatRect tb = st.getLocalBounds();
                        st.setPosition(cx - tb.width/2.f, sp_y + 7);
                        window.draw(st);
                    }
                } else {
                    sf::Text t; t.setFont(font); t.setString("DEAD");
                    t.setCharacterSize(11); t.setStyle(sf::Text::Bold);
                    t.setFillColor(C_DEAD);
                    sf::FloatRect tb = t.getLocalBounds();
                    t.setPosition(cx - tb.width/2.f, hp_y);
                    window.draw(t);
                }

                const sf::Texture* tex = enemy_frame_for(local, anim_tick, i * 5);
                if (tex) {
                    // Use draw_sprite_fit but with horizontal mirror via setScale(-x, y)
                    sf::Vector2u sz = tex->getSize();
                    if (sz.x > 0 && sz.y > 0) {
                        float max_w = CELL_W - 28.f;
                        float max_h = CELL_H - 50.f;
                        float scale = std::min(max_w / (float)sz.x,
                                               max_h / (float)sz.y);
                        if (scale > 0.f) {
                            sf::Sprite spr(*tex);
                            spr.setScale(-scale, scale);    // mirror horizontally
                            float draw_w = sz.x * scale;
                            float draw_h = sz.y * scale;
                            spr.setPosition(cx + draw_w / 2.f,
                                            cy_bot - draw_h);
                            sf::Color tint = e.alive ? sf::Color::White
                                                     : sf::Color(60, 60, 60, 200);
                            if (e.stunned) tint = sf::Color(220, 200, 80, 255);
                            spr.setColor(tint);
                            window.draw(spr);
                        }
                    }
                }
            }
        }

        {
            sf::Text t; t.setFont(font);
            char abuf[160];
            std::snprintf(abuf, sizeof(abuf),
                "Solar Core: %-8s  Lunar Blade: %-8s  Eclipse Relic: %s",
                snap.artifacts[ARTIFACT_SOLAR].locked_by < 0
                    ? (snap.artifacts[ARTIFACT_SOLAR].present ? "world" : "---")
                    : (is_player_id(snap.artifacts[ARTIFACT_SOLAR].locked_by) ? "player" : "enemy"),
                snap.artifacts[ARTIFACT_LUNAR].locked_by < 0
                    ? (snap.artifacts[ARTIFACT_LUNAR].present ? "world" : "---")
                    : (is_player_id(snap.artifacts[ARTIFACT_LUNAR].locked_by) ? "player" : "enemy"),
                snap.artifacts[ARTIFACT_ECLIPSE].present
                    ? (snap.artifacts[ARTIFACT_ECLIPSE].locked_by < 0 ? "world" : "held")
                    : "not spawned");
            t.setString(abuf); t.setCharacterSize(11); t.setFillColor(C_TEXT);
            // Background strip so the dark text reads on the lava
            draw_rect(window, 0, LOG_Y - 22, WIN_W, 18,
                      sf::Color{12,15,22,200}, sf::Color::Transparent);
            t.setPosition(LEFT_X, LOG_Y - 20); window.draw(t);
        }

        if (snap.pending_drop.pending) {
            const WeaponDef* def = get_weapon_def(snap.pending_drop.weapon_id);
            char dbuf[128];
            std::snprintf(dbuf, sizeof(dbuf),
                "WEAPON DROP: %s — Pick up? [Y] Yes  [N] No",
                def ? def->name : "Unknown");
            draw_rect(window, 0, LOG_Y - 48, WIN_W, 26,
                      sf::Color{40,10,10,230}, C_AMBER);
            sf::Text dt; dt.setFont(font); dt.setString(dbuf);
            dt.setCharacterSize(13); dt.setStyle(sf::Text::Bold); dt.setFillColor(C_AMBER);
            dt.setPosition(20, LOG_Y - 44); window.draw(dt);
        }

        if (waiting_for_weapon && active >= 0) {
            draw_rect(window, 0, LOG_Y - 48, WIN_W, 26,
                      sf::Color{10,30,10,230}, C_AMBER);
            sf::Text it; it.setFont(font);
            char ibuf[256]; ibuf[0] = '\0';
            int idx = 1;
            for (int sl = 0; sl < INVENTORY_SLOTS; sl++) {
                const Weapon& w = snap.entities[active].inventory[sl];
                if (w.id >= 0 && w.start_slot == sl) {
                    const WeaponDef* def = get_weapon_def(w.id);
                    char tmp[64];
                    std::snprintf(tmp, sizeof(tmp), "[%d]%s(%ddmg) ",
                        idx++, def ? def->name : "?", w.damage);
                    std::strncat(ibuf, tmp, sizeof(ibuf) - strlen(ibuf) - 1);
                }
            }
            it.setString(std::string("WEAPON: ") + ibuf + " [ESC]=Cancel");
            it.setCharacterSize(12); it.setFillColor(C_GREEN);
            it.setPosition(10, LOG_Y - 44); window.draw(it);
        }

        if (waiting_for_lts && active >= 0) {
            draw_rect(window, 0, LOG_Y - 48, WIN_W, 26,
                      sf::Color{10,10,40,230}, C_AMBER);
            sf::Text lt; lt.setFont(font);
            char lbuf[256]; lbuf[0] = '\0';
            const Entity& pe = snap.entities[active];
            for (int i = 0; i < pe.lts_count && i < 9; i++) {
                const WeaponDef* def = get_weapon_def(pe.long_term_storage[i].id);
                char tmp[48];
                std::snprintf(tmp, sizeof(tmp), "[%d]%s ", i+1, def ? def->name : "?");
                std::strncat(lbuf, tmp, sizeof(lbuf) - strlen(lbuf) - 1);
            }
            lt.setString(std::string("STORAGE: ") + lbuf + " [ESC]=Cancel");
            lt.setCharacterSize(12); lt.setFillColor(C_CYAN);
            lt.setPosition(10, LOG_Y - 44); window.draw(lt);
        }

        if (status_msg[0] != '\0') {
            draw_rect(window, 0, LOG_Y - 48, WIN_W, 26,
                      sf::Color{50, 30, 10, 230}, C_AMBER);
            sf::Text mt; mt.setFont(font);
            mt.setString(status_msg);
            mt.setCharacterSize(13); mt.setFillColor(C_YELLOW);
            mt.setPosition(10, LOG_Y - 44); window.draw(mt);
        }

        draw_rect(window, 0, LOG_Y, WIN_W, LOG_H,
                  sf::Color{12,15,22,235}, C_BORDER);
        {
            sf::Text label; label.setFont(font); label.setString("ACTION LOG");
            label.setCharacterSize(11); label.setFillColor(C_AMBER_DIM);
            label.setPosition(10, LOG_Y + 4); window.draw(label);
        }
        for (int li = 0; li < 5; li++) {
            int idx = ((snap.log_head - 5 + li) % MAX_LOG_LINES + MAX_LOG_LINES) % MAX_LOG_LINES;
            if (snap.action_log[idx][0] == '\0') continue;
            sf::Text t; t.setFont(font);
            t.setString(snap.action_log[idx]); t.setCharacterSize(12);
            t.setFillColor(li == 4 ? C_TEXT : C_DIM);
            t.setPosition(10, LOG_Y + 18 + li * 19); window.draw(t);
        }

        if (snap.status == GS_WIN || snap.status == GS_LOSE) {
            sf::RectangleShape dim2({(float)WIN_W, (float)WIN_H});
            dim2.setFillColor({0,0,0,180}); window.draw(dim2);
            sf::Text t; t.setFont(font);
            t.setString(snap.status == GS_WIN
                ? "VICTORY — 10 enemies defeated!"
                : "DEFEAT — All players have fallen.");
            t.setCharacterSize(36); t.setStyle(sf::Text::Bold);
            t.setFillColor(snap.status == GS_WIN ? C_AMBER : C_RED);
            sf::FloatRect tb = t.getLocalBounds();
            t.setPosition((WIN_W - tb.width)/2.f - tb.left,
                          (WIN_H - tb.height)/2.f - tb.top);
            window.draw(t);
        }

        window.display();
    } // end main loop

    return nullptr;
}

// Public API

void renderer_start(GameState* state, const char* window_title) {
    g_state_ptr  = state;
    g_render_run = 1;
    if (window_title && window_title[0]) {
        std::snprintf(g_window_title, sizeof(g_window_title),
                      "%s", window_title);
    }
    pthread_create(&g_render_tid, nullptr, render_loop, nullptr);
}

void renderer_stop() {
    g_render_run = 0;
    pthread_join(g_render_tid, nullptr);
}