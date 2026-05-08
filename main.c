#include "parking.h"
#include "raylib.h"   // pulls in all UI/drawing functions

// ─────────────────────────────────────────────────────────────────────────────
// ts  – return current wall-clock time as "HH:MM:SS"
// ─────────────────────────────────────────────────────────────────────────────
const char *ts(void)
{
    static char buf[16];
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// push_event  – append a colour-coded message to the on-screen event ring
// ─────────────────────────────────────────────────────────────────────────────
void push_event(ParkingLot *lot, Color col, const char *fmt, ...)
{
    pthread_mutex_lock(&lot->event_mutex);
    GUIEvent *ev = &lot->events[lot->event_tail];
    va_list args;
    va_start(args, fmt);
    vsnprintf(ev->text, sizeof(ev->text), fmt, args);
    va_end(args);
    ev->col = col;
    ev->age = 0.0f;
    lot->event_tail = (lot->event_tail + 1) % MAX_EVENTS;
    if (lot->event_count < MAX_EVENTS) lot->event_count++;
    else                               lot->event_head = (lot->event_head + 1) % MAX_EVENTS;
    pthread_mutex_unlock(&lot->event_mutex);
}

// ─────────────────────────────────────────────────────────────────────────────
// enqueue_log  – push a message onto the async file-logger queue
// ─────────────────────────────────────────────────────────────────────────────
void enqueue_log(ParkingLot *lot, const char *fmt, ...)
{
    pthread_mutex_lock(&lot->log_mutex);
    while (lot->log_count == LOG_QUEUE_SIZE)
        pthread_cond_wait(&lot->log_not_full, &lot->log_mutex);
    va_list args;
    va_start(args, fmt);
    vsnprintf(lot->log_queue[lot->log_tail].message,
              sizeof(lot->log_queue[lot->log_tail].message), fmt, args);
    va_end(args);
    lot->log_tail  = (lot->log_tail + 1) % LOG_QUEUE_SIZE;
    lot->log_count++;
    pthread_cond_signal(&lot->log_not_empty);
    pthread_mutex_unlock(&lot->log_mutex);
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_for_gate  – block until entry or exit gate is signalled open
// ─────────────────────────────────────────────────────────────────────────────
void wait_for_gate(pthread_mutex_t *m, pthread_cond_t *c, int *flag)
{
    pthread_mutex_lock(m);
    while (*flag == 0) pthread_cond_wait(c, m);
    pthread_mutex_unlock(m);
}

// ─────────────────────────────────────────────────────────────────────────────
// logger_thread  – background thread that drains the log queue to disk
// ─────────────────────────────────────────────────────────────────────────────
void *logger_thread(void *arg)
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

// ─────────────────────────────────────────────────────────────────────────────
// vehicle_thread  – one thread per vehicle: enter → park → wait → exit
// ─────────────────────────────────────────────────────────────────────────────
void *vehicle_thread(void *arg)
{
    VehicleArg *va   = (VehicleArg *)arg;
    ParkingLot *lot  = va->lot;
    int         vid  = va->vehicle_id;
    VehicleType type = va->type;
    int         idx  = va->target_slot;
    free(va);
    pthread_detach(pthread_self());

    const char *t_str       = (type == TYPE_BIKE)  ? "Bike"
                            : (type == TYPE_CAR)   ? "Car" : "Heavy";
    sem_t      *target_sem  = (type == TYPE_BIKE)  ? &lot->sem_bikes
                            : (type == TYPE_CAR)   ? &lot->sem_cars : &lot->sem_heavy;
    int *target_free_ui     = (type == TYPE_BIKE)  ? &lot->free_bikes
                            : (type == TYPE_CAR)   ? &lot->free_cars : &lot->free_heavy;

    // Acquire semaphore slot
    sem_wait(target_sem);
    __sync_fetch_and_sub(target_free_ui, 1);

    push_event(lot, COL_MUTED, "%s V%02d arriving at gate...", t_str, vid);

    wait_for_gate(&lot->entry_mutex, &lot->entry_cond, &lot->entry_gate_open);
    lot->entry_pulse = 1.0f;
    usleep(GATE_OPEN_DELAY_US);

    // Enter slot
    pthread_mutex_lock(&lot->slots_mutex);
    lot->slots[idx].state         = SLOT_OCCUPIED;
    lot->slots[idx].vehicle_id    = vid;
    lot->slots[idx].entry_time    = time(NULL);
    lot->slots[idx].animating_in  = 1;
    lot->slots[idx].animating_out = 0;
    lot->slots[idx].anim          = 0.0f;
    lot->slots[idx].force_remove  = 0;
    lot->occupied_count++;
    int sid = lot->slots[idx].slot_id;
    pthread_mutex_unlock(&lot->slots_mutex);

    push_event(lot, COL_GREEN, "%s V%02d ASSIGNED  → Slot %02d", t_str, vid, sid);
    enqueue_log(lot, "[%s] %s V%02d ENTERED slot %02d", ts(), t_str, vid, sid);

    // Wait indefinitely until the user signals removal via cond var
    pthread_mutex_lock(&lot->slots[idx].slot_mutex);
    while (lot->slots[idx].force_remove == 0)
        pthread_cond_wait(&lot->slots[idx].remove_cond, &lot->slots[idx].slot_mutex);
    pthread_mutex_unlock(&lot->slots[idx].slot_mutex);

    wait_for_gate(&lot->exit_mutex, &lot->exit_cond, &lot->exit_gate_open);
    lot->exit_pulse = 1.0f;
    usleep(GATE_OPEN_DELAY_US);

    // Leave slot
    pthread_mutex_lock(&lot->slots_mutex);
    time_t duration            = time(NULL) - lot->slots[idx].entry_time;
    lot->slots[idx].state         = SLOT_FREE;
    lot->slots[idx].vehicle_id    = -1;
    lot->slots[idx].entry_time    = 0;
    lot->slots[idx].animating_out = 1;
    lot->slots[idx].animating_in  = 0;
    lot->occupied_count--;
    pthread_mutex_unlock(&lot->slots_mutex);

    double fee = (double)duration * PARKING_FEE_PER_SEC;
    if (type == TYPE_BIKE)  fee *= 0.5;
    if (type == TYPE_HEAVY) fee *= 2.0;

    pthread_mutex_lock(&lot->revenue_mutex);
    lot->total_revenue += fee;
    pthread_mutex_unlock(&lot->revenue_mutex);

    pthread_mutex_lock(&lot->stats_mutex);
    lot->vehicles_served++;
    pthread_mutex_unlock(&lot->stats_mutex);

    __sync_fetch_and_add(target_free_ui, 1);
    sem_post(target_sem);

    push_event(lot, COL_AMBER, "%s V%02d REMOVED ← Slot %02d (Parked: %lds)",
               t_str, vid, sid, (long)duration);
    enqueue_log(lot, "[%s] %s V%02d REMOVED from slot %02d fee %.2f",
                ts(), t_str, vid, sid, fee);

    return NULL;
}

// ─────────────────────────────────────────────────────────────────────────────
// spawn_vehicle  – create a new VehicleArg and launch its thread
// ─────────────────────────────────────────────────────────────────────────────
void spawn_vehicle(ParkingLot *lot, VehicleType type, int slot_idx)
{
    pthread_mutex_lock(&lot->stats_mutex);
    lot->global_vehicle_id++;
    int new_id = lot->global_vehicle_id;
    pthread_mutex_unlock(&lot->stats_mutex);

    VehicleArg *arg = malloc(sizeof(VehicleArg));
    arg->lot         = lot;
    arg->type        = type;
    arg->vehicle_id  = new_id;
    arg->target_slot = slot_idx;

    pthread_t tid;
    pthread_create(&tid, NULL, vehicle_thread, arg);
}

// ─────────────────────────────────────────────────────────────────────────────
// lot_init  – initialise every mutex, semaphore, and slot in the lot
// ─────────────────────────────────────────────────────────────────────────────
void lot_init(ParkingLot *lot)
{
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        lot->slots[i].slot_id    = i + 1;
        lot->slots[i].state      = SLOT_FREE;
        lot->slots[i].vehicle_id = -1;
        lot->slots[i].anim       = 0.0f;

        if      (i < NUM_BIKE_SLOTS)                        lot->slots[i].allowed_type = TYPE_BIKE;
        else if (i < NUM_BIKE_SLOTS + NUM_CAR_SLOTS)        lot->slots[i].allowed_type = TYPE_CAR;
        else                                                 lot->slots[i].allowed_type = TYPE_HEAVY;

        pthread_mutex_init(&lot->slots[i].slot_mutex, NULL);
        pthread_cond_init(&lot->slots[i].remove_cond, NULL);
        lot->slots[i].force_remove = 0;
    }

    lot->occupied_count = 0;
    pthread_mutex_init(&lot->slots_mutex, NULL);

    sem_init(&lot->sem_bikes, 0, NUM_BIKE_SLOTS);
    sem_init(&lot->sem_cars,  0, NUM_CAR_SLOTS);
    sem_init(&lot->sem_heavy, 0, NUM_HEAVY_SLOTS);
    lot->free_bikes = NUM_BIKE_SLOTS;
    lot->free_cars  = NUM_CAR_SLOTS;
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

    lot->vehicles_served   = 0;
    lot->global_vehicle_id = 0;
    pthread_mutex_init(&lot->stats_mutex, NULL);

    lot->event_head = lot->event_tail = lot->event_count = 0;
    pthread_mutex_init(&lot->event_mutex, NULL);

    lot->ui_state = UI_IDLE;
}

// ─────────────────────────────────────────────────────────────────────────────
// lot_destroy  – clean up every OS synchronisation primitive
// ─────────────────────────────────────────────────────────────────────────────
void lot_destroy(ParkingLot *lot)
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

// ─────────────────────────────────────────────────────────────────────────────
// main  – application entry point: init → game loop → cleanup
// ─────────────────────────────────────────────────────────────────────────────
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

        // ── Handle mouse clicks on the grid ──────────────────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && lot->ui_state != UI_IDLE) {
            Vector2 mouse = GetMousePosition();
            pthread_mutex_lock(&lot->slots_mutex);

            for (int i = 0; i < TOTAL_SLOTS; i++) {
                if (CheckCollisionPointRec(mouse, lot->slots[i].bounds)) {

                    if (lot->ui_state >= UI_ADD_BIKE && lot->ui_state <= UI_ADD_HEAVY) {
                        VehicleType req_type = (VehicleType)(lot->ui_state - UI_ADD_BIKE);
                        if (lot->slots[i].state == SLOT_FREE &&
                            lot->slots[i].allowed_type == req_type)
                        {
                            lot->slots[i].state = SLOT_RESERVED;
                            spawn_vehicle(lot, req_type, i);
                            lot->ui_state = UI_IDLE;
                        }
                    }
                    else if (lot->ui_state == UI_REMOVE) {
                        if (lot->slots[i].state == SLOT_OCCUPIED) {
                            pthread_mutex_lock(&lot->slots[i].slot_mutex);
                            lot->slots[i].force_remove = 1;
                            pthread_cond_signal(&lot->slots[i].remove_cond);
                            pthread_mutex_unlock(&lot->slots[i].slot_mutex);
                            lot->ui_state = UI_IDLE;
                        }
                    }
                }
            }
            pthread_mutex_unlock(&lot->slots_mutex);
        }

        // ── Animate slot entry / exit ─────────────────────────────────────────
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

        // ── Gate pulse decay ──────────────────────────────────────────────────
        if (lot->entry_pulse > 0) lot->entry_pulse -= dt * 2.0f;
        if (lot->exit_pulse  > 0) lot->exit_pulse  -= dt * 2.0f;
        if (lot->entry_pulse < 0) lot->entry_pulse  = 0;
        if (lot->exit_pulse  < 0) lot->exit_pulse   = 0;

        // ── Age event log entries ─────────────────────────────────────────────
        pthread_mutex_lock(&lot->event_mutex);
        for (int i = 0; i < MAX_EVENTS; i++) lot->events[i].age += dt;
        pthread_mutex_unlock(&lot->event_mutex);

        // ── Draw everything ───────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground(COL_BG);

        draw_sidebar(lot, font);
        draw_grid(lot, font);
        draw_log_panel(lot, font);
        draw_stats(lot, font);

        DrawRectangle(0, 0, WIN_W, 8, COL_ACCENT);   // accent top bar

        EndDrawing();
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
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
