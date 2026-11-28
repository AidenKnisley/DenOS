#include "serial.h"
#include "types.h"
#include "timer.h"
#include "clock.h"
#include "scheduler.h"
#include "cpu.h"
#include "kformat.h"
#include "terminal_ui.h"

#define BREAK_W 40
#define BREAK_H 16
static void breakout_draw(const u8 bricks[4][9], int ball_x, int ball_y,
                          int paddle, int score, int lives, int paused) {
    char field[BREAK_H][BREAK_W];
    char info[80];
    for (int y = 0; y < BREAK_H; ++y)
        for (int x = 0; x < BREAK_W; ++x)
            field[y][x] = (x == 0 || y == 0 || x == BREAK_W - 1 || y == BREAK_H - 1) ? '#' : ' ';
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 9; ++col)
            if (bricks[row][col]) {
                int x = 3 + col * 4;
                field[2 + row][x] = '[';
                field[2 + row][x + 1] = ']';
            }
    for (int i = 0; i < 7; ++i) field[14][paddle + i] = '=';
    field[ball_y][ball_x] = 'o';
    terminal_ui_clear();
    serial_write(TERM_CYAN "BREAKOUT" TERM_RESET "  Move: A/D or arrows   Pause: P   Quit: Q\n");
    (void)ksnprintf(info, sizeof(info), "Score: %d   Lives: %d%s", score, lives, paused ? "   [PAUSED]" : "");
    serial_write(info); serial_write("\n");
    for (int y = 0; y < BREAK_H; ++y) {
        for (int x = 0; x < BREAK_W; ++x) {
            char c = field[y][x];
            if (c == '#') serial_write(TERM_DIM);
            else if (c == '[' || c == ']') serial_write(TERM_YELLOW);
            else if (c == '=') serial_write(TERM_GREEN);
            else if (c == 'o') serial_write(TERM_RED);
            serial_putc(c);
            if (c == '#' || c == '[' || c == ']' || c == '=' || c == 'o')
                serial_write(TERM_RESET);
        }
        serial_write("\n");
    }
}

void denos_game_breakout(void) {
    u8 bricks[4][9];
    int ball_x = 20, ball_y = 12, vx = 1, vy = -1, paddle = 16;
    int score = 0, lives = 3, paused = 0, remaining = 36;
    u64 last = timer_ticks();
    u64 step_ticks = ((u64)clock_frequency() * 4u) / 100u;
    if (!step_ticks) step_ticks = 1u;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 9; ++col) bricks[row][col] = 1;
    while (lives > 0 && remaining > 0) {
        int key;
        int dirty = 0;
        while (serial_try_getkey(&key)) {
            if (key == 'q' || key == 'Q') return;
            if (key == 'p' || key == 'P' || key == ' ') { paused = !paused; dirty = 1; }
            if (key == KEY_LEFT || key == 'a' || key == 'A') { paddle--; dirty = 1; }
            if (key == KEY_RIGHT || key == 'd' || key == 'D') { paddle++; dirty = 1; }
        }
        if (paddle < 1) paddle = 1;
        if (paddle > BREAK_W - 9) paddle = BREAK_W - 9;
        u64 now = timer_ticks();
        if (!paused && now - last >= step_ticks) {
            int nx, ny;
            dirty = 1;
            last = now;
            nx = ball_x + vx; ny = ball_y + vy;
            if (nx <= 1 || nx >= BREAK_W - 2) { vx = -vx; nx = ball_x + vx; }
            if (ny <= 1) { vy = -vy; ny = ball_y + vy; }
            if (ny == 13 && vy > 0 && nx >= paddle - 1 && nx <= paddle + 7) {
                vy = -1;
                vx = nx < paddle + 3 ? -1 : 1;
                ny = 12;
            }
            if (ny >= 15) {
                lives--;
                ball_x = 20; ball_y = 12; vx = 1; vy = -1;
                if (lives <= 0) break;
                breakout_draw((const u8 (*)[9])bricks, ball_x, ball_y, paddle, score, lives, paused);
                continue;
            }
            if (ny >= 2 && ny <= 5 && nx >= 3 && nx < 3 + 9 * 4 &&
                ((nx - 3) % 4) < 2) {
                int col = (nx - 3) / 4;
                int row = ny - 2;
                if (bricks[row][col]) {
                    bricks[row][col] = 0; remaining--; score += 10;
                    vy = -vy; ny = ball_y + vy;
                }
            }
            ball_x = nx; ball_y = ny;
        }
        if (dirty) breakout_draw((const u8 (*)[9])bricks, ball_x, ball_y, paddle, score, lives, paused);
        scheduler_run(); cpu_pause();
    }
    breakout_draw((const u8 (*)[9])bricks, ball_x, ball_y, paddle, score, lives, paused);
    serial_write(lives ? TERM_GREEN "\nYOU CLEARED THE WALL!" TERM_RESET : TERM_RED "\nGAME OVER" TERM_RESET);
    serial_write("  Score: "); terminal_ui_u64((u64)score);
    terminal_ui_wait_key("\nPress any key to return to arcade...");
}

