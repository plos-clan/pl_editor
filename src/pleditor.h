/**
 * pleditor.h - Platform-independent core for PL Editor
 */
#ifndef PLEDITOR_H
#define PLEDITOR_H

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Editor config */
#define PLEDITOR_VERSION "0.1.0"
#define PLEDITOR_TAB_STOP 4
#define PLEDITOR_QUIT_CONFIRM_TIMES 3

/* Key definitions */
#define PLEDITOR_CTRL_KEY(k) ((k) & 0x1f)
#define PLEDITOR_KEY_ESC '\x1b'
#define PLEDITOR_KEY_BACKSPACE 127

/* Special key codes */
enum pleditor_key {
    PLEDITOR_ARROW_LEFT = 1000,
    PLEDITOR_ARROW_RIGHT,
    PLEDITOR_ARROW_UP,
    PLEDITOR_ARROW_DOWN,
    PLEDITOR_PAGE_UP,
    PLEDITOR_PAGE_DOWN,
    PLEDITOR_HOME_KEY,
    PLEDITOR_END_KEY,
    PLEDITOR_DEL_KEY
};

/* Highlight types */
enum pleditor_highlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MULTILINE_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH       /* For search */
};

/* Data structure for highlighting in a row */
typedef struct pleditor_highlight_row {
    unsigned char *hl;      /* Highlighting for each character */
    bool hl_multiline_comment;  /* Is this row part of a multi-line comment */
} pleditor_highlight_row;

/* Syntax definition structure */
typedef struct pleditor_syntax {
    char *filetype;         /* Language/filetype name */
    char **filematch;       /* File patterns that match this syntax */
    char **keywords;        /* Keywords for the language */
    char *singleline_comment_start;  /* Single line comment start */
    char *multiline_comment_start;   /* Multi-line comment start */
    char *multiline_comment_end;     /* Multi-line comment end */
    bool flags;             /* Syntax flags */
} pleditor_syntax;

/* Row of text in the editor */
typedef struct pleditor_row {
    int size;                /* Size of the text */
    char *chars;            /* Raw text content */
    int render_size;        /* Size of the rendered text */
    char *render;           /* Rendered text (with tab expansion) */
    pleditor_highlight_row *hl; /* Syntax highlighting for this row */
} pleditor_row;

/* Editor state */
typedef struct pleditor_state {
    int cx, cy;             /* Cursor position */
    int rx;                 /* Render X position (for tabs) */
    int row_offset;         /* Row scroll offset */
    int col_offset;         /* Column scroll offset */
    int screen_rows;        /* Number of visible rows */
    int screen_cols;        /* Number of visible columns */
    int num_rows;           /* Number of rows in file */
    pleditor_row *rows;     /* File content */
    bool dirty;             /* File has unsaved changes */
    char *filename;         /* Currently open filename */
    char status_msg[80];    /* Status message */
    time_t status_msg_time; /* Time when status was set */
    pleditor_syntax *syntax; /* Current syntax highlighting */
} pleditor_state;

/* Function prototypes */
void pleditor_init(pleditor_state *state);
void pleditor_free(pleditor_state *state);
void pleditor_open(pleditor_state *state, const char *filename);
void pleditor_save(pleditor_state *state);

void pleditor_insert_char(pleditor_state *state, int c);
void pleditor_delete_char(pleditor_state *state);
void pleditor_insert_newline(pleditor_state *state);

void pleditor_refresh_screen(pleditor_state *state);
void pleditor_set_status_message(pleditor_state *state, const char *fmt, ...);
void pleditor_move_cursor(pleditor_state *state, int key);
void pleditor_process_keypress(pleditor_state *state, int c);

/* Syntax highlighting functions */
void pleditor_syntax_select_by_filename(pleditor_state *state, const char *filename);
void pleditor_syntax_update_row(pleditor_state *state, int row_idx);
int pleditor_syntax_color_to_vt100(int hl);
bool pleditor_syntax_init(pleditor_state *state);

#endif /* PLEDITOR_H */