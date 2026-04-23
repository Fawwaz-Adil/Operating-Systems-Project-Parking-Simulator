#include "raylib.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

#define NUM_BIKE_SLOTS      4
#define NUM_CAR_SLOTS       6
#define NUM_HEAVY_SLOTS     2
#define TOTAL_SLOTS         (NUM_BIKE_SLOTS + NUM_CAR_SLOTS + NUM_HEAVY_SLOTS)

#define LOG_QUEUE_SIZE      128
#define GATE_OPEN_DELAY_US  300000
#define PARKING_FEE_PER_SEC 2.50
#define MAX_LOG_DISPLAY     18   
#define MAX_EVENTS          256  

#define WIN_W   1100
#define WIN_H   720
#define FPS     60

// UI Constants
#define SIDEBAR_X    10
#define SIDEBAR_Y    10
#define SIDEBAR_W    260
#define SIDEBAR_H    700
#define GRID_X       280
#define GRID_Y       10
#define GRID_W       540
#define GRID_H       460
#define LOG_X        280
#define LOG_Y        480
#define LOG_W        540
#define LOG_H        230
#define STATS_X      830
#define STATS_Y      10
#define STATS_W      260
#define STATS_H      700

// Colors
#define COL_BG          CLITERAL(Color){ 15,  17,  26, 255}
#define COL_PANEL       CLITERAL(Color){ 24,  27,  40, 255}
#define COL_BORDER      CLITERAL(Color){ 55,  62,  90, 255}
#define COL_ACCENT      CLITERAL(Color){ 82, 130, 255, 255}
#define COL_FREE        CLITERAL(Color){ 34,  48,  34, 255}
#define COL_FREE_BD     CLITERAL(Color){ 58, 120,  58, 255}
#define COL_OCC         CLITERAL(Color){ 55,  28,  28, 255}
#define COL_OCC_BD      CLITERAL(Color){210,  70,  70, 255}
#define COL_WHITE       CLITERAL(Color){220, 225, 240, 255}
#define COL_MUTED       CLITERAL(Color){110, 118, 150, 255}
#define COL_GREEN       CLITERAL(Color){ 72, 200,  90, 255}
#define COL_RED         CLITERAL(Color){220,  72,  72, 255}
#define COL_AMBER       CLITERAL(Color){240, 170,  50, 255}
#define COL_BLUE        CLITERAL(Color){ 82, 130, 255, 255}
#define COL_WINDOW      CLITERAL(Color){180, 210, 255, 160}
#define COL_TYRE        CLITERAL(Color){ 30,  30,  30, 255}

typedef enum { SLOT_FREE = 0, SLOT_RESERVED, SLOT_OCCUPIED } SlotState;
typedef enum { TYPE_BIKE = 0, TYPE_CAR, TYPE_HEAVY } VehicleType;

// GUI Interaction States
typedef enum { UI_IDLE = 0, UI_ADD_BIKE, UI_ADD_CAR, UI_ADD_HEAVY, UI_REMOVE } UIActionState;

typedef struct {
    int             slot_id;
    VehicleType     allowed_type;
    SlotState       state;
    int             vehicle_id;
    time_t          entry_time;
    float           anim;
    int             animating_in;
    int             animating_out;
    
    Rectangle       bounds;           // Bounding box for mouse clicks
    pthread_mutex_t slot_mutex;       // OS: Per-slot lock
    pthread_cond_t  remove_cond;      // OS: Cond var to wait for user removal
    int             force_remove;     // Signal flag
} ParkingSlot;

typedef struct { char text[120]; Color col; float age; } GUIEvent;
typedef struct { char message[256]; } LogEntry;

typedef struct {
    ParkingSlot        slots[TOTAL_SLOTS];
    int                occupied_count;
    pthread_mutex_t    slots_mutex;
    
    sem_t              sem_bikes;
    sem_t              sem_cars;
    sem_t              sem_heavy;
    int                free_bikes; 
    int                free_cars;
    int                free_heavy;

    int                entry_gate_open;
    pthread_mutex_t    entry_mutex;
    pthread_cond_t     entry_cond;
    int                exit_gate_open;
    pthread_mutex_t    exit_mutex;
    pthread_cond_t     exit_cond;

    double             total_revenue;
    pthread_mutex_t    revenue_mutex;

    LogEntry           log_queue[LOG_QUEUE_SIZE];
    int                log_head, log_tail, log_count;
    int                log_shutdown;
    pthread_mutex_t    log_mutex;
    pthread_cond_t     log_not_empty;
    pthread_cond_t     log_not_full;

    int                vehicles_served;
    int                global_vehicle_id;
    pthread_mutex_t    stats_mutex;

    GUIEvent           events[MAX_EVENTS];
    int                event_head, event_tail, event_count;
    pthread_mutex_t    event_mutex;

    float              entry_pulse;
    float              exit_pulse;
    
    UIActionState      ui_state;      // Active UI action mode
} ParkingLot;

typedef struct { 
    int vehicle_id;
    VehicleType type;
    ParkingLot *lot; 
    int target_slot;  // Passed from user click
} VehicleArg;

static void  lot_init(ParkingLot *lot);
static void  lot_destroy(ParkingLot *lot);
static void  push_event(ParkingLot *lot, Color col, const char *fmt, ...);
static void  enqueue_log(ParkingLot *lot, const char *fmt, ...);
static void *vehicle_thread(void *arg);
static void *logger_thread(void *arg);
static void  wait_for_gate(pthread_mutex_t *m, pthread_cond_t *c, int *flag);
static const char *ts(void);
static void draw_panel(Rectangle r, Color bg, Color border);
static void draw_slot(int sx, int sy, int sw, int sh, ParkingSlot *s);
static void draw_vehicle(int sx, int sy, int sw, int sh, float anim, int vid, VehicleType type);
static void draw_sidebar(ParkingLot *lot, Font font);
static void draw_grid(ParkingLot *lot, Font font);
static void draw_log_panel(ParkingLot *lot, Font font);
static void draw_stats(ParkingLot *lot, Font font);
static Color lerp_color(Color a, Color b, float t);
static bool gui_button(Rectangle r, const char *text, Color base_col);
static void spawn_vehicle(ParkingLot *lot, VehicleType type, int slot_idx);

int main(void)
{
    ParkingLot *lot = calloc(1, sizeof(ParkingLot));
    if (!lot) return 1;
    lot_init(lot);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WIN_W, WIN_H, "Manual Assignment Smart Parking");
    SetTargetFPS(FPS);
    Font font = GetFontDefault();

    pthread_t logger_tid;
    pthread_create(&logger_tid, NULL, logger_thread, lot);

    push_event(lot, COL_ACCENT, "=== System Ready. Select an action from sidebar ===");

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // 1. Process User Clicks on the Grid
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && lot->ui_state != UI_IDLE) {
            Vector2 mouse = GetMousePosition();
            pthread_mutex_lock(&lot->slots_mutex);
            
            for (int i = 0; i < TOTAL_SLOTS; i++) {
                if (CheckCollisionPointRec(mouse, lot->slots[i].bounds)) {
                    // Action: Add Vehicle
                    if (lot->ui_state >= UI_ADD_BIKE && lot->ui_state <= UI_ADD_HEAVY) {
                        VehicleType req_type = (VehicleType)(lot->ui_state - UI_ADD_BIKE);
                        
                        if (lot->slots[i].state == SLOT_FREE && lot->slots[i].allowed_type == req_type) {
                            lot->slots[i].state = SLOT_RESERVED; // Lock it immediately
                            spawn_vehicle(lot, req_type, i);
                            lot->ui_state = UI_IDLE;
                        }
                    } 
                    // Action: Remove Vehicle
                    else if (lot->ui_state == UI_REMOVE) {
                        if (lot->slots[i].state == SLOT_OCCUPIED) {
                            pthread_mutex_lock(&lot->slots[i].slot_mutex);
                            lot->slots[i].force_remove = 1;
                            pthread_cond_signal(&lot->slots[i].remove_cond); // WAKE UP THREAD
                            pthread_mutex_unlock(&lot->slots[i].slot_mutex);
                            lot->ui_state = UI_IDLE;
                        }
                    }
                }
            }
            pthread_mutex_unlock(&lot->slots_mutex);
        }

        // Animate slots
        pthread_mutex_lock(&lot->slots_mutex);
        for (int i = 0; i < TOTAL_SLOTS; i++) {
            ParkingSlot *s = &lot->slots[i];
            if (s->animating_in) {
                s->anim += dt * 1.8f;
                if (s->anim >= 1.0f) { s->anim = 1.0f; s->animating_in = 0; }
            }
            if (s->animating_out) {
                s->anim -= dt * 1.8f;
                if (s->anim <= 0.0f) { s->anim = 0.0f; s->animating_out = 0; }
            }
        }
        pthread_mutex_unlock(&lot->slots_mutex);

        if (lot->entry_pulse > 0) lot->entry_pulse -= dt * 2.0f;
        if (lot->exit_pulse  > 0) lot->exit_pulse  -= dt * 2.0f;
        if (lot->entry_pulse < 0) lot->entry_pulse = 0;
        if (lot->exit_pulse  < 0) lot->exit_pulse  = 0;

        pthread_mutex_lock(&lot->event_mutex);
        for (int i = 0; i < MAX_EVENTS; i++) lot->events[i].age += dt;
        pthread_mutex_unlock(&lot->event_mutex);

        BeginDrawing();
        ClearBackground(COL_BG);

        draw_sidebar(lot, font);
        draw_grid(lot, font);
        draw_log_panel(lot, font);
        draw_stats(lot, font);

        DrawRectangle(0, 0, WIN_W, 8, COL_ACCENT);

        EndDrawing();
    }

    pthread_mutex_lock(&lot->log_mutex);
    lot->log_shutdown = 1;
    pthread_cond_signal(&lot->log_not_empty);
    pthread_mutex_unlock(&lot->log_mutex);

    pthread_join(logger_tid, NULL);
    CloseWindow();
    lot_destroy(lot);
    free(lot);
    return 0;
}

static void spawn_vehicle(ParkingLot *lot, VehicleType type, int slot_idx)
{
    pthread_mutex_lock(&lot->stats_mutex);
    lot->global_vehicle_id++;
    int new_id = lot->global_vehicle_id;
    pthread_mutex_unlock(&lot->stats_mutex);

    VehicleArg *arg = malloc(sizeof(VehicleArg));
    arg->lot = lot;
    arg->type = type;
    arg->vehicle_id = new_id;
    arg->target_slot = slot_idx;

    pthread_t tid;
    pthread_create(&tid, NULL, vehicle_thread, arg);
}

static void *vehicle_thread(void *arg)
{
    VehicleArg *va = (VehicleArg *)arg;
    ParkingLot *lot = va->lot;
    int vid = va->vehicle_id;
    VehicleType type = va->type;
    int idx = va->target_slot;
    free(va);
    pthread_detach(pthread_self()); 

    const char* t_str = (type == TYPE_BIKE) ? "Bike" : (type == TYPE_CAR) ? "Car" : "Heavy";
    sem_t *target_sem = (type == TYPE_BIKE) ? &lot->sem_bikes : (type == TYPE_CAR) ? &lot->sem_cars : &lot->sem_heavy;
    int *target_free_ui = (type == TYPE_BIKE) ? &lot->free_bikes : (type == TYPE_CAR) ? &lot->free_cars : &lot->free_heavy;

    // Wait on semaphore to decrement count
    sem_wait(target_sem);
    __sync_fetch_and_sub(target_free_ui, 1);

    push_event(lot, COL_MUTED, "%s V%02d arriving at gate...", t_str, vid);

    wait_for_gate(&lot->entry_mutex, &lot->entry_cond, &lot->entry_gate_open);
    lot->entry_pulse = 1.0f;
    usleep(GATE_OPEN_DELAY_US);

    // Enter Slot
    pthread_mutex_lock(&lot->slots_mutex);
    lot->slots[idx].state        = SLOT_OCCUPIED;
    lot->slots[idx].vehicle_id   = vid;
    lot->slots[idx].entry_time   = time(NULL);
    lot->slots[idx].animating_in  = 1;
    lot->slots[idx].animating_out = 0;
    lot->slots[idx].anim          = 0.0f;
    lot->slots[idx].force_remove  = 0; // Reset remove flag
    lot->occupied_count++;
    int sid = lot->slots[idx].slot_id;
    pthread_mutex_unlock(&lot->slots_mutex);
    
    push_event(lot, COL_GREEN, "%s V%02d ASSIGNED  → Slot %02d", t_str, vid, sid);
    enqueue_log(lot, "[%s] %s V%02d ENTERED slot %02d", ts(), t_str, vid, sid);

    // -------------------------------------------------------------
    // NEW: Infinite wait using Condition Variable. Only wakes on removal!
    // -------------------------------------------------------------
    pthread_mutex_lock(&lot->slots[idx].slot_mutex);
    while (lot->slots[idx].force_remove == 0) {
        pthread_cond_wait(&lot->slots[idx].remove_cond, &lot->slots[idx].slot_mutex);
    }
    pthread_mutex_unlock(&lot->slots[idx].slot_mutex);
    // -------------------------------------------------------------

    wait_for_gate(&lot->exit_mutex, &lot->exit_cond, &lot->exit_gate_open);
    lot->exit_pulse = 1.0f;
    usleep(GATE_OPEN_DELAY_US);

    // Leave Slot
    pthread_mutex_lock(&lot->slots_mutex);
    time_t duration = time(NULL) - lot->slots[idx].entry_time;
    lot->slots[idx].state         = SLOT_FREE;
    lot->slots[idx].vehicle_id    = -1;
    lot->slots[idx].entry_time    = 0;
    lot->slots[idx].animating_out = 1;
    lot->slots[idx].animating_in  = 0;
    lot->occupied_count--;
    pthread_mutex_unlock(&lot->slots_mutex);

    double fee = (double)duration * PARKING_FEE_PER_SEC;
    if(type == TYPE_BIKE) fee *= 0.5;
    if(type == TYPE_HEAVY) fee *= 2.0;

    pthread_mutex_lock(&lot->revenue_mutex);
    lot->total_revenue += fee;
    pthread_mutex_unlock(&lot->revenue_mutex);

    pthread_mutex_lock(&lot->stats_mutex);
    lot->vehicles_served++;
    pthread_mutex_unlock(&lot->stats_mutex);

    __sync_fetch_and_add(target_free_ui, 1);
    sem_post(target_sem);
    
    push_event(lot, COL_AMBER, "%s V%02d REMOVED ← Slot %02d (Parked: %lds)", t_str, vid, sid, (long)duration);
    enqueue_log(lot, "[%s] %s V%02d REMOVED from slot %02d fee %.2f", ts(), t_str, vid, sid, fee);

    return NULL;
}

static void *logger_thread(void *arg)
{
    ParkingLot *lot = (ParkingLot *)arg;
    FILE *fp = fopen("parking_log.txt", "w");
    if (!fp) return NULL;
    fprintf(fp, "Interactive Parking Log\n\n");
            
    while (1) {
        pthread_mutex_lock(&lot->log_mutex);
        while (lot->log_count == 0 && !lot->log_shutdown)
            pthread_cond_wait(&lot->log_not_empty, &lot->log_mutex);
            
        if (lot->log_count == 0 && lot->log_shutdown) {
            pthread_mutex_unlock(&lot->log_mutex);
            break;
        }
        LogEntry e = lot->log_queue[lot->log_head];
        lot->log_head = (lot->log_head + 1) % LOG_QUEUE_SIZE;
        lot->log_count--;
        pthread_cond_signal(&lot->log_not_full);
        pthread_mutex_unlock(&lot->log_mutex);
        
        fprintf(fp, "%s\n", e.message);
        fflush(fp);
    }
    fclose(fp);
    return NULL;
}

static bool gui_button(Rectangle r, const char *text, Color base_col)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, r);
    bool clicked = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    Color draw_col = hovered ? ColorAlpha(base_col, 0.8f) : base_col;
    DrawRectangleRounded(r, 0.2f, 6, draw_col);
    DrawRectangleRoundedLines(r, 0.2f, 6, COL_WHITE); // 4 args for your Raylib version

    int tw = MeasureText(text, 10);
    DrawText(text, r.x + r.width/2 - tw/2, r.y + r.height/2 - 5, 10, COL_WHITE);
    return clicked;
}

static void draw_sidebar(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {SIDEBAR_X, SIDEBAR_Y, SIDEBAR_W, SIDEBAR_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    int x = SIDEBAR_X + 16, y = SIDEBAR_Y + 16;
    DrawText("INTERACTIVE OS PARKING", x, y, 16, COL_ACCENT); y += 30;

    // --- GUI BUTTONS ---
    DrawText("1. ADD VEHICLES", x, y, 12, COL_WHITE); y += 20;
    if (gui_button((Rectangle){x, y, 100, 30}, "ADD BIKE", COL_GREEN)) lot->ui_state = UI_ADD_BIKE;
    if (gui_button((Rectangle){x + 110, y, 100, 30}, "ADD CAR", COL_BLUE)) lot->ui_state = UI_ADD_CAR;
    y += 40;
    if (gui_button((Rectangle){x, y, 210, 30}, "ADD HEAVY VEHICLE", COL_AMBER)) lot->ui_state = UI_ADD_HEAVY;
    
    y += 50;
    DrawText("2. REMOVE VEHICLES", x, y, 12, COL_WHITE); y += 20;
    if (gui_button((Rectangle){x, y, 210, 30}, "REMOVE VEHICLE", COL_RED)) lot->ui_state = UI_REMOVE;
    y += 45;
    
    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 16, y, COL_BORDER); y += 15;

    // Dynamic UI State Message
    const char *msg = "IDLE";
    Color msg_col = COL_MUTED;
    if (lot->ui_state == UI_ADD_BIKE) { msg = "Click EMPTY Bike Slot"; msg_col = COL_GREEN; }
    if (lot->ui_state == UI_ADD_CAR) { msg = "Click EMPTY Car Slot"; msg_col = COL_BLUE; }
    if (lot->ui_state == UI_ADD_HEAVY) { msg = "Click EMPTY Heavy Slot"; msg_col = COL_AMBER; }
    if (lot->ui_state == UI_REMOVE) { msg = "Click OCCUPIED Slot to Free"; msg_col = COL_RED; }
    
    DrawText("CURRENT ACTION:", x, y, 10, COL_MUTED); y += 15;
    DrawText(msg, x, y, 12, msg_col); y += 30;
    DrawLine(x, y, SIDEBAR_X + SIDEBAR_W - 16, y, COL_BORDER); y += 15;

    DrawText("ENTRY GATE", x, y, 12, COL_MUTED);
    float ep = lot->entry_pulse;
    Color eg_col = ep > 0 ? lerp_color(COL_AMBER, COL_GREEN, 1.0f - ep) : COL_GREEN;
    DrawCircle(x + 180, y + 6, 7, ColorAlpha(eg_col, 0.3f));
    DrawCircleLines(x + 180, y + 6, 7, eg_col); y += 26;

    DrawText("EXIT GATE", x, y, 12, COL_MUTED);
    float xp = lot->exit_pulse;
    Color xg_col = xp > 0 ? lerp_color(COL_AMBER, COL_GREEN, 1.0f - xp) : COL_GREEN;
    DrawCircle(x + 180, y + 6, 7, ColorAlpha(xg_col, 0.3f));
    DrawCircleLines(x + 180, y + 6, 7, xg_col); y += 28;
}

static void draw_grid(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {GRID_X, GRID_Y, GRID_W, GRID_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    pthread_mutex_lock(&lot->slots_mutex);
    int pad_x = 20;

    // ZONE 1: BIKES
    DrawText("BIKE PARKING", GRID_X + pad_x, GRID_Y + 20, 12, COL_GREEN);
    int b_y = GRID_Y + 40, b_w = (GRID_W - 2 * pad_x - 3 * 10) / 4, b_h = 100;
    for (int i = 0; i < NUM_BIKE_SLOTS; i++) {
        lot->slots[i].bounds = (Rectangle){GRID_X + pad_x + i * (b_w + 10), b_y, b_w, b_h};
        draw_slot(lot->slots[i].bounds.x, b_y, b_w, b_h, &lot->slots[i]);
    }

    // ZONE 2: CARS
    DrawText("CAR PARKING", GRID_X + pad_x, GRID_Y + 160, 12, COL_BLUE);
    int c_y = GRID_Y + 180, c_w = (GRID_W - 2 * pad_x - 5 * 10) / 6, c_h = 110;
    for (int i = NUM_BIKE_SLOTS; i < NUM_BIKE_SLOTS + NUM_CAR_SLOTS; i++) {
        int idx = i - NUM_BIKE_SLOTS;
        lot->slots[i].bounds = (Rectangle){GRID_X + pad_x + idx * (c_w + 10), c_y, c_w, c_h};
        draw_slot(lot->slots[i].bounds.x, c_y, c_w, c_h, &lot->slots[i]);
    }

    // ZONE 3: HEAVY VEHICLES
    DrawText("HEAVY VEHICLE PARKING", GRID_X + pad_x, GRID_Y + 310, 12, COL_AMBER);
    int h_y = GRID_Y + 330, h_w = (GRID_W - 2 * pad_x - 1 * 10) / 2, h_h = 110;
    for (int i = NUM_BIKE_SLOTS + NUM_CAR_SLOTS; i < TOTAL_SLOTS; i++) {
        int idx = i - (NUM_BIKE_SLOTS + NUM_CAR_SLOTS);
        lot->slots[i].bounds = (Rectangle){GRID_X + pad_x + idx * (h_w + 10), h_y, h_w, h_h};
        draw_slot(lot->slots[i].bounds.x, h_y, h_w, h_h, &lot->slots[i]);
    }
    pthread_mutex_unlock(&lot->slots_mutex);
}

static void draw_slot(int sx, int sy, int sw, int sh, ParkingSlot *s)
{
    Color bg  = (s->state == SLOT_OCCUPIED) ? COL_OCC  : COL_FREE;
    Color bd  = (s->state == SLOT_OCCUPIED) ? COL_OCC_BD : COL_FREE_BD;

    DrawRectangle(sx, sy, sw, sh, bg);
    DrawRectangleLines(sx, sy, sw, sh, bd);

    char num[8]; snprintf(num, sizeof(num), "%02d", s->slot_id);
    DrawText(num, sx + 6, sy + 5, 11, ColorAlpha(COL_WHITE, 0.4f));

    if (s->state == SLOT_OCCUPIED || s->anim > 0.0f) {
        draw_vehicle(sx, sy, sw, sh, s->anim, s->vehicle_id, s->allowed_type);
        
        // DISPLAY DAYS (SECONDS) PARKED
        if (s->state == SLOT_OCCUPIED && s->anim == 1.0f) {
            time_t parked = time(NULL) - s->entry_time;
            char t_str[16]; snprintf(t_str, sizeof(t_str), "Days: %ld", (long)parked);
            DrawText(t_str, sx + sw/2 - MeasureText(t_str, 10)/2, sy + sh - 15, 10, COL_AMBER);
        }
    }
}

static void draw_vehicle(int sx, int sy, int sw, int sh, float anim, int vid, VehicleType type)
{
    int cx = sx + sw / 2;
    int cy = (int)(sy + sh / 2 + (sh * 0.1f) * (1.0f - anim) - 5);

    float hue = (float)((vid * 47) % 360);
    Color body = ColorFromHSV(hue, 0.6f, 0.8f);

    if (type == TYPE_BIKE) {
        DrawRectangleRounded((Rectangle){cx - 8, cy - 15, 16, 30}, 0.5f, 6, body);
        DrawRectangle(cx - 3, cy - 20, 6, 8, COL_TYRE);
        DrawRectangle(cx - 3, cy + 12, 6, 8, COL_TYRE);
    } 
    else if (type == TYPE_CAR) {
        int cw = sw - 16; if (cw > 60) cw = 60;
        int ch = sh - 20; if (ch > 90) ch = 90;
        int bx = cx - cw / 2, by = cy - ch / 2;
        
        DrawRectangleRounded((Rectangle){bx, by, cw, ch}, 0.35f, 6, body);
        DrawRectangleRounded((Rectangle){bx + 8, by + 6, cw - 16, ch / 4}, 0.3f, 4, COL_WINDOW);
        DrawRectangle(bx - 2, by + 6, 4, 10, COL_TYRE);
        DrawRectangle(bx + cw - 2, by + 6, 4, 10, COL_TYRE);
        DrawRectangle(bx - 2, by + ch - 16, 4, 10, COL_TYRE);
        DrawRectangle(bx + cw - 2, by + ch - 16, 4, 10, COL_TYRE);
    }
    else {
        int tw = sw - 20; if (tw > 120) tw = 120;
        int th = sh - 25; if (th > 80) th = 80;
        int bx = cx - tw / 2, by = cy - th / 2;

        DrawRectangle(bx, by, tw, th, body);
        DrawRectangle(bx + 4, by + 4, tw - 8, 14, COL_WINDOW); 
        DrawRectangle(bx - 3, by + 8, 6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + 8, 6, 14, COL_TYRE);
        DrawRectangle(bx - 3, by + th - 20, 6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + th - 20, 6, 14, COL_TYRE);
        DrawRectangle(bx - 3, by + th / 2 - 7, 6, 14, COL_TYRE);
        DrawRectangle(bx + tw - 3, by + th / 2 - 7, 6, 14, COL_TYRE);
    }

    char vid_str[6]; snprintf(vid_str, sizeof(vid_str), "V%02d", vid);
    DrawText(vid_str, cx - MeasureText(vid_str, 10) / 2, cy - 5, 10, WHITE);
}

static void draw_log_panel(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {LOG_X, LOG_Y, LOG_W, LOG_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    DrawText("EVENT LOG", LOG_X + 16, LOG_Y + 10, 13, COL_WHITE);
    DrawLine(LOG_X + 16, LOG_Y + 28, LOG_X + LOG_W - 16, LOG_Y + 28, COL_BORDER);

    pthread_mutex_lock(&lot->event_mutex);
    int line_h = 17, max_lines = (LOG_H - 42) / line_h;
    if (max_lines > MAX_LOG_DISPLAY) max_lines = MAX_LOG_DISPLAY;

    int count = lot->event_count;
    if (count > max_lines) count = max_lines;

    for (int i = 0; i < count; i++) {
        int idx = (lot->event_tail - 1 - i + MAX_EVENTS) % MAX_EVENTS;
        GUIEvent *ev = &lot->events[idx];
        float alpha = 1.0f - (ev->age / 30.0f);
        if (alpha < 0.15f) alpha = 0.15f;
        DrawText(ev->text, LOG_X + 12, LOG_Y + LOG_H - 12 - i * line_h, 11, ColorAlpha(ev->col, alpha));
    }
    pthread_mutex_unlock(&lot->event_mutex);
}

static void draw_stats(ParkingLot *lot, Font font)
{
    (void)font;
    Rectangle r = {STATS_X, STATS_Y, STATS_W, STATS_H};
    draw_panel(r, COL_PANEL, COL_BORDER);

    int x = STATS_X + 16, y = STATS_Y + 16;

    DrawText("LIVE STATISTICS", x, y, 14, COL_WHITE); y += 28;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    DrawText("TOTAL REVENUE", x, y, 11, COL_MUTED); y += 18;
    char rev[32]; snprintf(rev, sizeof(rev), "PKR %.2f", lot->total_revenue);
    DrawText(rev, x, y, 22, COL_GREEN); y += 32;
    DrawLine(x, y, STATS_X + STATS_W - 16, y, COL_BORDER); y += 14;

    DrawText("SERVED", x, y, 11, COL_MUTED);
    char sc[8]; snprintf(sc, sizeof(sc), "%d", lot->vehicles_served);
    DrawText(sc, x + 120, y, 16, COL_GREEN); y += 24;
}

static void draw_panel(Rectangle r, Color bg, Color border)
{
    DrawRectangleRounded(r, 0.04f, 6, bg);
    DrawRectangleRoundedLines(r, 0.04f, 6, border); 
}

static Color lerp_color(Color a, Color b, float t)
{
    return CLITERAL(Color){
        (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t), 255
    };
}

static void push_event(ParkingLot *lot, Color col, const char *fmt, ...)
{
    pthread_mutex_lock(&lot->event_mutex);
    GUIEvent *ev = &lot->events[lot->event_tail];
    va_list args; va_start(args, fmt);
    vsnprintf(ev->text, sizeof(ev->text), fmt, args); va_end(args);
    ev->col = col; ev->age = 0.0f;
    lot->event_tail = (lot->event_tail + 1) % MAX_EVENTS;
    if (lot->event_count < MAX_EVENTS) lot->event_count++;
    else lot->event_head = (lot->event_head + 1) % MAX_EVENTS;
    pthread_mutex_unlock(&lot->event_mutex);
}

static void enqueue_log(ParkingLot *lot, const char *fmt, ...)
{
    pthread_mutex_lock(&lot->log_mutex);
    while (lot->log_count == LOG_QUEUE_SIZE)
        pthread_cond_wait(&lot->log_not_full, &lot->log_mutex);
    va_list args; va_start(args, fmt);
    vsnprintf(lot->log_queue[lot->log_tail].message, sizeof(lot->log_queue[lot->log_tail].message), fmt, args);
    va_end(args);
    lot->log_tail  = (lot->log_tail + 1) % LOG_QUEUE_SIZE;
    lot->log_count++;
    pthread_cond_signal(&lot->log_not_empty);
    pthread_mutex_unlock(&lot->log_mutex);
}

static void wait_for_gate(pthread_mutex_t *m, pthread_cond_t *c, int *flag)
{
    pthread_mutex_lock(m);
    while (*flag == 0) pthread_cond_wait(c, m);
    pthread_mutex_unlock(m);
}

static void lot_init(ParkingLot *lot)
{
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        lot->slots[i].slot_id    = i + 1;
        lot->slots[i].state      = SLOT_FREE;
        lot->slots[i].vehicle_id = -1;
        lot->slots[i].anim       = 0.0f;
        if(i < NUM_BIKE_SLOTS) lot->slots[i].allowed_type = TYPE_BIKE; 
        else if(i < NUM_BIKE_SLOTS + NUM_CAR_SLOTS) lot->slots[i].allowed_type = TYPE_CAR; 
        else lot->slots[i].allowed_type = TYPE_HEAVY; 
        
        // Initialize OS Synchronization variables for EACH slot
        pthread_mutex_init(&lot->slots[i].slot_mutex, NULL);
        pthread_cond_init(&lot->slots[i].remove_cond, NULL);
        lot->slots[i].force_remove = 0;
    }
    
    lot->occupied_count = 0;
    pthread_mutex_init(&lot->slots_mutex, NULL);
    
    sem_init(&lot->sem_bikes, 0, NUM_BIKE_SLOTS);
    sem_init(&lot->sem_cars, 0, NUM_CAR_SLOTS);
    sem_init(&lot->sem_heavy, 0, NUM_HEAVY_SLOTS);
    lot->free_bikes = NUM_BIKE_SLOTS;
    lot->free_cars = NUM_CAR_SLOTS;
    lot->free_heavy = NUM_HEAVY_SLOTS;

    lot->entry_gate_open = 1;
    pthread_mutex_init(&lot->entry_mutex, NULL);
    pthread_cond_init(&lot->entry_cond, NULL);
    
    lot->exit_gate_open = 1;
    pthread_mutex_init(&lot->exit_mutex, NULL);
    pthread_cond_init(&lot->exit_cond, NULL);

    lot->total_revenue = 0.0;
    pthread_mutex_init(&lot->revenue_mutex, NULL);
    
    lot->log_head = lot->log_tail = lot->log_count = 0;
    lot->log_shutdown = 0;
    pthread_mutex_init(&lot->log_mutex, NULL);
    pthread_cond_init(&lot->log_not_empty, NULL);
    pthread_cond_init(&lot->log_not_full, NULL);
    
    lot->vehicles_served = 0;
    lot->global_vehicle_id = 0;
    pthread_mutex_init(&lot->stats_mutex, NULL);

    lot->event_head = lot->event_tail = lot->event_count = 0;
    pthread_mutex_init(&lot->event_mutex, NULL);
    
    lot->ui_state = UI_IDLE;
}

static void lot_destroy(ParkingLot *lot)
{
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        pthread_mutex_destroy(&lot->slots[i].slot_mutex);
        pthread_cond_destroy(&lot->slots[i].remove_cond);
    }
    sem_destroy(&lot->sem_bikes);
    sem_destroy(&lot->sem_cars);
    sem_destroy(&lot->sem_heavy);
    pthread_mutex_destroy(&lot->slots_mutex);
    pthread_mutex_destroy(&lot->entry_mutex);
    pthread_cond_destroy(&lot->entry_cond);
    pthread_mutex_destroy(&lot->exit_mutex);
    pthread_cond_destroy(&lot->exit_cond);
    pthread_mutex_destroy(&lot->revenue_mutex);
    pthread_mutex_destroy(&lot->log_mutex);
    pthread_cond_destroy(&lot->log_not_empty);
    pthread_cond_destroy(&lot->log_not_full);
    pthread_mutex_destroy(&lot->stats_mutex);
    pthread_mutex_destroy(&lot->event_mutex);
}

static const char *ts(void)
{
    static char buf[16];
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti); 
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    return buf;
}
