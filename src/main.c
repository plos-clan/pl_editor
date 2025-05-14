/**
 * main.c - Entry point for pleditor
 */

#include <stdio.h>

#include "pleditor.h"
#include "platform.h"
#include "syntax.h"

int main(int argc, char *argv[]) {
    /* Initialize platform */
    if (!pleditor_platform_init()) {
        fprintf(stderr, "Failed to initialize terminal\n");
        return 1;
    }

    /* Initialize editor state */
    pleditor_state state;
    pleditor_init(&state);
    pleditor_syntax_init(&state);

    /* Open file if specified */
    if (argc >= 2 && !pleditor_open(&state, argv[1])) {
        pleditor_platform_cleanup();
        return 1;
    }

    /* Set initial status message */
    pleditor_set_status_message(&state,
        "HELP: Ctrl-S = save/save as | Ctrl-Q = quit | Ctrl-R = toggle line numbers");

    /* Main editor loop */
    while (!state.should_quit) {
        pleditor_refresh_screen(&state);
        int c = pleditor_platform_read_key();
        pleditor_handle_keypress(&state, c);
    }

    /* Cleanup resources and restore terminal status */
    pleditor_free(&state);
    pleditor_platform_cleanup();

    return 0;
}
