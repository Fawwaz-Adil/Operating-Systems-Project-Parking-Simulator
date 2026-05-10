#include "parking.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>


Color priority_color(int p)
{
   
    if (p >= 7) return COL_PRIO_HI;
    if (p >= 4) return COL_PRIO_MID;
    return COL_PRIO_LO;
}

Color lerp_color(Color a, Color b, float t)
{
    return CLITERAL(Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255
    };
}


bool gui_button(Rectangle r, const char *text, Color base_col)
{
    Vector2 mouse   = GetMousePosition();
    bool    hovered = CheckCollisionPointRec(mouse, r);
    bool    clicked = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    Color draw_col = hovered ? ColorAlpha(base_col, 0.75f) : base_col;
    DrawRectangleRounded(r, 0.22f, 6, draw_col);
    DrawRectangleRoundedLines(r, 0.22f, 6, ColorAlpha(COL_WHITE, 0.5f));

    int tw = MeasureText(text, 10);
    DrawText(text, (int)(r.x + r.width / 2 - tw / 2),
             (int)(r.y + r.height / 2 - 5), 10, COL_WHITE);
    return clicked;
}


void draw_panel(Rectangle r, Color bg, Color border)
{
    DrawRectangleRounded(r, 0.04f, 6, bg);
    DrawRectangleRoundedLines(r, 0.04f, 6, border);
}


void draw_vehicle(int sx, int sy, int sw, int sh, float anim,
                  int vid, VehicleType type, int priority)
{
    (void)vid;
    int cx = sx + sw / 2;
    int cy = (int)(sy + sh / 2 + (sh * 0.1f) * (1.0f - anim) - 5);

    
    Color body = priority_color(priority);

    if (type == TYPE_BIKE) {
        DrawRectangleRounded((Rectangle){cx - 8, cy - 15, 16, 30}, 0.5f, 6, body);
        DrawRectangle(cx - 3, cy - 20, 6, 8,  COL_TYRE);
        DrawRectangle(cx - 3, cy + 12, 6, 8,  COL_TYRE);
    }
    else if (type == TYPE_CAR) {
        int cw = sw - 16; if (cw > 60) cw = 60;
        int ch = sh - 20; if (ch > 90) ch = 90;
        int bx = cx - cw / 2, by = cy - ch / 2;
        DrawRectangleRounded((Rectangle){bx, by, cw, ch}, 0.35f, 6, body);
        DrawRectangleRounded((Rectangle){bx+8, by+6, cw-16, ch/4}, 0.3f, 4, COL_WINDOW);
        DrawRectangle(bx - 2,      by + 6,       4, 10, COL_TYRE);
        DrawRectangle(bx + cw - 2, by + 6,       4, 10, COL_TYRE);
        DrawRectangle(bx - 2,      by + ch - 16, 4, 10, COL_TYRE);
        DrawRectangle(bx + cw - 2, by + ch - 16, 4, 10, COL_TYRE);
    }
    else {
        int tw = sw - 20; if (tw > 120) tw = 120;
        int th = sh - 25; if (th > 80)  th = 80;
        int bx = cx - tw / 2, by = cy - th / 2;
        DrawRectangle(bx, by, tw, th, body);
        DrawRectangle(bx + 4, by + 4, tw - 8, 14, COL_WINDOW);
        DrawRectangle(bx - 3,      by + 8,        6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + 8,        6, 14, COL_TYRE);
        DrawRectangle(bx - 3,      by + th - 20,  6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + th - 20,  6, 14, COL_TYRE);
        DrawRectangle(bx - 3,      by + th/2 - 7, 6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + th/2 - 7, 6, 14, COL_TYRE);
    }
}


void draw_slot(int sx, int sy, int sw, int sh, ParkingSlot *s)
{
    Color bg, bd;
    if (s->state == SLOT_OCCUPIED) {
        bg = COL_OCC;
        bd = priority_color(s->priority);
    } else {
        bg = COL_FREE;
        bd = COL_FREE_BD;
    }

    DrawRectangle(sx, sy, sw, sh, bg);
    DrawRectangleLines(sx, sy, sw, sh, bd);

    
    char num[8];
    snprintf(num, sizeof(num), "%02d", s->slot_id);
    DrawText(num, sx + 5, sy + 4, 10, ColorAlpha(COL_WHITE, 0.35f));

    if (s->state == SLOT_OCCUPIED || s->anim > 0.0f) {
        draw_vehicle(sx, sy, sw, sh, s->anim,
                     s->vehicle_id, s->allowed_type, s->priority);

       
        if (s->state == SLOT_OCCUPIED && s->anim >= 0.95f) {
           
            const char *vn = s->vehicle_num[0] ? s->vehicle_num : "???";
            int tw = MeasureText(vn, 9);
            DrawText(vn, sx + sw/2 - tw/2, sy + sh - 28, 9, COL_WHITE);

           
            char pb[6];
            snprintf(pb, sizeof(pb), "P%d", s->priority);
            Color pc = priority_color(s->priority);
            int pw = MeasureText(pb, 10);
            DrawRectangle(sx + sw/2 - pw/2 - 3, sy + sh - 17,
                          pw + 6, 14, ColorAlpha(pc, 0.35f));
            DrawText(pb, sx + sw/2 - pw/2, sy + sh - 16, 10, pc);

           
            time_t parked = time(NULL) - s->entry_time;
            char t_str[16];
            snprintf(t_str, sizeof(t_str), "%lds", (long)parked);
            DrawText(t_str, sx + 4, sy + sh - 15, 9, COL_MUTED);
        }
    }
}


void draw_sidebar(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle panel = {SIDEBAR_X, SIDEBAR_Y, SIDEBAR_W, SIDEBAR_H};
    draw_panel(panel, COL_PANEL, COL_BORDER);

    int x = SIDEBAR_X + 14;
    int y = SIDEBAR_Y + 14;

   
    DrawText("SMART PARKING", x, y, 16, COL_ACCENT); y += 26;
    DrawText("Priority Queue System", x, y, 10, COL_MUTED); y += 22;
    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 14, y, COL_BORDER); y += 12;


    DrawText("1. SELECT VEHICLE TYPE", x, y, 11, COL_WHITE); y += 18;

    bool b_active = (lot->ui_state == UI_INPUT_BIKE);
    bool c_active = (lot->ui_state == UI_INPUT_CAR);
    bool h_active = (lot->ui_state == UI_INPUT_HEAVY);

    Color b_col = b_active ? COL_GREEN  : ColorAlpha(COL_GREEN,  0.4f);
    Color c_col = c_active ? COL_BLUE   : ColorAlpha(COL_BLUE,   0.4f);
    Color h_col = h_active ? COL_AMBER  : ColorAlpha(COL_AMBER,  0.4f);

    if (gui_button((Rectangle){x,       y, 78, 28}, "BIKE",  b_col)) {
        lot->ui_state        = UI_INPUT_BIKE;
        lot->form.num_active = true;
    }
    if (gui_button((Rectangle){x + 86,  y, 78, 28}, "CAR",   c_col)) {
        lot->ui_state        = UI_INPUT_CAR;
        lot->form.num_active = true;
    }
    if (gui_button((Rectangle){x + 172, y, 78, 28}, "HEAVY", h_col)) {
        lot->ui_state        = UI_INPUT_HEAVY;
        lot->form.num_active = true;
    }
    y += 38;

    
    bool form_open = (lot->ui_state == UI_INPUT_BIKE  ||
                      lot->ui_state == UI_INPUT_CAR   ||
                      lot->ui_state == UI_INPUT_HEAVY);

    if (form_open) {
        DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 14, y, COL_BORDER); y += 10;
        DrawText("2. VEHICLE NUMBER", x, y, 11, COL_WHITE); y += 16;

    
        Rectangle tb = {x, y, SIDEBAR_W - 28, 26};
        Color tb_bg  = lot->form.num_active ? COL_INPUT_ACTIVE : COL_INPUT_BG;
        DrawRectangleRounded(tb, 0.2f, 4, tb_bg);
        DrawRectangleRoundedLines(tb, 0.2f, 4,
            lot->form.num_active ? COL_ACCENT : COL_BORDER);

        
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 m = GetMousePosition();
            lot->form.num_active = CheckCollisionPointRec(m, tb);
        }

        const char *disp = lot->form.num_len > 0 ? lot->form.vehicle_num : "e.g. ABC-123";
        Color tc = lot->form.num_len > 0 ? COL_WHITE : COL_MUTED;
        DrawText(disp, tb.x + 8, tb.y + 7, 11, tc);

      
        if (lot->form.num_active) {
            int cw = MeasureText(lot->form.vehicle_num, 11);
            if ((int)(GetTime() * 2) % 2 == 0)
                DrawText("|", tb.x + 9 + cw, tb.y + 7, 11, COL_ACCENT);
        }
        y += 34;

        
        DrawText("3. PRIORITY  (1=Low  9=High)", x, y, 11, COL_WHITE); y += 16;

        int bw = 26, bh = 26, gap = 4;
        for (int p = 1; p <= 9; p++) {
            int bx = x + (p - 1) * (bw + gap);
            bool selected = (lot->form.priority == p);
            Color pc = priority_color(p);
            Color bc = selected ? pc : ColorAlpha(pc, 0.3f);
            Rectangle br = {bx, y, bw, bh};
            DrawRectangleRounded(br, 0.25f, 4, bc);
            DrawRectangleRoundedLines(br, 0.25f, 4, selected ? pc : COL_BORDER);
            char ps[3]; snprintf(ps, sizeof(ps), "%d", p);
            int tw = MeasureText(ps, 11);
            DrawText(ps, bx + bw/2 - tw/2, y + 7, 11,
                     selected ? COL_BG : COL_WHITE);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 m = GetMousePosition();
                if (CheckCollisionPointRec(m, br)) {
                    lot->form.priority = p;
                    lot->form.num_active = false;
                }
            }
        }
        y += 34;

        bool can_submit = (lot->form.num_len > 0 && lot->form.priority > 0);
        Color sc = can_submit ? COL_ACCENT : ColorAlpha(COL_ACCENT, 0.3f);

        if (gui_button((Rectangle){x, y, 126, 30}, "SUBMIT", sc) && can_submit) {
            VehicleType vt = (lot->ui_state == UI_INPUT_BIKE)  ? TYPE_BIKE
                           : (lot->ui_state == UI_INPUT_CAR)   ? TYPE_CAR : TYPE_HEAVY;
            spawn_vehicle(lot, vt, lot->form.vehicle_num, lot->form.priority);
            
            memset(&lot->form, 0, sizeof(lot->form));
            lot->ui_state = UI_IDLE;
        }
        if (gui_button((Rectangle){x + 134, y, 96, 30}, "CANCEL", COL_MUTED)) {
            memset(&lot->form, 0, sizeof(lot->form));
            lot->ui_state = UI_IDLE;
        }
        y += 40;

        
        if (!can_submit) {
            const char *hint = lot->form.num_len == 0
                             ? "Enter vehicle number"
                             : "Choose a priority (1-9)";
            DrawText(hint, x, y, 10, COL_MUTED);
            y += 18;
        }
    } else {
        y += 6;
    }

    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 14, y, COL_BORDER); y += 12;

    
    DrawText("REMOVE VEHICLE", x, y, 11, COL_WHITE); y += 16;
    bool removing = (lot->ui_state == UI_REMOVE);
    if (gui_button((Rectangle){x, y, 230, 28},
                   removing ? "Click an OCCUPIED slot" : "REMOVE VEHICLE",
                   removing ? COL_RED : ColorAlpha(COL_RED, 0.6f)))
    {
        if (!removing) lot->ui_state = UI_REMOVE;
    }
    y += 38;

    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 14, y, COL_BORDER); y += 12;

    
    DrawText("ENTRY GATE", x, y, 11, COL_MUTED);
    float ep     = lot->entry_pulse;
    Color eg_col = ep > 0 ? lerp_color(COL_AMBER, COL_GREEN, 1.0f - ep) : COL_GREEN;
    DrawCircle(x + 200, y + 6, 7, ColorAlpha(eg_col, 0.3f));
    DrawCircleLines(x + 200, y + 6, 7, eg_col);
    y += 22;

    DrawText("EXIT GATE", x, y, 11, COL_MUTED);
    float xp     = lot->exit_pulse;
    Color xg_col = xp > 0 ? lerp_color(COL_AMBER, COL_GREEN, 1.0f - xp) : COL_GREEN;
    DrawCircle(x + 200, y + 6, 7, ColorAlpha(xg_col, 0.3f));
    DrawCircleLines(x + 200, y + 6, 7, xg_col);
    y += 28;

    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 14, y, COL_BORDER); y += 12;

   
    DrawText("PRIORITY WAIT QUEUE", x, y, 11, COL_WHITE); y += 16;

    pthread_mutex_lock(&lot->wait_mutex);

  
    char wbuf[48];
    snprintf(wbuf, sizeof(wbuf), "Bikes  : %d waiting", lot->wait_bike_count);
    DrawText(wbuf, x, y, 10,
             lot->wait_bike_count > 0 ? COL_GREEN : COL_MUTED);
    y += 15;

    snprintf(wbuf, sizeof(wbuf), "Cars   : %d waiting", lot->wait_car_count);
    DrawText(wbuf, x, y, 10,
             lot->wait_car_count > 0 ? COL_BLUE : COL_MUTED);
    y += 15;

    snprintf(wbuf, sizeof(wbuf), "Heavy  : %d waiting", lot->wait_heavy_count);
    DrawText(wbuf, x, y, 10,
             lot->wait_heavy_count > 0 ? COL_AMBER : COL_MUTED);
    y += 18;

  
    typedef struct { WaitEntry e; const char *zone_col_name; Color col; } ShowEntry;
    ShowEntry show[15];
    int sc2 = 0;

    
    for (int i = lot->wait_bike_count - 1; i >= 0 && sc2 < 5; i--)
        show[sc2++] = (ShowEntry){ lot->wait_bike[i], "Bike", COL_GREEN };
    for (int i = lot->wait_car_count - 1; i >= 0 && sc2 < 10; i--)
        show[sc2++] = (ShowEntry){ lot->wait_car[i], "Car", COL_BLUE };
    for (int i = lot->wait_heavy_count - 1; i >= 0 && sc2 < 15; i--)
        show[sc2++] = (ShowEntry){ lot->wait_heavy[i], "Heavy", COL_AMBER };

    pthread_mutex_unlock(&lot->wait_mutex);

    if (sc2 > 0) {
        DrawText("Next up:", x, y, 10, COL_MUTED); y += 14;
        int show_max = sc2 < 6 ? sc2 : 6;
        for (int i = 0; i < show_max; i++) {
            char line[48];
            snprintf(line, sizeof(line), "[P%d] %s %s",
                     show[i].e.priority,
                     show[i].zone_col_name,
                     show[i].e.vehicle_num);
            Color lc = priority_color(show[i].e.priority);
            DrawText(line, x + 6, y, 10, lc);
            y += 13;
        }
    }
}

void draw_grid(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {GRID_X, GRID_Y, GRID_W, GRID_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    pthread_mutex_lock(&lot->slots_mutex);
    int pad_x = 18;

    
    DrawText("BIKE PARKING  [Slots 01-04]",
             GRID_X + pad_x, GRID_Y + 14, 12, COL_GREEN);
    int b_y = GRID_Y + 34;
    int b_w = (GRID_W - 2 * pad_x - 3 * 10) / 4;
    int b_h = 100;
    for (int i = 0; i < NUM_BIKE_SLOTS; i++) {
        int bx = GRID_X + pad_x + i * (b_w + 10);
        lot->slots[i].bounds = (Rectangle){bx, b_y, b_w, b_h};
        draw_slot(bx, b_y, b_w, b_h, &lot->slots[i]);
    }

    
    DrawText("CAR PARKING  [Slots 05-10]",
             GRID_X + pad_x, GRID_Y + 152, 12, COL_BLUE);
    int c_y = GRID_Y + 170;
    int c_w = (GRID_W - 2 * pad_x - 5 * 10) / 6;
    int c_h = 110;
    for (int i = NUM_BIKE_SLOTS; i < NUM_BIKE_SLOTS + NUM_CAR_SLOTS; i++) {
        int idx = i - NUM_BIKE_SLOTS;
        int cx  = GRID_X + pad_x + idx * (c_w + 10);
        lot->slots[i].bounds = (Rectangle){cx, c_y, c_w, c_h};
        draw_slot(cx, c_y, c_w, c_h, &lot->slots[i]);
    }

    
    DrawText("HEAVY VEHICLE PARKING  [Slots 11-12]",
             GRID_X + pad_x, GRID_Y + 300, 12, COL_AMBER);
    int h_y = GRID_Y + 318;
    int h_w = (GRID_W - 2 * pad_x - 1 * 10) / 2;
    int h_h = 120;
    for (int i = NUM_BIKE_SLOTS + NUM_CAR_SLOTS; i < TOTAL_SLOTS; i++) {
        int idx = i - (NUM_BIKE_SLOTS + NUM_CAR_SLOTS);
        int hx  = GRID_X + pad_x + idx * (h_w + 10);
        lot->slots[i].bounds = (Rectangle){hx, h_y, h_w, h_h};
        draw_slot(hx, h_y, h_w, h_h, &lot->slots[i]);
    }

    pthread_mutex_unlock(&lot->slots_mutex);

    
    int lx = GRID_X + GRID_W - 130, ly = GRID_Y + 10;
    DrawText("Priority:", lx, ly, 10, COL_MUTED);
    DrawRectangle(lx + 56, ly + 1, 12, 10, COL_PRIO_LO);
    DrawText("1-3", lx + 71, ly, 10, COL_PRIO_LO);
    DrawRectangle(lx + 56, ly + 14, 12, 10, COL_PRIO_MID);
    DrawText("4-6", lx + 71, ly + 14, 10, COL_PRIO_MID);
    DrawRectangle(lx + 56, ly + 28, 12, 10, COL_PRIO_HI);
    DrawText("7-9", lx + 71, ly + 28, 10, COL_PRIO_HI);
}


void draw_log_panel(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {LOG_X, LOG_Y, LOG_W, LOG_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    DrawText("EVENT LOG", LOG_X + 16, LOG_Y + 10, 13, COL_WHITE);
    DrawLine(LOG_X + 16, LOG_Y + 28, LOG_X + LOG_W - 16, LOG_Y + 28, COL_BORDER);

    pthread_mutex_lock(&lot->event_mutex);
    int line_h    = 16;
    int max_lines = (LOG_H - 44) / line_h;
    if (max_lines > MAX_LOG_DISPLAY) max_lines = MAX_LOG_DISPLAY;
    int count = lot->event_count < max_lines ? lot->event_count : max_lines;

    for (int i = 0; i < count; i++) {
        int      idx = (lot->event_tail - 1 - i + MAX_EVENTS) % MAX_EVENTS;
        GUIEvent *ev = &lot->events[idx];
        float alpha  = 1.0f - (ev->age / 40.0f);
        if (alpha < 0.15f) alpha = 0.15f;
        DrawText(ev->text,
                 LOG_X + 12,
                 LOG_Y + LOG_H - 12 - i * line_h,
                 10, ColorAlpha(ev->col, alpha));
    }
    pthread_mutex_unlock(&lot->event_mutex);
}


void draw_stats(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {STATS_X, STATS_Y, STATS_W, STATS_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    int x = STATS_X + 16, y = STATS_Y + 14;

    DrawText("LIVE STATISTICS", x, y, 14, COL_WHITE); y += 28;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    /* Revenue */
    DrawText("TOTAL REVENUE", x, y, 11, COL_MUTED); y += 18;
    char rev[40];
    snprintf(rev, sizeof(rev), "PKR %.2f", lot->total_revenue);
    DrawText(rev, x, y, 22, COL_GREEN); y += 34;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    /* Vehicles served */
    DrawText("VEHICLES SERVED", x, y, 11, COL_MUTED); y += 18;
    char sc[12]; snprintf(sc, sizeof(sc), "%d", lot->vehicles_served);
    DrawText(sc, x, y, 22, COL_ACCENT); y += 34;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    /* Occupancy */
    DrawText("OCCUPANCY", x, y, 11, COL_MUTED); y += 18;
    char oc[24];
    snprintf(oc, sizeof(oc), "%d / %d", lot->occupied_count, TOTAL_SLOTS);
    DrawText(oc, x, y, 22, COL_AMBER); y += 34;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    /* Per-zone availability */
    DrawText("SLOT AVAILABILITY", x, y, 11, COL_MUTED); y += 18;

    auto_draw_zone_bar:;  /* label trick not needed; just inline */

    
    {
        char zl[32];
        snprintf(zl, sizeof(zl), "Bikes  %d/%d", lot->free_bikes, NUM_BIKE_SLOTS);
        DrawText(zl, x, y, 11, COL_GREEN);
        
        float frac = (float)lot->free_bikes / NUM_BIKE_SLOTS;
        DrawRectangle(x + 120, y + 2, 80, 10, ColorAlpha(COL_GREEN, 0.15f));
        DrawRectangle(x + 120, y + 2, (int)(80 * frac), 10, COL_GREEN);
        y += 18;
    }
    {
        char zl[32];
        snprintf(zl, sizeof(zl), "Cars   %d/%d", lot->free_cars, NUM_CAR_SLOTS);
        DrawText(zl, x, y, 11, COL_BLUE);
        float frac = (float)lot->free_cars / NUM_CAR_SLOTS;
        DrawRectangle(x + 120, y + 2, 80, 10, ColorAlpha(COL_BLUE, 0.15f));
        DrawRectangle(x + 120, y + 2, (int)(80 * frac), 10, COL_BLUE);
        y += 18;
    }
    {
        char zl[32];
        snprintf(zl, sizeof(zl), "Heavy  %d/%d", lot->free_heavy, NUM_HEAVY_SLOTS);
        DrawText(zl, x, y, 11, COL_AMBER);
        float frac = (float)lot->free_heavy / NUM_HEAVY_SLOTS;
        DrawRectangle(x + 120, y + 2, 80, 10, ColorAlpha(COL_AMBER, 0.15f));
        DrawRectangle(x + 120, y + 2, (int)(80 * frac), 10, COL_AMBER);
        y += 24;
    }

    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    DrawText("CURRENTLY PARKED", x, y, 11, COL_MUTED); y += 16;

    pthread_mutex_lock(&lot->slots_mutex);
    int listed = 0;
    for (int i = 0; i < TOTAL_SLOTS && listed < 12; i++) {
        ParkingSlot *s = &lot->slots[i];
        if (s->state == SLOT_OCCUPIED) {
            const char *zt = (s->allowed_type == TYPE_BIKE)  ? "Bike"
                           : (s->allowed_type == TYPE_CAR)   ? "Car " : "Hvy ";
            char line[48];
            snprintf(line, sizeof(line), "S%02d  [P%d] %s  %s",
                     s->slot_id, s->priority, zt,
                     s->vehicle_num[0] ? s->vehicle_num : "---");
            Color lc = priority_color(s->priority);
            DrawText(line, x, y, 10, lc);
            y += 14;
            listed++;
        }
    }
    pthread_mutex_unlock(&lot->slots_mutex);

    if (listed == 0)
        DrawText("(no vehicles parked)", x, y, 10, COL_MUTED);
}