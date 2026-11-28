#include "serial.h"
#include "types.h"
#include "timer.h"
#include "clock.h"
#include "entropy.h"
#include "scheduler.h"
#include "cpu.h"
#include "kformat.h"
#include "terminal_ui.h"

#define SNAKE_W 36
#define SNAKE_H 14
#define SNAKE_CAP (SNAKE_W * SNAKE_H)

static int snake_has(const int *sx, const int *sy, int length, int x, int y) {
    for (int i = 0; i < length; ++i)
        if (sx[i] == x && sy[i] == y) return 1;
    return 0;
}

static void snake_spawn_food(const int *sx, const int *sy, int length,
                             int *food_x, int *food_y) {
    int free_cells = 0;
    int choice;
    for (int y = 1; y < SNAKE_H - 1; ++y)
        for (int x = 1; x < SNAKE_W - 1; ++x)
            if (!snake_has(sx, sy, length, x, y)) free_cells++;
    if (!free_cells) { *food_x = -1; *food_y = -1; return; }
    choice = (int)(entropy_u32() % (u32)free_cells);
    for (int y = 1; y < SNAKE_H - 1; ++y) {
        for (int x = 1; x < SNAKE_W - 1; ++x) {
            if (!snake_has(sx, sy, length, x, y)) {
                if (choice-- == 0) { *food_x = x; *food_y = y; return; }
            }
        }
    }
    *food_x = -1;
    *food_y = -1;
}

static void snake_draw(const int *sx, const int *sy, int length,
                       int food_x, int food_y, int score, int paused) {
    char field[SNAKE_H][SNAKE_W];
    char info[80];
    for (int y = 0; y < SNAKE_H; ++y)
        for (int x = 0; x < SNAKE_W; ++x)
            field[y][x] = (x == 0 || y == 0 || x == SNAKE_W - 1 || y == SNAKE_H - 1) ? '#' : ' ';
    for (int i = length - 1; i >= 1; --i) field[sy[i]][sx[i]] = 'o';
    field[sy[0]][sx[0]] = '@';
    if (food_x >= 0) field[food_y][food_x] = '*';
    terminal_ui_clear();
    serial_write(TERM_GREEN "SNAKE" TERM_RESET "  Move: arrows/WASD   Pause: P   Quit: Q\n");
    (void)ksnprintf(info, sizeof(info), "Score: %d   Length: %d%s", score, length, paused ? "   [PAUSED]" : "");
    serial_write(info); serial_write("\n");
    for (int y = 0; y < SNAKE_H; ++y) {
        for (int x = 0; x < SNAKE_W; ++x) {
            char c = field[y][x];
            if (c == '#') serial_write(TERM_DIM "#" TERM_RESET);
            else if (c == '@') serial_write(TERM_GREEN "@" TERM_RESET);
            else if (c == 'o') serial_write(TERM_CYAN "o" TERM_RESET);
            else if (c == '*') serial_write(TERM_RED "*" TERM_RESET);
            else serial_putc(' ');
        }
        serial_write("\n");
    }
}

void denos_game_snake(void) {
restart:
    {
        int sx[SNAKE_CAP], sy[SNAKE_CAP];
        int length = 4, direction = 1, food_x, food_y, score = 0;
        int paused = 0, dead = 0, won = 0, dirty = 1;
        u64 last = timer_ticks();
        u64 step_ticks = ((u64)clock_frequency() * 8u) / 100u;
        if (!step_ticks) step_ticks = 1u;
        for (int i = 0; i < length; ++i) { sx[i] = 8 - i; sy[i] = 7; }
        snake_spawn_food(sx, sy, length, &food_x, &food_y);
        while (!dead) {
            int key;
            while (serial_try_getkey(&key)) {
                int next = direction;
                if (key == 'q' || key == 'Q') return;
                if (key == 'p' || key == 'P' || key == ' ') { paused = !paused; dirty = 1; }
                if (key == KEY_UP || key == 'w' || key == 'W') next = 0;
                if (key == KEY_RIGHT || key == 'd' || key == 'D') next = 1;
                if (key == KEY_DOWN || key == 's' || key == 'S') next = 2;
                if (key == KEY_LEFT || key == 'a' || key == 'A') next = 3;
                if ((next + 2) % 4 != direction) direction = next;
            }
            u64 now = timer_ticks();
            if (!paused && now - last >= step_ticks) {
                int nx = sx[0], ny = sy[0];
                last = now;
                if (direction == 0) ny--;
                else if (direction == 1) nx++;
                else if (direction == 2) ny++;
                else nx--;
                int eating = nx == food_x && ny == food_y;
                if (nx <= 0 || nx >= SNAKE_W - 1 || ny <= 0 || ny >= SNAKE_H - 1 ||
                    snake_has(sx, sy, length - (eating ? 0 : 1), nx, ny)) {
                    dead = 1;
                } else {
                    int new_length = length;
                    if (eating && length < SNAKE_CAP) { new_length++; score += 10; }
                    for (int i = new_length - 1; i > 0; --i) {
                        if (i <= length) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
                    }
                    sx[0] = nx; sy[0] = ny; length = new_length;
                    dirty = 1;
                    if (eating) {
                        snake_spawn_food(sx, sy, length, &food_x, &food_y);
                        if (food_x < 0) { dead = 1; won = 1; }
                    }
                }
            }
            if (dirty || dead) {
                snake_draw(sx, sy, length, food_x, food_y, score, paused);
                dirty = 0;
            }
            if (dead) break;
            scheduler_run(); cpu_pause();
        }
        serial_write(won ? TERM_GREEN "\nYOU FILLED THE BOARD!" TERM_RESET "  Score: " :
                            TERM_RED "\nGAME OVER" TERM_RESET "  Score: ");
        terminal_ui_u64((u64)score);
        serial_write("\nR restart   Q return to arcade\n");
        for (;;) {
            int key = serial_getkey();
            if (key == 'r' || key == 'R') goto restart;
            if (key == 'q' || key == 'Q' || key == 27) return;
        }
    }
}


