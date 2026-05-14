#include "character_select.h"
#include <SFML/Graphics.hpp>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>


static const sf::Color CS_BG        {8,   10,  14,  255};
static const sf::Color CS_PANEL     {18,  22,  30,  255};
static const sf::Color CS_BORDER    {45,  50,  65,  255};
static const sf::Color CS_AMBER     {255, 176, 0,   255};
static const sf::Color CS_AMBER_DIM {140, 90,  0,   255};
static const sf::Color CS_TEXT      {210, 215, 225, 255};
static const sf::Color CS_DIM       {80,  85,  95,  255};
static const sf::Color CS_GREEN     {60,  200, 100, 255};
static const sf::Color CS_RED       {220, 60,  60,  255};
static const sf::Color CS_CYAN      {60,  200, 220, 255};
static const sf::Color CS_SELECTED  {255, 176, 0,   60};
static const sf::Color CS_DEAD      {40,  40,  50,  255};


struct HeroDef {
    const char* name;
    const char* title;
    const char* desc;
    const char* stat1;
    const char* stat2;
    sf::Color   accent;        // unique color per hero
    int         body_type;     // legacy fallback only (used if sprite fails to load)
    const char* sprite_path;   // PNG portrait, loaded as sf::Texture
};

static const HeroDef HEROES[4] = {
    {
        "ALYA",
        "THE WARRIOR",
        "Skilled blademaster.\nBalanced HP and damage.",
        "HP:  ROLL + 100..1000",
        "DMG: LAST + 10",
        {220, 80,  60,  255},   // red
        0,
        "hip/assets/alya.png"
    },
    {
        "CHRONO",
        "THE TIMEKEEPER",
        "Wields the chrono-blade.\nFast and decisive.",
        "HP:  ROLL + 100..1000",
        "DMG: LAST + 10",
        {120, 80,  220, 255},   // purple
        1,
        "hip/assets/chrono.png"
    },
    {
        "FROG",
        "THE KNIGHT",
        "Honor-bound swordsman.\nReliable in any fight.",
        "HP:  ROLL + 100..1000",
        "DMG: LAST + 10",
        {60,  200, 100, 255},   // green
        2,
        "hip/assets/frog.png"
    },
    {
        "MAGNUS",
        "THE FALLEN",
        "Cursed prophet.\nLong reach, dark power.",
        "HP:  ROLL + 100..1000",
        "DMG: LAST + 10",
        {60,  160, 220, 255},   // blue
        3,
        "hip/assets/magnus.png"
    },
};


static void draw_hero_sprite(sf::RenderWindow& w,
                             float cx, float cy,
                             sf::Color accent, int body_type,
                             float scale = 1.0f) {
    // All coords relative to (cx, cy) center-bottom

    auto rect = [&](float x, float y, float bw, float bh, sf::Color c) {
        sf::RectangleShape r({bw * scale, bh * scale});
        r.setPosition(cx + x * scale, cy + y * scale);
        r.setFillColor(c);
        w.draw(r);
    };

    auto circle = [&](float x, float y, float r_size, sf::Color c) {
        sf::CircleShape circ(r_size * scale);
        circ.setPosition(cx + x * scale, cy + y * scale);
        circ.setFillColor(c);
        w.draw(circ);
    };

    sf::Color skin   {220, 185, 150, 255};
    sf::Color dark   {30,  35,  45,  255};
    sf::Color hair   {60,  40,  20,  255};

    // Body types differ in armor/weapon/pose
    if (body_type == 0) {
        // WARRIOR — broad shoulders, sword, heavy armor
        // legs
        rect(-10, -40, 8, 20, accent);
        rect(2,   -40, 8, 20, accent);
        // torso — wide plate
        rect(-14, -70, 28, 30, accent);
        // shoulder pads
        rect(-18, -72, 8, 10, sf::Color{180,180,180,255});
        rect(10,  -72, 8, 10, sf::Color{180,180,180,255});
        // head
        circle(-8, -90, 8, skin);
        // helmet
        rect(-10, -92, 20, 10, sf::Color{160,160,160,255});
        rect(-8,  -96, 16, 6,  accent);
        // sword (right)
        rect(16,  -90, 4, 40, sf::Color{200,200,220,255});
        rect(12,  -74, 12, 4, sf::Color{160,140,80,255});
        // shield (left)
        rect(-26, -78, 10, 22, accent);
        rect(-24, -76, 6,  18, sf::Color{180,180,180,255});
    }
    else if (body_type == 1) {
        // MAGE — robes, staff, pointed hat
        // robe
        rect(-10, -60, 20, 40, accent);
        // wide robe bottom
        rect(-14, -30, 28, 12, accent);
        // torso
        rect(-8,  -80, 16, 22, sf::Color{
            (sf::Uint8)(accent.r/2),
            (sf::Uint8)(accent.g/2),
            (sf::Uint8)(accent.b/2), 255});
        // arms
        rect(-16, -78, 8, 16, accent);
        rect(8,   -78, 8, 16, accent);
        // head
        circle(-7, -92, 7, skin);
        // pointed hat
        // hat brim
        rect(-14, -88, 28, 5, dark);
        // hat cone (triangle-ish with rects)
        rect(-4,  -108, 8, 6,  accent);
        rect(-6,  -102, 12, 6, accent);
        rect(-8,  -96,  16, 6, accent);
        // star on hat
        rect(-2,  -105, 4, 4, CS_AMBER);
        // staff
        rect(14,  -110, 4, 70, sf::Color{120, 80, 40, 255});
        // orb on staff
        circle(12, -116, 6, accent);
        circle(13, -117, 4, sf::Color{255,255,200,200});
    }
    else if (body_type == 2) {
        // RANGER — hood, bow, quiver
        // legs (slightly apart)
        rect(-12, -40, 9, 22, sf::Color{60,80,50,255});
        rect(3,   -40, 9, 22, sf::Color{60,80,50,255});
        // torso — leather vest
        rect(-10, -72, 20, 32, sf::Color{80, 100, 60, 255});
        // belt
        rect(-10, -44, 20, 4, sf::Color{100, 70, 30, 255});
        // quiver on back
        rect(10,  -78, 6, 24, sf::Color{100, 70, 30, 255});
        rect(12,  -76, 2, 4, CS_AMBER);
        rect(12,  -70, 2, 4, CS_AMBER);
        // head
        circle(-7, -88, 7, skin);
        // hood
        rect(-12, -92, 24, 10, sf::Color{60,80,50,255});
        rect(-8,  -98, 16, 8,  sf::Color{60,80,50,255});
        // bow (left side, curved via 3 rects)
        rect(-24, -96, 4, 50, sf::Color{120, 80, 40, 255});
        rect(-22, -100,4, 6,  sf::Color{120, 80, 40, 255});
        rect(-22, -52, 4, 6,  sf::Color{120, 80, 40, 255});
        // bowstring
        rect(-21, -94, 2, 46, sf::Color{220,200,160,180});
        // arrow nocked
        rect(-20, -72, 14, 2, CS_AMBER);
    }
    else {
        // PALADIN — full plate, halo, mace
        // legs — greaves
        rect(-10, -40, 9, 22, sf::Color{180,180,200,255});
        rect(2,   -40, 9, 22, sf::Color{180,180,200,255});
        // tassets
        rect(-14, -50, 12, 12, sf::Color{160,160,180,255});
        rect(2,   -50, 12, 12, sf::Color{160,160,180,255});
        // breastplate
        rect(-12, -76, 24, 28, sf::Color{200,200,220,255});
        rect(-10, -74, 20, 24, accent);
        // pauldrons
        rect(-18, -78, 10, 10, sf::Color{200,200,220,255});
        rect(8,   -78, 10, 10, sf::Color{200,200,220,255});
        // head
        circle(-7, -90, 7, skin);
        // great helm
        rect(-10, -94, 20, 12, sf::Color{180,180,200,255});
        rect(-8,  -96, 16, 4,  accent);
        // halo
        sf::CircleShape halo(14 * scale, 30);
        halo.setPosition(cx - 14*scale, cy - 114*scale);
        halo.setFillColor(sf::Color::Transparent);
        halo.setOutlineColor(CS_AMBER);
        halo.setOutlineThickness(2 * scale);
        w.draw(halo);
        // mace
        rect(16,  -90, 5, 38, sf::Color{160,160,170,255});
        rect(12,  -94, 13, 8, sf::Color{200,200,220,255});
        rect(14,  -92, 9,  4, accent);
    }
}


static void draw_rounded_rect(sf::RenderWindow& w,
                              float x, float y, float bw, float bh,
                              float r, sf::Color fill, sf::Color border_c) {
    // fill
    sf::RectangleShape mid({bw, bh - 2*r});
    mid.setPosition(x, y + r);
    mid.setFillColor(fill);
    w.draw(mid);

    sf::RectangleShape top_b({bw - 2*r, r});
    top_b.setPosition(x + r, y);
    top_b.setFillColor(fill);
    w.draw(top_b);

    sf::RectangleShape bot_b({bw - 2*r, r});
    bot_b.setPosition(x + r, y + bh - r);
    bot_b.setFillColor(fill);
    w.draw(bot_b);

    // corners
    auto corner = [&](float cx2, float cy2) {
        sf::CircleShape c(r);
        c.setPosition(cx2, cy2);
        c.setFillColor(fill);
        w.draw(c);
    };
    corner(x,          y);
    corner(x + bw - 2*r, y);
    corner(x,          y + bh - 2*r);
    corner(x + bw - 2*r, y + bh - 2*r);

    // border outline
    sf::RectangleShape outline({bw, bh});
    outline.setPosition(x, y);
    outline.setFillColor(sf::Color::Transparent);
    outline.setOutlineColor(border_c);
    outline.setOutlineThickness(2.f);
    w.draw(outline);
}

// Main entry point

int run_character_select(sf::RenderWindow& window, const sf::Font& font,
                         bool selected_out[4]) {
    const int W = window.getSize().x;
    const int H = window.getSize().y;

    // Loaded once, kept alive in this scope. If a sprite fails to load
    // (missing file etc.) we fall back to the legacy geometric drawing.
    sf::Texture hero_tex[4];
    bool        hero_tex_ok[4] = {false, false, false, false};
    for (int i = 0; i < 4; i++) {
        if (hero_tex[i].loadFromFile(HEROES[i].sprite_path)) {
            hero_tex[i].setSmooth(false);   // pixel-art: keep crisp
            hero_tex_ok[i] = true;
        } else {
            std::fprintf(stderr,
                "[char_select] WARN: failed to load %s; using geometric fallback\n",
                HEROES[i].sprite_path);
        }
    }

    bool selected[4] = {false, false, false, false};
    bool hovered[4]  = {false, false, false, false};

    // Card layout — 4 cards side by side
    const float CARD_W   = 240.f;
    const float CARD_H   = 380.f;
    const float CARD_GAP = 24.f;
    const float TOTAL_W  = 4 * CARD_W + 3 * CARD_GAP;
    const float START_X  = (W - TOTAL_W) / 2.f;
    const float CARD_Y   = (H - CARD_H) / 2.f - 30.f;

    // Confirm button
    const float BTN_W = 200.f, BTN_H = 48.f;
    const float BTN_X = (W - BTN_W) / 2.f;
    const float BTN_Y = CARD_Y + CARD_H + 28.f;

    float anim_time = 0.f;
    sf::Clock clk;

    while (window.isOpen()) {
        float dt = clk.restart().asSeconds();
        anim_time += dt;

        sf::Vector2i mouse = sf::Mouse::getPosition(window);

        // Update hover states
        for (int i = 0; i < 4; i++) {
            float cx = START_X + i * (CARD_W + CARD_GAP);
            hovered[i] = sf::FloatRect(cx, CARD_Y, CARD_W, CARD_H)
                             .contains((float)mouse.x, (float)mouse.y);
        }

        // Count selected
        int sel_count = 0;
        for (int i = 0; i < 4; i++) if (selected[i]) sel_count++;

        bool btn_hov = sf::FloatRect(BTN_X, BTN_Y, BTN_W, BTN_H)
                           .contains((float)mouse.x, (float)mouse.y);

        // Events
        sf::Event ev;
        while (window.pollEvent(ev)) {
            if (ev.type == sf::Event::Closed) {
                window.close();
                return 0;
            }
            if (ev.type == sf::Event::MouseButtonReleased &&
                ev.mouseButton.button == sf::Mouse::Left) {
                int mx = ev.mouseButton.x, my = ev.mouseButton.y;

                // Card click — toggle
                for (int i = 0; i < 4; i++) {
                    float cx = START_X + i * (CARD_W + CARD_GAP);
                    if (sf::FloatRect(cx, CARD_Y, CARD_W, CARD_H)
                            .contains((float)mx, (float)my)) {
                        selected[i] = !selected[i];
                    }
                }

                // Confirm button
                if (sel_count > 0 &&
                    sf::FloatRect(BTN_X, BTN_Y, BTN_W, BTN_H)
                        .contains((float)mx, (float)my)) {
                    for (int i = 0; i < 4; i++) selected_out[i] = selected[i];
                    return sel_count;
                }
            }
            // Keyboard shortcut — 1/2/3/4 toggle heroes
            if (ev.type == sf::Event::KeyPressed) {
                int idx = -1;
                if (ev.key.code == sf::Keyboard::Num1) idx = 0;
                if (ev.key.code == sf::Keyboard::Num2) idx = 1;
                if (ev.key.code == sf::Keyboard::Num3) idx = 2;
                if (ev.key.code == sf::Keyboard::Num4) idx = 3;
                if (idx >= 0) selected[idx] = !selected[idx];

                // Enter confirms
                if (ev.key.code == sf::Keyboard::Return && sel_count > 0) {
                    for (int i = 0; i < 4; i++) selected_out[i] = selected[i];
                    return sel_count;
                }
            }
        }

        window.clear(CS_BG);

        // Animated scanline background
        for (int y = 0; y < H; y += 4) {
            sf::RectangleShape line({(float)W, 1.f});
            line.setPosition(0, (float)y);
            line.setFillColor(sf::Color{255, 255, 255, 8});
            window.draw(line);
        }

        // Title
        {
            sf::Text title; title.setFont(font);
            title.setString("CHOOSE YOUR PARTY");
            title.setCharacterSize(32);
            title.setStyle(sf::Text::Bold);
            title.setFillColor(CS_AMBER);
            sf::FloatRect tb = title.getLocalBounds();
            title.setPosition((W - tb.width) / 2.f - tb.left, 40.f);
            window.draw(title);

            // Decorative underline
            float uw = tb.width + 40;
            sf::RectangleShape ul({uw, 2.f});
            ul.setPosition((W - uw) / 2.f, 82.f);
            ul.setFillColor(CS_AMBER);
            window.draw(ul);
        }

        // Subtitle
        {
            sf::Text sub; sub.setFont(font);
            sub.setString("Select 1 to 4 heroes  |  Click or press 1-4  |  ENTER or CONFIRM to start");
            sub.setCharacterSize(12);
            sub.setFillColor(CS_DIM);
            sf::FloatRect sb = sub.getLocalBounds();
            sub.setPosition((W - sb.width) / 2.f - sb.left, 92.f);
            window.draw(sub);
        }

        // Hero cards
        for (int i = 0; i < 4; i++) {
            float cx = START_X + i * (CARD_W + CARD_GAP);
            const HeroDef& h = HEROES[i];

            bool sel = selected[i];
            bool hov = hovered[i];

            // Card background
            sf::Color card_fill = sel
                ? sf::Color{28, 34, 46, 255}
                : sf::Color{18, 22, 30, 255};
            sf::Color card_border = sel ? h.accent
                : (hov ? CS_AMBER_DIM : CS_BORDER);

            // Hover lift effect
            float lift = hov ? -6.f : 0.f;

            // Selected glow background
            if (sel) {
                sf::RectangleShape glow({CARD_W + 20, CARD_H + 20});
                glow.setPosition(cx - 10, CARD_Y + lift - 10);
                glow.setFillColor(sf::Color{
                    h.accent.r, h.accent.g, h.accent.b, 30});
                window.draw(glow);
            }

            draw_rounded_rect(window, cx, CARD_Y + lift,
                              CARD_W, CARD_H, 6.f, card_fill, card_border);

            // Selected indicator strip at top
            if (sel) {
                sf::RectangleShape strip({CARD_W - 4, 4.f});
                strip.setPosition(cx + 2, CARD_Y + lift + 2);
                strip.setFillColor(h.accent);
                window.draw(strip);
            }

            // Hero number badge
            {
                sf::CircleShape badge(14.f);
                badge.setPosition(cx + 10, CARD_Y + lift + 10);
                badge.setFillColor(sel ? h.accent : CS_PANEL);
                badge.setOutlineColor(sel ? h.accent : CS_BORDER);
                badge.setOutlineThickness(1.f);
                window.draw(badge);

                sf::Text num; num.setFont(font);
                char nbuf[4]; std::snprintf(nbuf, sizeof(nbuf), "%d", i+1);
                num.setString(nbuf);
                num.setCharacterSize(14);
                num.setStyle(sf::Text::Bold);
                num.setFillColor(sel ? sf::Color::Black : CS_DIM);
                sf::FloatRect nb = num.getLocalBounds();
                num.setPosition(cx + 10 + 14 - nb.width/2 - nb.left,
                                CARD_Y + lift + 10 + 14 - nb.height/2 - nb.top);
                window.draw(num);
            }

            // Sprite area background
            sf::RectangleShape sprite_bg({CARD_W - 20, 160.f});
            sprite_bg.setPosition(cx + 10, CARD_Y + lift + 40);
            sprite_bg.setFillColor(sf::Color{12, 15, 22, 255});
            sprite_bg.setOutlineColor(sf::Color{30, 35, 50, 255});
            sprite_bg.setOutlineThickness(1.f);
            window.draw(sprite_bg);

            // Draw hero sprite (real texture if loaded, geometric fallback otherwise)
            float sprite_cx = cx + CARD_W / 2.f;
            float sprite_cy = CARD_Y + lift + 190.f;
            float bob = std::sin(anim_time * 2.f + i * 1.5f) * 3.f;

            if (hero_tex_ok[i]) {
                // Fit sprite into the 160px-tall sprite area, preserve aspect
                sf::Sprite spr(hero_tex[i]);
                sf::Vector2u tex_sz = hero_tex[i].getSize();
                if (tex_sz.x > 0 && tex_sz.y > 0) {
                    const float MAX_H = 150.f;
                    const float MAX_W = CARD_W - 40.f;
                    float scale = std::min(MAX_H / (float)tex_sz.y,
                                           MAX_W / (float)tex_sz.x);
                    spr.setScale(scale, scale);
                    float draw_w = tex_sz.x * scale;
                    float draw_h = tex_sz.y * scale;
                    // Center horizontally, anchor bottom at sprite_cy + bob
                    spr.setPosition(sprite_cx - draw_w / 2.f,
                                    sprite_cy + bob - draw_h);
                    // Tint slightly with accent when selected
                    if (sel) {
                        spr.setColor(sf::Color(255, 255, 255, 255));
                    } else if (!hov) {
                        spr.setColor(sf::Color(220, 220, 220, 230));
                    }
                    window.draw(spr);
                }
            } else {
                draw_hero_sprite(window, sprite_cx, sprite_cy + bob,
                                 h.accent, h.body_type, 1.2f);
            }

            // Hero name
            {
                sf::Text name; name.setFont(font);
                name.setString(h.name);
                name.setCharacterSize(18);
                name.setStyle(sf::Text::Bold);
                name.setFillColor(sel ? h.accent : CS_TEXT);
                sf::FloatRect nb = name.getLocalBounds();
                name.setPosition(cx + (CARD_W - nb.width)/2 - nb.left,
                                 CARD_Y + lift + 210.f);
                window.draw(name);
            }

            // Hero title
            {
                sf::Text ttl; ttl.setFont(font);
                ttl.setString(h.title);
                ttl.setCharacterSize(10);
                ttl.setFillColor(sel ? sf::Color{
                    h.accent.r, h.accent.g, h.accent.b, 180} : CS_DIM);
                sf::FloatRect tb2 = ttl.getLocalBounds();
                ttl.setPosition(cx + (CARD_W - tb2.width)/2 - tb2.left,
                                CARD_Y + lift + 233.f);
                window.draw(ttl);
            }

            // Divider
            sf::RectangleShape div({CARD_W - 30, 1.f});
            div.setPosition(cx + 15, CARD_Y + lift + 252.f);
            div.setFillColor(sel ? sf::Color{
                h.accent.r, h.accent.g, h.accent.b, 80} : CS_BORDER);
            window.draw(div);

            // Stats
            {
                sf::Text s1; s1.setFont(font);
                s1.setString(h.stat1);
                s1.setCharacterSize(11);
                s1.setFillColor(CS_TEXT);
                s1.setPosition(cx + 16, CARD_Y + lift + 260.f);
                window.draw(s1);

                sf::Text s2; s2.setFont(font);
                s2.setString(h.stat2);
                s2.setCharacterSize(11);
                s2.setFillColor(CS_TEXT);
                s2.setPosition(cx + 16, CARD_Y + lift + 278.f);
                window.draw(s2);
            }

            // Description
            {
                // Simple word-wrap by splitting on \n
                const char* desc = h.desc;
                char line1[64] = "", line2[64] = "";
                const char* nl = strchr(desc, '\n');
                if (nl) {
                    int len = (int)(nl - desc);
                    strncpy(line1, desc, len); line1[len] = '\0';
                    strncpy(line2, nl + 1, sizeof(line2) - 1);
                } else {
                    strncpy(line1, desc, sizeof(line1) - 1);
                }
                sf::Text d1; d1.setFont(font);
                d1.setString(line1);
                d1.setCharacterSize(10);
                d1.setFillColor(CS_DIM);
                d1.setPosition(cx + 16, CARD_Y + lift + 300.f);
                window.draw(d1);

                if (line2[0]) {
                    sf::Text d2; d2.setFont(font);
                    d2.setString(line2);
                    d2.setCharacterSize(10);
                    d2.setFillColor(CS_DIM);
                    d2.setPosition(cx + 16, CARD_Y + lift + 315.f);
                    window.draw(d2);
                }
            }

            // Selected checkmark
            if (sel) {
                sf::Text chk; chk.setFont(font);
                chk.setString("SELECTED");
                chk.setCharacterSize(11);
                chk.setStyle(sf::Text::Bold);
                chk.setFillColor(h.accent);
                sf::FloatRect cb = chk.getLocalBounds();
                chk.setPosition(cx + (CARD_W - cb.width)/2 - cb.left,
                                CARD_Y + lift + CARD_H - 28.f);
                window.draw(chk);
            } else if (hov) {
                sf::Text chk; chk.setFont(font);
                chk.setString("CLICK TO SELECT");
                chk.setCharacterSize(10);
                chk.setFillColor(CS_DIM);
                sf::FloatRect cb = chk.getLocalBounds();
                chk.setPosition(cx + (CARD_W - cb.width)/2 - cb.left,
                                CARD_Y + lift + CARD_H - 26.f);
                window.draw(chk);
            }
        }

        // Confirm button
        {
            bool can_confirm = sel_count > 0;
            sf::Color btn_fill = can_confirm
                ? (btn_hov ? sf::Color{255,200,20,255} : sf::Color{30,36,50,255})
                : sf::Color{20,23,30,255};
            sf::Color btn_border = can_confirm ? CS_AMBER : CS_BORDER;
            sf::Color btn_text  = can_confirm ? CS_AMBER : CS_DIM;

            sf::RectangleShape btn({BTN_W, BTN_H});
            btn.setPosition(BTN_X, BTN_Y);
            btn.setFillColor(btn_fill);
            btn.setOutlineColor(btn_border);
            btn.setOutlineThickness(2.f);
            window.draw(btn);

            sf::Text bt; bt.setFont(font);
            char bbuf[32];
            if (can_confirm)
                std::snprintf(bbuf, sizeof(bbuf),
                    "CONFIRM  (%d hero%s)", sel_count, sel_count > 1 ? "es" : "");
            else
                std::snprintf(bbuf, sizeof(bbuf), "SELECT A HERO");
            bt.setString(bbuf);
            bt.setCharacterSize(14);
            bt.setStyle(sf::Text::Bold);
            bt.setFillColor(can_confirm && btn_hov ? sf::Color::Black : btn_text);
            sf::FloatRect bb = bt.getLocalBounds();
            bt.setPosition(BTN_X + (BTN_W - bb.width)/2 - bb.left,
                           BTN_Y + (BTN_H - bb.height)/2 - bb.top);
            window.draw(bt);
        }

        // Party count indicator
        if (sel_count > 0) {
            sf::Text pc; pc.setFont(font);
            char pbuf[48];
            std::snprintf(pbuf, sizeof(pbuf),
                "Party size: %d / 4", sel_count);
            pc.setString(pbuf);
            pc.setCharacterSize(12);
            pc.setFillColor(CS_AMBER_DIM);
            sf::FloatRect pb = pc.getLocalBounds();
            pc.setPosition((W - pb.width)/2 - pb.left, BTN_Y + BTN_H + 12);
            window.draw(pc);
        }

        window.display();
    }

    return 0;
}