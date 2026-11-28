#include "serial.h"
#include "types.h"
#include "terminal_ui.h"

static const char maze_map[10][16] = {
    "###############",
    "#P....#.......#",
    "#.##..#..###..#",
    "#....##....#..#",
    "###.####.#.#..#",
    "#...#....#....#",
    "#.#.#.##.###..#",
    "#.#...#...#...#",
    "#.....#...#..X#",
    "###############"
};

static void maze_draw(int px, int py, const u8 collected[10][15],
                      int found, int total, int won) {
    terminal_ui_clear();
    serial_write(TERM_CYAN "MAZE RUNNER" TERM_RESET "  Move: arrows/WASD   Quit: Q\n");
    serial_write("Find every * then reach X. Lights: "); terminal_ui_u64((u64)found);
    serial_write("/"); terminal_ui_u64((u64)total); serial_write("\n\n");
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 15; ++x) {
            char c = maze_map[y][x];
            if (x == px && y == py) serial_write(TERM_GREEN "@" TERM_RESET);
            else if (c == '#') serial_write(TERM_DIM "#" TERM_RESET);
            else if (c == 'X') serial_write(TERM_YELLOW "X" TERM_RESET);
            else if (c == '.' && !collected[y][x]) serial_write(TERM_RED "*" TERM_RESET);
            else serial_putc(' ');
        }
        serial_write("\n");
    }
    if (won) serial_write(TERM_GREEN "\nMAZE CLEARED!" TERM_RESET "  Press R to restart or Q to return.\n");
}

void denos_game_maze(void) {
restart_maze: ;
    int px = 1, py = 1, total = 0, found = 0, won = 0;
    u8 collected[10][15] = {{0}};
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 15; ++x)
            if (maze_map[y][x] == '.') total++;
    for (;;) {
        int key, nx = px, ny = py;
        maze_draw(px, py, collected, found, total, won);
        key = serial_getkey();
        if (key == 'q' || key == 'Q' || key == 27) return;
        if (won) {
            if (key == 'r' || key == 'R') goto restart_maze;
            continue;
        }
        if (key == KEY_UP || key == 'w' || key == 'W') ny--;
        else if (key == KEY_DOWN || key == 's' || key == 'S') ny++;
        else if (key == KEY_LEFT || key == 'a' || key == 'A') nx--;
        else if (key == KEY_RIGHT || key == 'd' || key == 'D') nx++;
        if (nx >= 0 && nx < 15 && ny >= 0 && ny < 10 && maze_map[ny][nx] != '#') {
            px = nx; py = ny;
            if (maze_map[py][px] == '.' && !collected[py][px]) {
                collected[py][px] = 1; found++;
            }
            if (maze_map[py][px] == 'X' && found == total) won = 1;
        }
    }
}

