#ifndef PARKING_H
#define PARKING_H

#include "raylib.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
//Capacity
#define NUM_BIKE_SLOTS      4
#define NUM_CAR_SLOTS       6
#define NUM_HEAVY_SLOTS     2
#define TOTAL_SLOTS         (NUM_BIKE_SLOTS + NUM_CAR_SLOTS + NUM_HEAVY_SLOTS)


#define LOG_QUEUE_SIZE      128
#define GATE_OPEN_DELAY_US  300000
#define PARKING_FEE_PER_SEC 2.50
#define MAX_LOG_DISPLAY     18
#define MAX_EVENTS          256
#define MAX_WAIT_QUEUE      64      /* max vehicles waiting per type          */
#define PRIORITY_MIN        1
#define PRIORITY_MAX        9

// Window / layout
#define WIN_W   1280
#define WIN_H   760
#define FPS     60

#define SIDEBAR_X    10
#define SIDEBAR_Y    10
#define SIDEBAR_W    280
#define SIDEBAR_H    740
#define GRID_X       300
#define GRID_Y       10
#define GRID_W       620
#define GRID_H       460
#define LOG_X        300
#define LOG_Y        480
#define LOG_W        620
#define LOG_H        270
#define STATS_X      930
#define STATS_Y      10
#define STATS_W      340
#define STATS_H      740

// Palette
#define COL_BG           CLITERAL(Color){ 15,  17,  26, 255}
#define COL_PANEL        CLITERAL(Color){ 24,  27,  40, 255}
#define COL_BORDER       CLITERAL(Color){ 55,  62,  90, 255}
#define COL_ACCENT       CLITERAL(Color){ 82, 130, 255, 255}
#define COL_FREE         CLITERAL(Color){ 34,  48,  34, 255}
#define COL_FREE_BD      CLITERAL(Color){ 58, 120,  58, 255}
#define COL_OCC          CLITERAL(Color){ 55,  28,  28, 255}
#define COL_OCC_BD       CLITERAL(Color){210,  70,  70, 255}
#define COL_WHITE        CLITERAL(Color){220, 225, 240, 255}
#define COL_MUTED        CLITERAL(Color){110, 118, 150, 255}
#define COL_GREEN        CLITERAL(Color){ 72, 200,  90, 255}
#define COL_RED          CLITERAL(Color){220,  72,  72, 255}
#define COL_AMBER        CLITERAL(Color){240, 170,  50, 255}
#define COL_BLUE         CLITERAL(Color){ 82, 130, 255, 255}
#define COL_WINDOW       CLITERAL(Color){180, 210, 255, 160}
#define COL_TYRE         CLITERAL(Color){ 30,  30,  30, 255}
#define COL_PRIO_HI      CLITERAL(Color){255,  70,  70, 255}
#define COL_PRIO_MID     CLITERAL(Color){240, 170,  50, 255}
#define COL_PRIO_LO      CLITERAL(Color){ 72, 200,  90, 255}
#define COL_INPUT_BG     CLITERAL(Color){ 30,  34,  52, 255}
#define COL_INPUT_ACTIVE CLITERAL(Color){ 45,  55,  90, 255}

typedef enum { SLOT_FREE = 0, SLOT_RESERVED, SLOT_OCCUPIED } SlotState;
typedef enum { TYPE_BIKE = 0, TYPE_CAR, TYPE_HEAVY }         VehicleType;

typedef enum {
    UI_IDLE = 0,
    UI_INPUT_BIKE,
    UI_INPUT_CAR,
    UI_INPUT_HEAVY,
    UI_REMOVE
} UIActionState;

typedef struct {
    int         vehicle_id;
    char        vehicle_num[16];
    int         priority;        
    VehicleType type;
} WaitEntry;


typedef struct {
    int             slot_id;
    VehicleType     allowed_type;
    SlotState       state;
    int             vehicle_id;
    char            vehicle_num[16];
    int             priority;
    time_t          entry_time;
    float           anim;
    int             animating_in;
    int             animating_out;
    Rectangle       bounds;
    pthread_mutex_t slot_mutex;
    pthread_cond_t  remove_cond;
    int             force_remove;
} ParkingSlot;

typedef struct { char text[180]; Color col; float age; } GUIEvent;
typedef struct { char message[256]; }                     LogEntry;


typedef struct {
    char  vehicle_num[16];
    int   num_len;
    int   priority;          
    bool  num_active;
} InputForm;


typedef struct {
    ParkingSlot     slots[TOTAL_SLOTS];
    int             occupied_count;
    pthread_mutex_t slots_mutex;

    sem_t           sem_bikes;
    sem_t           sem_cars;
    sem_t           sem_heavy;
    int             free_bikes;
    int             free_cars;
    int             free_heavy;

    
    WaitEntry       wait_bike[MAX_WAIT_QUEUE];
    int             wait_bike_count;
    WaitEntry       wait_car[MAX_WAIT_QUEUE];
    int             wait_car_count;
    WaitEntry       wait_heavy[MAX_WAIT_QUEUE];
    int             wait_heavy_count;
    pthread_mutex_t wait_mutex;

    int             entry_gate_open;
    pthread_mutex_t entry_mutex;
    pthread_cond_t  entry_cond;
    int             exit_gate_open;
    pthread_mutex_t exit_mutex;
    pthread_cond_t  exit_cond;

    double          total_revenue;
    pthread_mutex_t revenue_mutex;

    LogEntry        log_queue[LOG_QUEUE_SIZE];
    int             log_head, log_tail, log_count;
    int             log_shutdown;
    pthread_mutex_t log_mutex;
    pthread_cond_t  log_not_empty;
    pthread_cond_t  log_not_full;

    int             vehicles_served;
    int             global_vehicle_id;
    pthread_mutex_t stats_mutex;

    GUIEvent        events[MAX_EVENTS];
    int             event_head, event_tail, event_count;
    pthread_mutex_t event_mutex;

    float           entry_pulse;
    float           exit_pulse;

    UIActionState   ui_state;
    InputForm       form;
} ParkingLot;


typedef struct {
    int         vehicle_id;
    char        vehicle_num[16];
    int         priority;
    VehicleType type;
    ParkingLot *lot;
} VehicleArg;

void        lot_init(ParkingLot *lot);
void        lot_destroy(ParkingLot *lot);
void        push_event(ParkingLot *lot, Color col, const char *fmt, ...);
void        enqueue_log(ParkingLot *lot, const char *fmt, ...);
void       *vehicle_thread(void *arg);
void       *logger_thread(void *arg);
void        wait_for_gate(pthread_mutex_t *m, pthread_cond_t *c, int *flag);
void        spawn_vehicle(ParkingLot *lot, VehicleType type,
                          const char *vehicle_num, int priority);
const char *ts(void);


void  draw_panel(Rectangle r, Color bg, Color border);
void  draw_slot(int sx, int sy, int sw, int sh, ParkingSlot *s);
void  draw_vehicle(int sx, int sy, int sw, int sh, float anim,
                   int vid, VehicleType type, int priority);
void  draw_sidebar(ParkingLot *lot, Font font);
void  draw_grid(ParkingLot *lot, Font font);
void  draw_log_panel(ParkingLot *lot, Font font);
void  draw_stats(ParkingLot *lot, Font font);
Color lerp_color(Color a, Color b, float t);
Color priority_color(int p);
bool  gui_button(Rectangle r, const char *text, Color base_col);

#endif 
