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
#include "syntax.h"

/* For calculating the render index from a chars index */
int pleditor_cx_to_rx(pleditor_row *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (PLEDITOR_TAB_STOP - 1) - (rx % PLEDITOR_TAB_STOP);
        rx++;
    }
    return rx;
}

/* Update the render string for a row (for handling tabs, etc.) */
void pleditor_update_row(pleditor_row *row) {
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(PLEDITOR_TAB_STOP - 1) + 1);
    if (row->render == NULL) exit(1);

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
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

    /* Record the operation for undo - store the character being inserted */
    pleditor_undo_params params = {
        .type = UNDO_INSERT_CHAR,
        .cx = state->cx,
        .cy = state->cy,
        .character = c,
        .line = NULL,
        .line_size = 0
    };
    pleditor_push_undo(state, &params);

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
    /* Before inserting a newline, we save information about the current row for undo */
    if (state->cy < state->num_rows) {
        pleditor_row *row = &state->rows[state->cy];

        /* Store a copy of the entire row for undoing properly */
        pleditor_undo_params params = {
            .type = UNDO_INSERT_LINE,
            .cx = state->cx,
            .cy = state->cy,
            .character = 0,
            .line = row->chars,
            .line_size = row->size
        };
        pleditor_push_undo(state, &params);
    } else {
        /* If we're at the end of the file, just record the position */
        pleditor_undo_params params = {
            .type = UNDO_INSERT_LINE,
            .cx = state->cx,
            .cy = state->cy,
            .character = 0,
            .line = NULL,
            .line_size = 0
        };
        pleditor_push_undo(state, &params);
    }

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
        /* Record the operation for undo - save the character that will be deleted */
        int del_char = row->chars[state->cx - 1];
        pleditor_undo_params params = {
            .type = UNDO_DELETE_CHAR,
            .cx = state->cx - 1,
            .cy = state->cy,
            .character = del_char,
            .line = NULL,
            .line_size = 0
        };
        pleditor_push_undo(state, &params);

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
        /* At start of line or DEL at end of previous line */
        pleditor_row *prev_row = &state->rows[state->cy - 1];

        /* Save the line for undo */
        pleditor_undo_params params = {
            .type = UNDO_DELETE_LINE,
            .cx = prev_row->size,  /* Store previous row's end position */
            .cy = state->cy,
            .character = 0,
            .line = row->chars,
            .line_size = row->size
        };
        pleditor_push_undo(state, &params);

        /* Adjust cursor position for undo to beginning of previous row */
        state->cx = prev_row->size;

        /* Now merge the lines */
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

/* Calculate the number of digits in a given number */
int digit_count(int number) {
    if (number <= 0) return 1; /* Handle 0 and negative cases */

    int count = 0;
    while (number > 0) {
        number /= 10;
        count++;
    }
    return count;
}

/* Calculate the width needed for line numbers (digits + space) */
int pleditor_get_line_number_width(pleditor_state *state) {
    if (!state->show_line_numbers) {
        return 0;
    }

    /* Calculate the maximum visible line number on screen */
    int max_visible_line = state->row_offset + state->screen_rows;
    if (max_visible_line > state->num_rows) {
        max_visible_line = state->num_rows;
    }

    /* At least show single digit + space */
    int line_number_width = digit_count(max_visible_line) + 1;

    /* Ensure minimum of 2 characters wide + space */
    if (line_number_width < 3) {
        line_number_width = 3;
    }

    return line_number_width;
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
    /* Calculate the effective screen width available for text,
     * accounting for line number display width if enabled */
    int effective_screen_width = state->screen_cols;
    if (state->show_line_numbers) {
        effective_screen_width -= pleditor_get_line_number_width(state);
    }

    if (state->rx < state->col_offset) {
        state->col_offset = state->rx;
    }
    if (state->rx >= state->col_offset + effective_screen_width) {
        state->col_offset = state->rx - effective_screen_width + 1;
    }
}

/* Draw a row of the editor */
void pleditor_draw_rows(pleditor_state *state, char *buffer, int *len) {
    for (int y = 0; y < state->screen_rows; y++) {
        int filerow = y + state->row_offset;

        /* Draw line numbers if enabled */
        if (state->show_line_numbers) {
            int line_number_width = pleditor_get_line_number_width(state);
            int digits = line_number_width - 1; /* Subtract space */

            /* Check if this is a valid file line */
            bool is_file_line = filerow < state->num_rows;
            bool is_current_line = filerow == state->cy;

            /* Either it's an existing file line or it's the empty line right after
               the file that's currently being edited */
            if (is_file_line) {
                if (is_current_line) {
                    /* White text for current line */
                    *len += sprintf(buffer + *len, VT100_COLOR_WHITE);
                } else {
                    /* Light gray text for other lines */
                    *len += sprintf(buffer + *len, VT100_COLOR_DARK_GRAY);
                }

                /* Format line number with correct padding */
                char format[20];
                sprintf(format, "%%%dd ", digits);
                *len += sprintf(buffer + *len, format, filerow + 1);

                *len += sprintf(buffer + *len, VT100_COLOR_RESET);
            } else {
                /* Create padding with correct width */
                char padding[20] = "";
                for (int i = 0; i < line_number_width; i++) {
                    strcat(padding, " ");
                }
                *len += sprintf(buffer + *len, "%s", padding);

                *len += sprintf(buffer + *len, VT100_COLOR_RESET);
            }
        }

        if (filerow >= state->num_rows) {
            /* Draw welcome message or tilde for empty lines */
            if (state->num_rows == 0 && y == state->screen_rows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "pleditor -- version %s", PLEDITOR_VERSION);
                if (welcomelen > state->screen_cols) welcomelen = state->screen_cols;

                /* Center the welcome message */
                int available_width = state->screen_cols;
                if (state->show_line_numbers) {
                    available_width -= pleditor_get_line_number_width(state);
                }
                int padding = (available_width - welcomelen) / 2;
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
            /* Calculate available width for text after accounting for line numbers */
            int available_width = state->screen_cols;
            if (state->show_line_numbers) {
                available_width -= pleditor_get_line_number_width(state);
            }

            int len_to_display = state->rows[filerow].render_size - state->col_offset;
            if (len_to_display < 0) len_to_display = 0;
            if (len_to_display > available_width) len_to_display = available_width;

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
                        int color = pleditor_syntax_color_to_ansi(hl[j]);

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

/* Truncate long file paths with ellipsis at the beginning */
char* pleditor_truncated_path(const char* path, size_t max_len) {
    if (!path) return NULL;

    size_t path_len = strlen(path);
    static char truncated[80]; /* Static buffer for the truncated path */

    if (path_len <= max_len) {
        /* Path is short enough, return as is */
        strcpy(truncated, path);
    } else {
        /* Path is too long, truncate with ellipsis */
        strcpy(truncated, "...");
        strcat(truncated, path + (path_len - (max_len - 3)));
    }

    return truncated;
}

/* Draw the status bar at the bottom of the screen */
void pleditor_draw_status_bar(pleditor_state *state, char *buffer, int *len) {
    /* Inverse video for status bar */
    *len += sprintf(buffer + *len, VT100_INVERSE);

    char status[80], rstatus[80];
    char* display_filename = state->filename ?
                             pleditor_truncated_path(state->filename, 30) :
                             "[No Name]";
    int status_len = snprintf(status, sizeof(status), "%s - %d lines %s",
                             display_filename,
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

    /* Show status message if it exists */
    int msglen = strlen(state->status_msg);
    if (msglen > state->screen_cols) msglen = state->screen_cols;
    if (msglen) {
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
    int cursor_screen_x = state->rx - state->col_offset + 1;

    /* Add offset for line numbers if enabled */
    if (state->show_line_numbers) {
        cursor_screen_x += pleditor_get_line_number_width(state);
    }

    cursor_position(state->cy - state->row_offset + 1, cursor_screen_x, cursor_pos);
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
}

/* Display a prompt in the status bar and get a input */
char* pleditor_prompt(pleditor_state *state, const char *prompt) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    if (!buf) return NULL;

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        /* Display the prompt and current input */
        pleditor_set_status_message(state, "%s: %s", prompt, buf);
        pleditor_refresh_screen(state);

        int c = pleditor_platform_read_key();

        if (c == PLEDITOR_DEL_KEY || c == PLEDITOR_KEY_BACKSPACE) {
            /* Handle backspace/delete */
            if (buflen > 0) {
                buflen--;
                buf[buflen] = '\0';
            }
        } else if (c == '\r' || c == '\n') {
            /* Handle Enter/Return */
            if (buflen != 0) {
                pleditor_set_status_message(state, "");
                return buf;
            }
        } else if (c == PLEDITOR_KEY_ESC || c == PLEDITOR_CTRL_KEY('q')) {
            /* Handle escape or quit */
            pleditor_set_status_message(state, "");
            free(buf);
            return NULL;
        } else if (!iscntrl(c) && c < 128) {
            /* Append character to buffer */
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                char *newbuf = realloc(buf, bufsize);
                if (!newbuf) {
                    free(buf);
                    return NULL;
                }
                buf = newbuf;
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
    }
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
            if (state->cy < state->num_rows - 1) state->cy++;
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
    /* If no filename set, prompt the user for one */
    if (state->filename == NULL) {
        char *filename = pleditor_prompt(state, "Save as");
        if (filename == NULL) {
            pleditor_set_status_message(state, "Save aborted");
            return;
        }
        free(state->filename);
        state->filename = filename;

        /* Select syntax highlighting based on new filename */
        pleditor_syntax_by_name(state, state->filename);

        /* Apply highlighting if a syntax was selected */
        if (state->syntax) {
            pleditor_syntax_update_all(state);
        }
    }

    /* Create a single string of entire file */
    int totlen = 0;
    for (int i = 0; i < state->num_rows; i++)
        totlen += state->rows[i].size + 1; /* +1 for newline */

    char *buf = malloc(totlen + 1);
    char *p = buf;
    for (int i = 0; i < state->num_rows; i++) {
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

    /* Clear status message on any keypress unless we're confirming quit */
    if (!(c == PLEDITOR_CTRL_KEY('q') && state->dirty && quit_times > 0)) {
        pleditor_set_status_message(state, "");
    }

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

        case PLEDITOR_CTRL_KEY('r'):
            state->show_line_numbers = !state->show_line_numbers;
            /* Update status message to show current line number state */
            pleditor_set_status_message(state, "Line numbers: %s",
                                     state->show_line_numbers ? "ON" : "OFF");
            break;

        case PLEDITOR_CTRL_KEY('z'):
            pleditor_perform_undo(state);
            break;

        case PLEDITOR_CTRL_KEY('y'):
            pleditor_perform_redo(state);
            break;

        case PLEDITOR_KEY_BACKSPACE:
        case PLEDITOR_CTRL_KEY('h'):
            pleditor_delete_char(state);
            break;

        case PLEDITOR_DEL_KEY:
            /* If we're at the end of the document, do nothing */
            if (state->num_rows == 0 ||
                (state->cy == state->num_rows - 1 &&
                 state->cx == state->rows[state->cy].size)) {
                break;
            }
            /* Store original cursor position before moving right */
            int orig_cx = state->cx;
            int orig_cy = state->cy;

            pleditor_move_cursor(state, PLEDITOR_ARROW_RIGHT);
            pleditor_delete_char(state);

            /* Update the undo operation with the original cursor position
             * and mark the position by making cx negative to signal it was a DEL operation */
            if (state->undo_stack && state->undo_stack->type == UNDO_DELETE_CHAR) {
                state->undo_stack->cx = -orig_cx - 1; /* Store as negative to mark DEL op */
                state->undo_stack->cy = orig_cy;
            }
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
                    if (state->cy > state->num_rows - 1) state->cy = state->num_rows - 1;
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
    state->syntax = NULL;  /* No syntax highlighting by default */
    state->show_line_numbers = true; /* Line numbers enabled by default */
    state->undo_stack = NULL; /* Initialize the undo stack */
    state->redo_stack = NULL; /* Initialize the redo stack */
    state->is_unredoing = false; /* Initialize unredoing flag */

    if (!pleditor_platform_get_size(&state->screen_rows, &state->screen_cols)) {
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

    /* Use malloc and strcpy instead of strdup */
    state->filename = malloc(strlen(filename) + 1);
    if (state->filename == NULL) exit(1);
    strcpy(state->filename, filename);

    char *buffer;
    size_t len;

    if (!pleditor_platform_read_file(filename, &buffer, &len)) {
        pleditor_set_status_message(state, "New file: %s", filename);

        /* Select syntax highlighting based on filename */
        pleditor_syntax_by_name(state, filename);

        /* No rows to highlight yet */
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
    pleditor_syntax_by_name(state, filename);

    /* Apply syntax highlighting to all rows */
    pleditor_syntax_update_all(state);
}

/* Free editor resources */
void pleditor_free(pleditor_state *state) {
    for (int i = 0; i < state->num_rows; i++) {
        pleditor_free_row(&state->rows[i]);
    }
    free(state->rows);
    free(state->filename);
    pleditor_free_unredo_stack(&state->undo_stack);
    pleditor_free_unredo_stack(&state->redo_stack);
}

void pleditor_push_undo(pleditor_state *state, const pleditor_undo_params *params) {
    /* Don't record undo operations when undoing or redoing */
    if (state->is_unredoing) return;

    /* Clear redo stack when a new edit is made */
    pleditor_free_unredo_stack(&state->redo_stack);

    pleditor_undo_operation *op = malloc(sizeof(pleditor_undo_operation));
    if (!op) return;

    op->type = params->type;
    op->cx = params->cx;
    op->cy = params->cy;
    op->character = params->character;
    op->line = NULL;
    op->line_size = params->line_size;

    if (params->line) {
        op->line = malloc(params->line_size + 1);
        if (op->line) {
            if (params->line_size > 0) {
                memcpy(op->line, params->line, params->line_size);
            }
            op->line[params->line_size] = '\0';
        }
    }

    op->next = state->undo_stack;
    state->undo_stack = op;
}

void pleditor_free_unredo_stack(pleditor_undo_operation **stack) {
    pleditor_undo_operation *op = *stack;
    while (op) {
        pleditor_undo_operation *next = op->next;
        if (op->line) free(op->line);
        free(op);
        op = next;
    }
    *stack = NULL;
}

void pleditor_perform_undo(pleditor_state *state) {
    if (!state->undo_stack) {
        pleditor_set_status_message(state, "Nothing to undo");
        return;
    }

    /* Set flag to prevent recording operations while undoing */
    state->is_unredoing = true;

    pleditor_undo_operation *op = state->undo_stack;
    state->undo_stack = op->next;

    /* Save this operation to the redo stack */
    pleditor_undo_operation *redo_op = malloc(sizeof(pleditor_undo_operation));

    if (redo_op) {
        redo_op->type = op->type;
        redo_op->cx = op->cx;
        redo_op->cy = op->cy;
        redo_op->character = op->character;
        redo_op->line = NULL;
        redo_op->line_size = op->line_size;

        if (op->line && op->line_size > 0) {
            redo_op->line = malloc(op->line_size + 1);
            if (redo_op->line) {
                memcpy(redo_op->line, op->line, op->line_size);
                redo_op->line[op->line_size] = '\0';
            }
        }

        redo_op->next = state->redo_stack;
        state->redo_stack = redo_op;
    }

    switch (op->type) {
        case UNDO_INSERT_CHAR:
            /* For insert char, we need to delete the character that was inserted */
            state->cx = op->cx;
            state->cy = op->cy;
            if (state->cy < state->num_rows) {
                pleditor_row *row = &state->rows[state->cy];
                if (state->cx < row->size) {
                    /* Get the character for redo before deleting it */
                    redo_op->character = row->chars[state->cx];

                    /* Delete the character at the cursor position without pushing to undo stack again */
                    memmove(&row->chars[state->cx], &row->chars[state->cx + 1], row->size - state->cx);
                    row->size--;
                    pleditor_update_row(row);
                    state->dirty = true;
                    if (state->syntax) {
                        pleditor_syntax_update_row(state, state->cy);
                    }
                }
            }
            break;

        case UNDO_DELETE_CHAR:
            /* For delete char, we need to re-insert the character */
            bool is_del_operation = false;

            /* Check if this was a DEL operation (marked by negative cx) */
            if (op->cx < 0) {
                is_del_operation = true;
                state->cx = -op->cx - 1; /* Extract the original position */
            } else {
                state->cx = op->cx;
            }
            state->cy = op->cy;

            if (op->character != 0) {
                /* Insert character without pushing to undo stack again */
                if (state->cy == state->num_rows) {
                    pleditor_insert_row(state, state->num_rows, "", 0);
                }

                pleditor_row *row = &state->rows[state->cy];
                row->chars = realloc(row->chars, row->size + 2);
                memmove(&row->chars[state->cx + 1], &row->chars[state->cx], row->size - state->cx + 1);
                row->size++;
                row->chars[state->cx] = op->character;
                pleditor_update_row(row);

                if (state->syntax) {
                    pleditor_syntax_update_row(state, state->cy);
                }

                /* Only increment cursor for backspace, not for DEL */
                if (!is_del_operation) {
                    state->cx++;
                }
                state->dirty = true;
            }
            break;

        case UNDO_INSERT_LINE:
            /* For insert line, we need to properly merge the split lines back */
            if (state->cy < state->num_rows) {
                /* Set cursor to the row that had the Enter key pressed */
                state->cy = op->cy;

                /* When we have the original line data from before the split */
                if (op->line) {
                    /* First handle the case of a proper newline (not at the beginning of a line) */
                    if (op->cx > 0) {
                        /* Delete the current row that was split */
                        pleditor_delete_row(state, state->cy);

                        /* Insert the original line content that was saved before splitting */
                        pleditor_insert_row(state, state->cy, op->line, op->line_size);

                        /* Delete any content from the next line that would be duplicate */
                        if ((state->cy + 1) < state->num_rows) {
                            /* Delete the next line (which was created by the Enter key) */
                            pleditor_delete_row(state, state->cy + 1);
                        }
                    } else {
                        /* For newline at the beginning of a line, just remove the empty line */
                        pleditor_delete_row(state, state->cy);
                    }

                    /* Update the dirty flag since we've modified the content */
                    state->dirty = true;
                } else {
                    /* For simple empty line insertion, just delete the row */
                    pleditor_delete_row(state, state->cy);
                    state->dirty = true;
                }

                /* Set cursor to the position before the newline was inserted */
                state->cx = op->cx;
            }
            break;

        case UNDO_DELETE_LINE:
            /* Only break if the line pointer is NULL */
            if (!op->line) break;

            /* Handle special case for Delete at end of line */
            if (op->cy > 0 && op->cy <= state->num_rows) {
                pleditor_row *prev_row = &state->rows[op->cy - 1];

                /* Check if this is a line joining operation (DEL at line end) */
                if (op->cx > 0) {
                    /* Truncate previous line to remove second line content */
                    prev_row->chars[op->cx] = '\0';
                    prev_row->size = op->cx;
                    pleditor_update_row(prev_row);

                    if (state->syntax) {
                        pleditor_syntax_update_row(state, op->cy - 1);
                    }
                } else {
                    /* Original backspace at line start case */
                    int match_start = prev_row->size - op->line_size;

                    /* Check if previous line ends with deleted line content */
                    if (prev_row->size >= op->line_size &&
                        memcmp(&prev_row->chars[match_start], op->line, op->line_size) == 0) {
                            /* Truncate previous line */
                            prev_row->chars[match_start] = '\0';
                            prev_row->size = match_start;
                            pleditor_update_row(prev_row);

                            if (state->syntax) {
                                pleditor_syntax_update_row(state, op->cy - 1);
                            }
                        }
                }
            }

            /* Re-insert the deleted line */
            pleditor_insert_row(state, op->cy, op->line, op->line_size);

            /* Position cursor at end of previous line for DEL at end of line case */
            if (op->cy > 0) {
                state->cy = op->cy - 1;
                state->cx = op->cx;
            } else {
                state->cy = op->cy;
                state->cx = 0;
            }

            state->dirty = true;
            break;
        }

    /* Clear the unredoing flag */
    state->is_unredoing = false;

    /* Free the undo operation */
    if (op->line) free(op->line);
    free(op);

    pleditor_set_status_message(state, "Undo successful");
}

void pleditor_perform_redo(pleditor_state *state) {
    if (!state->redo_stack) {
        pleditor_set_status_message(state, "Nothing to redo");
        return;
    }

    /* Set flag to prevent recording operations while redoing */
    state->is_unredoing = true;

    pleditor_undo_operation *op = state->redo_stack;
    state->redo_stack = op->next;

    /* Save this operation to the undo stack */
    pleditor_undo_operation *undo_op = malloc(sizeof(pleditor_undo_operation));

    if (undo_op) {
        undo_op->type = op->type;
        undo_op->cx = op->cx;
        undo_op->cy = op->cy;
        undo_op->character = op->character;
        undo_op->line = NULL;
        undo_op->line_size = op->line_size;

        if (op->line) {
            undo_op->line = malloc(op->line_size + 1);
            if (undo_op->line) {
                if (op->line_size > 0) {
                    memcpy(undo_op->line, op->line, op->line_size);
                }
                undo_op->line[op->line_size] = '\0';
            }
        }

        undo_op->next = state->undo_stack;
        state->undo_stack = undo_op;
    }

    switch (op->type) {
        case UNDO_INSERT_CHAR:
            /* For redo of insert, we need to re-insert the character */
            state->cx = op->cx;
            state->cy = op->cy;

            /* Insert character without pushing to undo stack again */
            if (state->cy == state->num_rows) {
                pleditor_insert_row(state, state->num_rows, "", 0);
            }

            if (state->cy < state->num_rows) {
                pleditor_row *row = &state->rows[state->cy];
                row->chars = realloc(row->chars, row->size + 2);
                memmove(&row->chars[state->cx + 1], &row->chars[state->cx], row->size - state->cx + 1);
                row->size++;
                row->chars[state->cx] = op->character;
                pleditor_update_row(row);

                if (state->syntax) {
                    pleditor_syntax_update_row(state, state->cy);
                }

                state->cx++;
                state->dirty = true;
            }
            break;

        case UNDO_DELETE_CHAR:
            /* For redo of delete, we need to delete the character again */
            bool is_del_operation = false;

            /* Check if this was a DEL operation (marked by negative cx) */
            if (op->cx < 0) {
                is_del_operation = true;
                state->cx = -op->cx - 1; /* Extract the original position */
            } else {
                state->cx = op->cx;
            }
            state->cy = op->cy;

            if (is_del_operation) {
                /* For DEL operation, simulate DEL key press */
                /* Move cursor right, then delete */
                pleditor_move_cursor(state, PLEDITOR_ARROW_RIGHT);
                pleditor_delete_char(state);
            } else if (state->cy < state->num_rows) {
                pleditor_row *row = &state->rows[state->cy];
                if (state->cx == 0 && state->cy > 0) {
                    /* This is a line join operation (deletion at beginning of line) */
                    /* Move cursor to the end of previous line where characters will be joined */
                    pleditor_row *prev_row = &state->rows[state->cy - 1];
                    int prev_row_size = prev_row->size;

                    /* Perform the delete character operation which will join the lines */
                    pleditor_delete_char(state);

                    /* Ensure cursor is at the join point */
                    state->cy = op->cy - 1;
                    state->cx = prev_row_size;
                } else if (state->cx < row->size) {
                    /* Delete the character at the cursor position */
                    memmove(&row->chars[state->cx], &row->chars[state->cx + 1], row->size - state->cx);
                    row->size--;
                    pleditor_update_row(row);
                    state->dirty = true;
                    if (state->syntax) {
                        pleditor_syntax_update_row(state, state->cy);
                    }
                }
            }
            break;

        case UNDO_INSERT_LINE:
            /* For redo of line insert, we need to re-insert the newline */
            state->cx = op->cx;
            state->cy = op->cy;

            if (state->cy < state->num_rows) {
                pleditor_row *row = &state->rows[state->cy];

                if (op->cx == 0) {
                    /* Insert an empty line before the current line */
                    pleditor_insert_row(state, state->cy, "", 0);
                    state->cy++;
                } else if (op->cx <= row->size) {
                    /* Split the line at cursor position */
                    char *second_half = &row->chars[op->cx];
                    int second_half_len = row->size - op->cx;

                    /* Insert new line with second half content */
                    pleditor_insert_row(state, state->cy + 1, second_half, second_half_len);

                    /* Truncate current line */
                    row->size = op->cx;
                    row->chars[op->cx] = '\0';
                    pleditor_update_row(row);

                    if (state->syntax) {
                        pleditor_syntax_update_row(state, state->cy);
                        pleditor_syntax_update_row(state, state->cy + 1);
                    }

                    /* Move cursor to beginning of next line */
                    state->cy++;
                    state->cx = 0;
                }

                state->dirty = true;
            }
            break;

        case UNDO_DELETE_LINE:
            /* For redo of line delete, we need to delete the line again */
            state->cx = op->cx;
            state->cy = op->cy;

            if (state->cy < state->num_rows) {
                /* Update the undo operation to save the current line content */
                if (undo_op) {
                    pleditor_row *current_row = &state->rows[state->cy];
                    undo_op->line_size = current_row->size;
                    
                    /* Allocate and save the line content for proper undo */
                    if (undo_op->line) {
                        free(undo_op->line);
                        undo_op->line = NULL;
                    }
                    
                    undo_op->line = malloc(current_row->size + 1);
                    if (undo_op->line) {
                        if (current_row->size > 0) {
                            memcpy(undo_op->line, current_row->chars, current_row->size);
                        }
                        undo_op->line[current_row->size] = '\0';
                    }
                }
                
                /* Check if this is a DEL key operation at the end of the previous line */
                if (state->cy > 0 && op->line) {
                    /* This is a DEL key at end of line case */
                    pleditor_row *prev_row = &state->rows[state->cy - 1];
                    int join_point = prev_row->size;

                    /* Merge the content with the previous line */
                    prev_row->chars = realloc(prev_row->chars, prev_row->size + op->line_size + 1);
                    if (op->line_size > 0) {
                        memcpy(&prev_row->chars[prev_row->size], op->line, op->line_size);
                    }
                    prev_row->size += op->line_size;
                    prev_row->chars[prev_row->size] = '\0';
                    pleditor_update_row(prev_row);

                    /* Update syntax highlighting for the modified row */
                    if (state->syntax) {
                        pleditor_syntax_update_row(state, state->cy - 1);
                    }

                    /* Delete the line */
                    pleditor_delete_row(state, state->cy);

                    /* Position cursor at the join point */
                    state->cy--;
                    state->cx = join_point;
                } else {
                    /* Regular line delete */
                    pleditor_delete_row(state, state->cy);
                    
                    /* If this was a DEL at end of line, position cursor at end of previous line */
                    if (state->cy > 0 && op->cx > 0) {
                        state->cy--;
                        state->cx = op->cx;
                    }
                }
                state->dirty = true;
            }
            break;
    }

    /* Clear the redoing flag */
    state->is_unredoing = false;

    /* Free the redo operation */
    if (op->line) free(op->line);
    free(op);

    pleditor_set_status_message(state, "Redo successful");
}
