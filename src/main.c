/**
 * main.c - Entry point for pleditor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pleditor.h"
#include "platform.h"

void print_usage() {
    printf("Usage: pleditor [filename]\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    /* Initialize platform */
    if (!pleditor_platform_init()) {
        fprintf(stderr, "Failed to initialize terminal\n");
        return 1;
    }
    
    /* Always restore terminal settings before exit */
    atexit(pleditor_platform_cleanup);
    
    /* Initialize editor state */
    pleditor_state state;
    pleditor_init(&state);
    
    /* Open file if specified */
    if (argc >= 2) {
        pleditor_open(&state, argv[1]);
    }
    
    /* Set initial status message */
    pleditor_set_status_message(&state, 
        "HELP: Ctrl-S = save | Ctrl-Q = quit");
    
    /* Main editor loop */
    while (1) {
        pleditor_refresh_screen(&state);
        int c = pleditor_platform_read_key(0);
        pleditor_process_keypress(&state, c);
    }
    
    /* Free editor resources */
    pleditor_free(&state);
    
    return 0;
}