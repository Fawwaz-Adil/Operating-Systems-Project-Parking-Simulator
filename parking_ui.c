#include "parking.h"
#include "raylib.h"
// ─────────────────────────────────────────────────────────────────────────────
// gui_button  – draws a rounded button and returns true on click
// ─────────────────────────────────────────────────────────────────────────────
bool gui_button(Rectangle r, const char *text, Color base_col)
{
    Vector2 mouse   = GetMousePosition();
    bool    hovered = CheckCollisionPointRec(mouse, r);
    bool    clicked = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    Color draw_col = hovered ? ColorAlpha(base_col, 0.8f) : base_col;
    DrawRectangleRounded(r, 0.2f, 6, draw_col);
    DrawRectangleRoundedLines(r, 0.2f, 6, COL_WHITE);

    int tw = MeasureText(text, 10);
    DrawText(text, r.x + r.width / 2 - tw / 2, r.y + r.height / 2 - 5, 10, COL_WHITE);
    return clicked;
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_panel  – rounded rectangle background with border
// ─────────────────────────────────────────────────────────────────────────────
void draw_panel(Rectangle r, Color bg, Color border)
{
    DrawRectangleRounded(r, 0.04f, 6, bg);
    DrawRectangleRoundedLines(r, 0.04f, 6, border);
}

// ─────────────────────────────────────────────────────────────────────────────
// lerp_color  – linearly interpolate between two colors
// ─────────────────────────────────────────────────────────────────────────────
Color lerp_color(Color a, Color b, float t)
{
    return CLITERAL(Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_vehicle  – renders bike / car / heavy sprite inside a slot cell
// ─────────────────────────────────────────────────────────────────────────────
void draw_vehicle(int sx, int sy, int sw, int sh, float anim, int vid, VehicleType type)
{
    int cx = sx + sw / 2;
    int cy = (int)(sy + sh / 2 + (sh * 0.1f) * (1.0f - anim) - 5);

    float hue  = (float)((vid * 47) % 360);
    Color body = ColorFromHSV(hue, 0.6f, 0.8f);

    if (type == TYPE_BIKE) {
        DrawRectangleRounded((Rectangle){cx - 8, cy - 15, 16, 30}, 0.5f, 6, body);
        DrawRectangle(cx - 3, cy - 20, 6, 8, COL_TYRE);
        DrawRectangle(cx - 3, cy + 12,  6, 8, COL_TYRE);
    }
    else if (type == TYPE_CAR) {
        int cw = sw - 16; if (cw > 60) cw = 60;
        int ch = sh - 20; if (ch > 90) ch = 90;
        int bx = cx - cw / 2, by = cy - ch / 2;

        DrawRectangleRounded((Rectangle){bx, by, cw, ch}, 0.35f, 6, body);
        DrawRectangleRounded((Rectangle){bx + 8, by + 6, cw - 16, ch / 4}, 0.3f, 4, COL_WINDOW);
        DrawRectangle(bx - 2,      by + 6,      4, 10, COL_TYRE);
        DrawRectangle(bx + cw - 2, by + 6,      4, 10, COL_TYRE);
        DrawRectangle(bx - 2,      by + ch - 16, 4, 10, COL_TYRE);
        DrawRectangle(bx + cw - 2, by + ch - 16, 4, 10, COL_TYRE);
    }
    else {
        int tw = sw - 20; if (tw > 120) tw = 120;
        int th = sh - 25; if (th > 80)  th = 80;
        int bx = cx - tw / 2, by = cy - th / 2;

        DrawRectangle(bx, by, tw, th, body);
        DrawRectangle(bx + 4, by + 4, tw - 8, 14, COL_WINDOW);
        DrawRectangle(bx - 3,      by + 8,      6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + 8,      6, 14, COL_TYRE);
        DrawRectangle(bx - 3,      by + th - 20, 6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + th - 20, 6, 14, COL_TYRE);
        DrawRectangle(bx - 3,      by + th / 2 - 7, 6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + th / 2 - 7, 6, 14, COL_TYRE);
    }

    char vid_str[6];
    snprintf(vid_str, sizeof(vid_str), "V%02d", vid);
    DrawText(vid_str, cx - MeasureText(vid_str, 10) / 2, cy - 5, 10, WHITE);
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_slot  – renders one parking cell (free or occupied)
// ─────────────────────────────────────────────────────────────────────────────
void draw_slot(int sx, int sy, int sw, int sh, ParkingSlot *s)
{
    Color bg = (s->state == SLOT_OCCUPIED) ? COL_OCC     : COL_FREE;
    Color bd = (s->state == SLOT_OCCUPIED) ? COL_OCC_BD  : COL_FREE_BD;

    DrawRectangle(sx, sy, sw, sh, bg);
    DrawRectangleLines(sx, sy, sw, sh, bd);

    char num[8];
    snprintf(num, sizeof(num), "%02d", s->slot_id);
    DrawText(num, sx + 6, sy + 5, 11, ColorAlpha(COL_WHITE, 0.4f));

    if (s->state == SLOT_OCCUPIED || s->anim > 0.0f) {
        draw_vehicle(sx, sy, sw, sh, s->anim, s->vehicle_id, s->allowed_type);

        if (s->state == SLOT_OCCUPIED && s->anim == 1.0f) {
            time_t parked = time(NULL) - s->entry_time;
            char t_str[16];
            snprintf(t_str, sizeof(t_str), "Days: %ld", (long)parked);
            DrawText(t_str, sx + sw / 2 - MeasureText(t_str, 10) / 2,
                     sy + sh - 15, 10, COL_AMBER);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_sidebar  – left panel with action buttons and gate status
// ─────────────────────────────────────────────────────────────────────────────
void draw_sidebar(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {SIDEBAR_X, SIDEBAR_Y, SIDEBAR_W, SIDEBAR_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    int x = SIDEBAR_X + 16, y = SIDEBAR_Y + 16;
    DrawText("INTERACTIVE OS PARKING", x, y, 16, COL_ACCENT); y += 30;

    DrawText("1. ADD VEHICLES", x, y, 12, COL_WHITE); y += 20;
    if (gui_button((Rectangle){x,       y, 100, 30}, "ADD BIKE",  COL_GREEN)) lot->ui_state = UI_ADD_BIKE;
    if (gui_button((Rectangle){x + 110, y, 100, 30}, "ADD CAR",   COL_BLUE))  lot->ui_state = UI_ADD_CAR;
    y += 40;
    if (gui_button((Rectangle){x, y, 210, 30}, "ADD HEAVY VEHICLE", COL_AMBER)) lot->ui_state = UI_ADD_HEAVY;

    y += 50;
    DrawText("2. REMOVE VEHICLES", x, y, 12, COL_WHITE); y += 20;
    if (gui_button((Rectangle){x, y, 210, 30}, "REMOVE VEHICLE", COL_RED)) lot->ui_state = UI_REMOVE;
    y += 45;

    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 16, y, COL_BORDER); y += 15;

    // Dynamic action hint
    const char *msg    = "IDLE";
    Color       msg_col = COL_MUTED;
    if (lot->ui_state == UI_ADD_BIKE)   { msg = "Click EMPTY Bike Slot";      msg_col = COL_GREEN; }
    if (lot->ui_state == UI_ADD_CAR)    { msg = "Click EMPTY Car Slot";       msg_col = COL_BLUE;  }
    if (lot->ui_state == UI_ADD_HEAVY)  { msg = "Click EMPTY Heavy Slot";     msg_col = COL_AMBER; }
    if (lot->ui_state == UI_REMOVE)     { msg = "Click OCCUPIED Slot to Free"; msg_col = COL_RED;  }

    DrawText("CURRENT ACTION:", x, y, 10, COL_MUTED); y += 15;
    DrawText(msg, x, y, 12, msg_col); y += 30;
    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 16, y, COL_BORDER); y += 15;

    // Entry gate indicator
    DrawText("ENTRY GATE", x, y, 12, COL_MUTED);
    float ep     = lot->entry_pulse;
    Color eg_col = ep > 0 ? lerp_color(COL_AMBER, COL_GREEN, 1.0f - ep) : COL_GREEN;
    DrawCircle(x + 180, y + 6, 7, ColorAlpha(eg_col, 0.3f));
    DrawCircleLines(x + 180, y + 6, 7, eg_col); y += 26;

    // Exit gate indicator
    DrawText("EXIT GATE", x, y, 12, COL_MUTED);
    float xp     = lot->exit_pulse;
    Color xg_col = xp > 0 ? lerp_color(COL_AMBER, COL_GREEN, 1.0f - xp) : COL_GREEN;
    DrawCircle(x + 180, y + 6, 7, ColorAlpha(xg_col, 0.3f));
    DrawCircleLines(x + 180, y + 6, 7, xg_col);
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_grid  – centre panel showing all parking zones and slots
// ─────────────────────────────────────────────────────────────────────────────
void draw_grid(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {GRID_X, GRID_Y, GRID_W, GRID_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    pthread_mutex_lock(&lot->slots_mutex);
    int pad_x = 20;

    // Zone 1 – Bikes
    DrawText("BIKE PARKING", GRID_X + pad_x, GRID_Y + 20, 12, COL_GREEN);
    int b_y = GRID_Y + 40;
    int b_w = (GRID_W - 2 * pad_x - 3 * 10) / 4;
    int b_h = 100;
    for (int i = 0; i < NUM_BIKE_SLOTS; i++) {
        lot->slots[i].bounds = (Rectangle){GRID_X + pad_x + i * (b_w + 10), b_y, b_w, b_h};
        draw_slot(lot->slots[i].bounds.x, b_y, b_w, b_h, &lot->slots[i]);
    }

    // Zone 2 – Cars
    DrawText("CAR PARKING", GRID_X + pad_x, GRID_Y + 160, 12, COL_BLUE);
    int c_y = GRID_Y + 180;
    int c_w = (GRID_W - 2 * pad_x - 5 * 10) / 6;
    int c_h = 110;
    for (int i = NUM_BIKE_SLOTS; i < NUM_BIKE_SLOTS + NUM_CAR_SLOTS; i++) {
        int idx = i - NUM_BIKE_SLOTS;
        lot->slots[i].bounds = (Rectangle){GRID_X + pad_x + idx * (c_w + 10), c_y, c_w, c_h};
        draw_slot(lot->slots[i].bounds.x, c_y, c_w, c_h, &lot->slots[i]);
    }

    // Zone 3 – Heavy
    DrawText("HEAVY VEHICLE PARKING", GRID_X + pad_x, GRID_Y + 310, 12, COL_AMBER);
    int h_y = GRID_Y + 330;
    int h_w = (GRID_W - 2 * pad_x - 1 * 10) / 2;
    int h_h = 110;
    for (int i = NUM_BIKE_SLOTS + NUM_CAR_SLOTS; i < TOTAL_SLOTS; i++) {
        int idx = i - (NUM_BIKE_SLOTS + NUM_CAR_SLOTS);
        lot->slots[i].bounds = (Rectangle){GRID_X + pad_x + idx * (h_w + 10), h_y, h_w, h_h};
        draw_slot(lot->slots[i].bounds.x, h_y, h_w, h_h, &lot->slots[i]);
    }

    pthread_mutex_unlock(&lot->slots_mutex);
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_log_panel  – bottom-centre scrolling event log
// ─────────────────────────────────────────────────────────────────────────────
void draw_log_panel(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {LOG_X, LOG_Y, LOG_W, LOG_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    DrawText("EVENT LOG", LOG_X + 16, LOG_Y + 10, 13, COL_WHITE);
    DrawLine(LOG_X + 16, LOG_Y + 28, LOG_X + LOG_W - 16, LOG_Y + 28, COL_BORDER);

    pthread_mutex_lock(&lot->event_mutex);
    int line_h   = 17;
    int max_lines = (LOG_H - 42) / line_h;
    if (max_lines > MAX_LOG_DISPLAY) max_lines = MAX_LOG_DISPLAY;

    int count = lot->event_count;
    if (count > max_lines) count = max_lines;

    for (int i = 0; i < count; i++) {
        int      idx = (lot->event_tail - 1 - i + MAX_EVENTS) % MAX_EVENTS;
        GUIEvent *ev = &lot->events[idx];
        float alpha  = 1.0f - (ev->age / 30.0f);
        if (alpha < 0.15f) alpha = 0.15f;
        DrawText(ev->text,
                 LOG_X + 12,
                 LOG_Y + LOG_H - 12 - i * line_h,
                 11,
                 ColorAlpha(ev->col, alpha));
    }
    pthread_mutex_unlock(&lot->event_mutex);
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_stats  – right panel showing revenue and vehicles served
// ─────────────────────────────────────────────────────────────────────────────
void draw_stats(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {STATS_X, STATS_Y, STATS_W, STATS_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    int x = STATS_X + 16, y = STATS_Y + 16;

    DrawText("LIVE STATISTICS", x, y, 14, COL_WHITE); y += 28;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    DrawText("TOTAL REVENUE", x, y, 11, COL_MUTED); y += 18;
    char rev[32];
    snprintf(rev, sizeof(rev), "PKR %.2f", lot->total_revenue);
    DrawText(rev, x, y, 22, COL_GREEN); y += 32;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    DrawText("SERVED", x, y, 11, COL_MUTED);
    char sc[8];
    snprintf(sc, sizeof(sc), "%d", lot->vehicles_served);
    DrawText(sc, x + 120, y, 16, COL_GREEN);
}
