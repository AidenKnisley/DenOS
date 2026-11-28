#include "serial.h"
#include "terminal_ui.h"
#include "terminal_games.h"

void terminal_arcade_run(void) {
    for (;;) {
        int key;
        terminal_ui_clear();
        serial_write(TERM_CYAN "==================== DENOS ARCADE ====================" TERM_RESET "\n\n");
        serial_write("  1  SNAKE        Eat the stars. Don't hit the walls.\n");
        serial_write("  2  BREAKOUT     Bounce the ball and clear the bricks.\n");
        serial_write("  3  MAZE RUNNER  Collect every star and find the exit.\n\n");
        serial_write("Choose 1, 2, or 3. Press Q to return to the shell.\n");
        key = serial_getkey();
        if (key == 'q' || key == 'Q' || key == 27) break;
        if (key == '1') denos_game_snake();
        else if (key == '2') denos_game_breakout();
        else if (key == '3') denos_game_maze();
    }
    terminal_ui_clear();
}
