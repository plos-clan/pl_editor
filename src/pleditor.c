/**
 * pleditor.c - Core editor implementation
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "pleditor.h"
#include "terminal.h"
#include "platform.h"

/* For calculating the render index from a chars index */
int pleditor_cx_to_rx(pleditor_row *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (PLEDITOR_TAB_STOP - 1) - (rx % PLEDITOR_TAB_STOP);
        rx++;
    }
    return rx;
}

/* Update the render string for a row (for handling tabs, etc.) */
void pleditor_update_row(pleditor_row *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(PLEDITOR_TAB_STOP - 1) + 1);
    if (row->render == NULL) exit(1);

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % PLEDITOR_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->render_size = idx;
}

/* Insert a row at the specified position */
void pleditor_insert_row(pleditor_state *state, int at, char *s, size_t len) {
    if (at < 0 || at > state->num_rows) return;

    state->rows = realloc(state->rows, sizeof(pleditor_row) * (state->num_rows + 1));
    memmove(&state->rows[at + 1], &state->rows[at], sizeof(pleditor_row) * (state->num_rows - at));

    state->rows[at].size = len;
    state->rows[at].chars = malloc(len + 1);
    memcpy(state->rows[at].chars, s, len);
    state->rows[at].chars[len] = '\0';

    state->rows[at].render_size = 0;
    state->rows[at].render = NULL;
    state->rows[at].hl = NULL;
    pleditor_update_row(&state->rows[at]);
    
    /* Update highlighting for the row if syntax highlighting is enabled */
    extern void pleditor_syntax_update_row(pleditor_state *state, int row_idx);
    if (state->syntax) {
        pleditor_syntax_update_row(state, at);
    }

    state->num_rows++;
    state->dirty = true;
}

/* Free a row's memory */
void pleditor_free_row(pleditor_row *row) {
    free(row->render);
    free(row->chars);
    /* Free highlighting memory if allocated */
    if (row->hl) {
        free(row->hl->hl);
        free(row->hl);
    }
}

/* Delete a row at the specified position */
void pleditor_delete_row(pleditor_state *state, int at) {
    if (at < 0 || at >= state->num_rows) return;
    pleditor_free_row(&state->rows[at]);
    memmove(&state->rows[at], &state->rows[at + 1], sizeof(pleditor_row) * (state->num_rows - at - 1));
    state->num_rows--;
    state->dirty = true;
}

/* Insert a character at the current cursor position */
void pleditor_insert_char(pleditor_state *state, int c) {
    /* Don't insert control characters in the text (except TAB) */
    if (iscntrl(c) && c != '\t') {
        return;
    }
    
    if (state->cy == state->num_rows) {
        pleditor_insert_row(state, state->num_rows, "", 0);
    }
    
    pleditor_row *row = &state->rows[state->cy];
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[state->cx + 1], &row->chars[state->cx], row->size - state->cx + 1);
    row->size++;
    row->chars[state->cx] = c;
    pleditor_update_row(row);
    
    /* Update syntax highlighting for the modified row */
    if (state->syntax) {
        pleditor_syntax_update_row(state, state->cy);
    }
    
    state->cx++;
    state->dirty = true;
}

/* Insert a newline (Enter key) */
void pleditor_insert_newline(pleditor_state *state) {
    if (state->cx == 0) {
        pleditor_insert_row(state, state->cy, "", 0);
    } else {
        pleditor_row *row = &state->rows[state->cy];
        pleditor_insert_row(state, state->cy + 1, &row->chars[state->cx], row->size - state->cx);
        row = &state->rows[state->cy]; /* Row pointer may have changed due to realloc */
        row->size = state->cx;
        row->chars[row->size] = '\0';
        pleditor_update_row(row);
        
        /* Update syntax highlighting for the modified current row */
        if (state->syntax) {
            pleditor_syntax_update_row(state, state->cy);
        }
    }
    state->cy++;
    state->cx = 0;
}

/* Delete the character at the current cursor position */
void pleditor_delete_char(pleditor_state *state) {
    if (state->cy == state->num_rows) return;
    if (state->cx == 0 && state->cy == 0) return;

    pleditor_row *row = &state->rows[state->cy];
    if (state->cx > 0) {
        memmove(&row->chars[state->cx - 1], &row->chars[state->cx], row->size - state->cx + 1);
        row->size--;
        state->cx--;
        pleditor_update_row(row);
        
        /* Update syntax highlighting for the modified row */
        if (state->syntax) {
            pleditor_syntax_update_row(state, state->cy);
        }
        
        state->dirty = true;
    } else {
        /* Backspace at start of line - append this line to previous line */
        state->cx = state->rows[state->cy - 1].size;
        pleditor_row *prev_row = &state->rows[state->cy - 1];
        prev_row->chars = realloc(prev_row->chars, prev_row->size + row->size + 1);
        memcpy(&prev_row->chars[prev_row->size], row->chars, row->size);
        prev_row->size += row->size;
        prev_row->chars[prev_row->size] = '\0';
        pleditor_update_row(prev_row);
        
        /* Update syntax highlighting for the modified previous row */
        if (state->syntax) {
            pleditor_syntax_update_row(state, state->cy - 1);
        }
        
        pleditor_delete_row(state, state->cy);
        state->cy--;
    }
}

/* Scroll the editor if cursor is outside visible area */
void pleditor_scroll(pleditor_state *state) {
    state->rx = 0;
    if (state->cy < state->num_rows) {
        state->rx = pleditor_cx_to_rx(&state->rows[state->cy], state->cx);
    }

    /* Vertical scrolling */
    if (state->cy < state->row_offset) {
        state->row_offset = state->cy;
    }
    if (state->cy >= state->row_offset + state->screen_rows) {
        state->row_offset = state->cy - state->screen_rows + 1;
    }

    /* Horizontal scrolling */
    if (state->rx < state->col_offset) {
        state->col_offset = state->rx;
    }
    if (state->rx >= state->col_offset + state->screen_cols) {
        state->col_offset = state->rx - state->screen_cols + 1;
    }
}

/* Draw a row of the editor */
void pleditor_draw_rows(pleditor_state *state, char *buffer, int *len) {
    int y;
    for (y = 0; y < state->screen_rows - 1; y++) {
        int filerow = y + state->row_offset;
        if (filerow >= state->num_rows) {
            /* Draw welcome message or tilde for empty lines */
            if (state->num_rows == 0 && y == state->screen_rows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "pleditor -- version %s", PLEDITOR_VERSION);
                if (welcomelen > state->screen_cols) welcomelen = state->screen_cols;

                /* Center the welcome message */
                int padding = (state->screen_cols - welcomelen) / 2;
                if (padding) {
                    *len += sprintf(buffer + *len, "~");
                    padding--;
                }
                while (padding--) *len += sprintf(buffer + *len, " ");

                *len += sprintf(buffer + *len, "%s", welcome);
            } else {
                *len += sprintf(buffer + *len, "~");
            }
        } else {
            /* Draw file content */
            int len_to_display = state->rows[filerow].render_size - state->col_offset;
            if (len_to_display < 0) len_to_display = 0;
            if (len_to_display > state->screen_cols) len_to_display = state->screen_cols;

            if (len_to_display > 0) {
                char *c = &state->rows[filerow].render[state->col_offset];
                unsigned char *hl = NULL;
                int current_color = -1;
                
                /* If this row has highlighting data */
                if (state->rows[filerow].hl) {
                    hl = state->rows[filerow].hl->hl;
                }
                
                for (int j = 0; j < len_to_display; j++) {
                    if (hl) {
                        int color = pleditor_syntax_color_to_vt100(hl[j]);
                        
                        if (color != current_color) {
                            current_color = color;
                            *len += sprintf(buffer + *len, "\x1b[%dm", color);
                        }
                    }
                    
                    buffer[(*len)++] = c[j];
                }
                
                /* Reset text color at end of line */
                *len += sprintf(buffer + *len, VT100_COLOR_RESET);
            }
        }

        /* Clear to end of line and add newline */
        *len += sprintf(buffer + *len, VT100_CLEAR_LINE "\r\n");
    }
}

/* Draw the status bar at the bottom of the screen */
void pleditor_draw_status_bar(pleditor_state *state, char *buffer, int *len) {
    /* Inverse video for status bar */
    *len += sprintf(buffer + *len, VT100_INVERSE);

    char status[80], rstatus[80];
    int status_len = snprintf(status, sizeof(status), " %.20s - %d lines %s",
                             state->filename ? state->filename : "[No Name]",
                             state->num_rows,
                             state->dirty ? "(modified)" : "");
    
    /* Add filetype information if available */
    char filetype[20] = "no ft";
    if (state->syntax) {
        snprintf(filetype, sizeof(filetype), "%s", state->syntax->filetype);
    }
    
    int rstatus_len = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d ",
                              filetype, state->cy + 1, state->num_rows);

    if (status_len > state->screen_cols) status_len = state->screen_cols;
    *len += sprintf(buffer + *len, "%s", status);

    while (status_len < state->screen_cols - rstatus_len) {
        *len += sprintf(buffer + *len, " ");
        status_len++;
    }

    *len += sprintf(buffer + *len, "%s", rstatus);

    /* Reset text formatting */
    *len += sprintf(buffer + *len, VT100_COLOR_RESET "\r\n");
}

/* Draw the message bar below the status bar */
void pleditor_draw_message_bar(pleditor_state *state, char *buffer, int *len) {
    /* Clear the message bar */
    *len += sprintf(buffer + *len, VT100_CLEAR_LINE);

    /* Show status message if it's recent enough */
    int msglen = strlen(state->status_msg);
    if (msglen > state->screen_cols) msglen = state->screen_cols;
    if (msglen && pleditor_platform_get_time() - state->status_msg_time < 5) {
        *len += sprintf(buffer + *len, "%s", state->status_msg);
    }
}

/* Update the entire screen */
void pleditor_refresh_screen(pleditor_state *state) {
    pleditor_scroll(state);

    /* Buffer to build screen update in (large enough for entire screen) */
    char *buffer = malloc(state->screen_rows * state->screen_cols * 10);
    int len = 0;

    /* Hide cursor during screen update to avoid flicker */
    len += sprintf(buffer, VT100_CURSOR_HIDE VT100_CURSOR_HOME);

    /* Draw rows of text */
    pleditor_draw_rows(state, buffer, &len);

    /* Draw status bar and message bar */
    pleditor_draw_status_bar(state, buffer, &len);
    pleditor_draw_message_bar(state, buffer, &len);

    /* Position cursor */
    char cursor_pos[32];
    cursor_position(state->cy - state->row_offset + 1,
                   state->rx - state->col_offset + 1,
                   cursor_pos);
    len += sprintf(buffer + len, "%s", cursor_pos);

    /* Show cursor */
    len += sprintf(buffer + len, VT100_CURSOR_SHOW);

    /* Write buffer to terminal */
    pleditor_platform_write(buffer, len);
    free(buffer);
}

/* Set a status message to display in the message bar */
void pleditor_set_status_message(pleditor_state *state, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(state->status_msg, sizeof(state->status_msg), fmt, ap);
    va_end(ap);
    state->status_msg_time = pleditor_platform_get_time();
}

/* Move the cursor based on key press */
void pleditor_move_cursor(pleditor_state *state, int key) {
    pleditor_row *row = (state->cy >= state->num_rows) ? NULL : &state->rows[state->cy];

    switch (key) {
        case PLEDITOR_ARROW_LEFT:
            if (state->cx > 0) {
                state->cx--;
            } else if (state->cy > 0) {
                /* Move to end of previous line */
                state->cy--;
                state->cx = state->rows[state->cy].size;
            }
            break;

        case PLEDITOR_ARROW_RIGHT:
            if (row && state->cx < row->size) {
                state->cx++;
            } else if (row && state->cx == row->size) {
                /* Move to beginning of next line */
                state->cy++;
                state->cx = 0;
            }
            break;

        case PLEDITOR_ARROW_UP:
            if (state->cy > 0) state->cy--;
            break;

        case PLEDITOR_ARROW_DOWN:
            if (state->cy < state->num_rows) state->cy++;
            break;
    }

    /* Snap cursor to end of line if it's beyond line end */
    row = (state->cy >= state->num_rows) ? NULL : &state->rows[state->cy];
    int rowlen = row ? row->size : 0;
    if (state->cx > rowlen) {
        state->cx = rowlen;
    }
}

/* Save the current file */
void pleditor_save(pleditor_state *state) {
    if (state->filename == NULL) {
        pleditor_set_status_message(state, "Cannot save: no filename");
        return;
    }

    /* Create a single string of entire file */
    int totlen = 0;
    int i;
    for (i = 0; i < state->num_rows; i++)
        totlen += state->rows[i].size + 1; /* +1 for newline */

    char *buf = malloc(totlen + 1);
    char *p = buf;
    for (i = 0; i < state->num_rows; i++) {
        memcpy(p, state->rows[i].chars, state->rows[i].size);
        p += state->rows[i].size;
        *p = '\n';
        p++;
    }

    /* Write to file */
    if (pleditor_platform_write_file(state->filename, buf, totlen)) {
        state->dirty = false;
        pleditor_set_status_message(state, "%d bytes written to disk", totlen);
    } else {
        pleditor_set_status_message(state, "Can't save! I/O error: %s", strerror(errno));
    }

    free(buf);
}

/* Process a keypress */
void pleditor_process_keypress(pleditor_state *state, int c) {
    static int quit_times = PLEDITOR_QUIT_CONFIRM_TIMES;

    switch (c) {
        case PLEDITOR_CTRL_KEY('q'):
            if (state->dirty && quit_times > 0) {
                pleditor_set_status_message(state,
                    "WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            /* Clear screen and reposition cursor before exit */
            pleditor_platform_write(VT100_CLEAR_SCREEN VT100_CURSOR_HOME, 6);
            exit(0);
            break;

        case PLEDITOR_CTRL_KEY('s'):
            pleditor_save(state);
            break;

        case PLEDITOR_KEY_BACKSPACE:
        case PLEDITOR_CTRL_KEY('h'):
            pleditor_delete_char(state);
            break;

        case PLEDITOR_DEL_KEY:
            pleditor_move_cursor(state, PLEDITOR_ARROW_RIGHT);
            pleditor_delete_char(state);
            break;

        case '\r':
        case '\n':
            pleditor_insert_newline(state);
            break;

        case PLEDITOR_CTRL_KEY('l'):
        case PLEDITOR_KEY_ESC:
            /* Just refresh screen */
            break;

        case PLEDITOR_ARROW_UP:
        case PLEDITOR_ARROW_DOWN:
        case PLEDITOR_ARROW_LEFT:
        case PLEDITOR_ARROW_RIGHT:
            pleditor_move_cursor(state, c);
            break;

        case PLEDITOR_HOME_KEY:
            state->cx = 0;
            break;

        case PLEDITOR_END_KEY:
            if (state->cy < state->num_rows)
                state->cx = state->rows[state->cy].size;
            break;

        case PLEDITOR_PAGE_UP:
        case PLEDITOR_PAGE_DOWN:
            {
                if (c == PLEDITOR_PAGE_UP) {
                    state->cy = state->row_offset;
                } else if (c == PLEDITOR_PAGE_DOWN) {
                    state->cy = state->row_offset + state->screen_rows - 1;
                    if (state->cy > state->num_rows) state->cy = state->num_rows;
                }

                int times = state->screen_rows;
                while (times--)
                    pleditor_move_cursor(state,
                                      (c == PLEDITOR_PAGE_UP) ?
                                      PLEDITOR_ARROW_UP :
                                      PLEDITOR_ARROW_DOWN);
            }
            break;

        default:
            pleditor_insert_char(state, c);
            break;
    }

    quit_times = PLEDITOR_QUIT_CONFIRM_TIMES;
}

/* Initialize the editor state */
void pleditor_init(pleditor_state *state) {
    state->cx = 0;
    state->cy = 0;
    state->rx = 0;
    state->row_offset = 0;
    state->col_offset = 0;
    state->num_rows = 0;
    state->rows = NULL;
    state->dirty = false;
    state->filename = NULL;
    state->status_msg[0] = '\0';
    state->status_msg_time = 0;
    state->syntax = NULL;  /* No syntax highlighting by default */
    
    if (!pleditor_platform_get_window_size(&state->screen_rows, &state->screen_cols)) {
        /* Fallback if window size detection fails */
        state->screen_rows = 24;
        state->screen_cols = 80;
    }
    
    /* Leave room for status line and message bar */
    state->screen_rows -= 2;
}

/* Open a file in the editor */
void pleditor_open(pleditor_state *state, const char *filename) {
    free(state->filename);
    state->filename = strdup(filename);
    
    char *buffer;
    size_t len;
    
    if (!pleditor_platform_read_file(filename, &buffer, &len)) {
        pleditor_set_status_message(state, "New file: %s", filename);
        return;
    }
    
    /* Parse the file contents into rows */
    char *line = buffer;
    char *end = buffer + len;
    char *eol;
    
    while (line < end) {
        /* Find the end of the current line */
        eol = strchr(line, '\n');
        
        size_t line_length;
        if (eol) {
            line_length = eol - line;
            eol++; /* Skip newline */
        } else {
            line_length = end - line;
            eol = end;
        }
        
        /* Add the line to our rows */
        pleditor_insert_row(state, state->num_rows, line, line_length);
        
        line = eol;
    }
    
    free(buffer);
    state->dirty = false;
    
    /* Select syntax highlighting based on filename */
    extern void pleditor_syntax_select_by_filename(pleditor_state *state, const char *filename);
    pleditor_syntax_select_by_filename(state, filename);
    
    /* Apply syntax highlighting to all rows */
    extern void pleditor_syntax_update_all(pleditor_state *state);
    pleditor_syntax_update_all(state);
}

/* Free editor resources */
void pleditor_free(pleditor_state *state) {
    for (int i = 0; i < state->num_rows; i++) {
        pleditor_free_row(&state->rows[i]);
    }
    free(state->rows);
    free(state->filename);
}
