/**
 * syntax.c - Syntax highlighting implementation
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "syntax.h"
#include "pleditor.h"

/* C-like language keywords */
char *C_HL_keywords[] = {
    /* C keywords */
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "case", "#include",
    "#define", "#ifdef", "#ifndef", "#endif", "#pragma", "volatile",
    "register", "sizeof", "typedef", "const", "auto",

    /* Types - keyword2 */
    "int|" , "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "bool|", "short|", "size_t|", "uint8_t|", "uint16_t|", "uint32_t|",
    "uint64_t|", "int8_t|", "int16_t|", "int32_t|", "int64_t|", "FILE|", "time_t|",

    NULL
};

/* Lua language keywords */
char *LUA_HL_keywords[] = {
    /* Lua keywords */
    "function", "local", "if", "then", "else", "elseif", "end", "while",
    "do", "for", "repeat", "until", "break", "return", "in", "and", "or", "not",

    /* Lua built-in values */
    "true|", "false|", "nil|",

    /* Lua built-in functions */
    "print|", "pairs|", "ipairs|", "type|", "tonumber|", "tostring|", "require|",
    "table|", "string|", "math|", "os|", "io|", "coroutine|", "error|", "assert|",

    NULL
};

/* Python language keywords */
char *PYTHON_HL_keywords[] = {
    /* Python keywords */
    "def", "class", "if", "elif", "else", "while", "for", "in", "try",
    "except", "finally", "with", "as", "import", "from", "pass", "return",
    "break", "continue", "lambda", "yield", "global", "nonlocal", "assert",
    "raise", "del", "not", "and", "or", "is", "async", "await",

    /* Python built-in values */
    "True|", "False|", "None|",

    /* Python built-in functions */
    "print|", "len|", "int|", "str|", "float|", "list|", "dict|", "tuple|", "set|",
    "range|", "enumerate|", "sorted|", "sum|", "min|", "max|", "abs|", "open|",
    "type|", "id|", "input|", "format|", "zip|", "map|", "filter|", "any|", "all|",

    NULL
};

/* Riddle language keywords */
char *RIDDLE_HL_keywords[] = {
    /* Riddle keywords */
    "var", "val", "for", "while", "continue", "break", "if", "else", "fun",
    "return", "import", "package", "class", "try", "catch", "override",
    "static", "const", "public", "protected", "private", "virtual", "operator",

    /* Types - keyword2 */
    "int|" , "long|", "double|", "float|", "char|", "void|", "bool|", "short|",

    /* Riddle built-in values */
    "true|", "false|", "null|",

    NULL
};

/* Syntax definitions */
pleditor_syntax HLDB[] = {
    /* C-like language */
    {
        "c",
        (char*[]){"c", "h", "cpp", "hpp", "cc", "cxx", "c++", NULL},
        C_HL_keywords,
        "//",     /* Single line comment start */
        "/*",     /* Multi-line comment start */
        "*/",     /* Multi-line comment end */
        0         /* Flags */
    },
    /* Lua language */
    {
        "lua",
        (char*[]){"lua", NULL},
        LUA_HL_keywords,
        "--",     /* Single line comment start */
        "--[[",   /* Multi-line comment start */
        "]]",     /* Multi-line comment end */
        0         /* Flags */
    },
    /* Python language */
    {
        "python",
        (char*[]){"py", "pyw", NULL},
        PYTHON_HL_keywords,
        "#",      /* Single line comment start */
        "\"\"\"", /* Multi-line comment/docstring start */
        "\"\"\"", /* Multi-line comment/docstring end */
        0         /* Flags */
    },
    /* Riddle language */
    {
        "riddle",
        (char*[]){"rid", NULL},
        RIDDLE_HL_keywords,
        "//",     /* Single line comment start */
        "/*",     /* Multi-line comment start */
        "*/",     /* Multi-line comment end */
        0
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* Is the character a separator */
static bool is_separator(int c) {
    return c == '\0' || isspace(c) || c == '\0' ||
    strchr(",.()+-/*=~%<>[];\\{}:", c) != NULL;
}

/* Handle hex, octal, or binary number formats */
static bool highlight_based_number(pleditor_row *row, int *i) {
    if (*i + 2 >= row->render_size || row->render[*i] != '0')
        return false;

    char next = row->render[*i + 1];
    if (!strchr("xXoObB", next))
        return false;

    /* Highlight the prefix (0x, 0o, 0b) */
    row->hl->hl[*i] = row->hl->hl[*i + 1] = HL_NUMBER;
    *i += 2;

    /* Continue highlighting based on the number format */
    while (*i < row->render_size) {
        char c = row->render[*i];
        bool valid;

        if (next == 'x' || next == 'X')
            valid = isxdigit(c);
        else if (next == 'o' || next == 'O')
            valid = c >= '0' && c <= '7';
        else
            valid = c == '0' || c == '1';

        if (!valid) break;

        row->hl->hl[*i] = HL_NUMBER;
        (*i)++;
    }

    return true;
}

/* Initialize syntax highlighting system */
bool pleditor_syntax_init(pleditor_state *state) {
    state->syntax = NULL;

    /* Select syntax by filename if there is one */
    if (state->filename) {
        pleditor_syntax_by_name(state, state->filename);

        /* Apply syntax highlighting to all rows */
        pleditor_syntax_update_all(state);
    }

    return true;
}

/* Apply syntax highlighting to all rows in the file */
void pleditor_syntax_update_all(pleditor_state *state) {
    if (!state->syntax) return;

    for (int i = 0; i < state->num_rows; i++) {
        pleditor_syntax_update_row(state, i);
    }
}

/* Map highlight values to ansi escape code */
int pleditor_syntax_color_to_ansi(int hl) {
    switch(hl) {
        case HL_COMMENT:
        case HL_MULTILINE_COMMENT:
            return 36;  /* Cyan */
        case HL_KEYWORD1:
            return 33;  /* Yellow */
        case HL_KEYWORD2:
            return 32;  /* Green */
        case HL_STRING:
            return 35;  /* Magenta */
        case HL_NUMBER:
            return 31;  /* Red */
        case HL_MATCHSEARCH:
            return 34;  /* Blue */
        default:
            return 37;  /* White (default) */
    }
}

/* Select syntax highlighting based on file extension */
void pleditor_syntax_by_name(pleditor_state *state, const char *filename) {
    state->syntax = NULL;

    /* No filename, so no highlighting */
    if (!filename) return;

    /* Get file extension */
    char *ext = strrchr(filename, '.');
    if (!ext) return;
    ext++; /* Skip the dot */

    /* Try to match file extension with a syntax */
    for (unsigned int i = 0; i < HLDB_ENTRIES; i++) {
        pleditor_syntax *syntax = &HLDB[i];
        char **pattern = syntax->filematch;

        while (*pattern) {
            /* Check if extension matches */
            if (strcmp(*pattern, ext) == 0) {
                state->syntax = syntax;
                return;
            }
            pattern++;
        }
    }
}

/* Update highlighting for a row */
void pleditor_syntax_update_row(pleditor_state *state, int row_idx) {
    pleditor_row *row = &state->rows[row_idx];

    /* Free existing highlighting memory */
    if (row->hl) {
        free(row->hl->hl);
        free(row->hl);
    }

    /* Allocate new highlight struct */
    row->hl = malloc(sizeof(pleditor_highlight_row));
    row->hl->hl = malloc(row->render_size);
    memset(row->hl->hl, HL_NORMAL, row->render_size);
    row->hl->hl_multiline_comment = false;

    /* If no syntax, leave everything as normal */
    if (!state->syntax) return;

    char **keywords = state->syntax->keywords;
    char *scs = state->syntax->singleline_comment_start;
    char *mcs = state->syntax->multiline_comment_start;
    char *mce = state->syntax->multiline_comment_end;

    bool prev_sep = true;
    int in_string = 0;
    bool in_comment = (row_idx > 0 && state->rows[row_idx-1].hl) ?
                    state->rows[row_idx-1].hl->hl_multiline_comment : false;

    int i = 0;
    while (i < row->render_size) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl->hl[i-1] : HL_NORMAL;

        /* String handling */
        if (in_string) {
            row->hl->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->render_size) {
                row->hl->hl[i+1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == in_string) in_string = 0;
            i++;
            prev_sep = true;
            continue;
        }

        /* Comment handling */
        if (in_comment) {
            row->hl->hl[i] = HL_MULTILINE_COMMENT;
            if (mce && strncmp(&row->render[i], mce, strlen(mce)) == 0) {
                for (unsigned int j = 0; j < strlen(mce); j++)
                    row->hl->hl[i+j] = HL_MULTILINE_COMMENT;
                i += strlen(mce);
                in_comment = false;
                prev_sep = true;
                continue;
            } else {
                i++;
                continue;
            }
        }

        /* Start of multi-line comment */
        if (mcs && strncmp(&row->render[i], mcs, strlen(mcs)) == 0) {
            for (unsigned int j = 0; j < strlen(mcs); j++)
                row->hl->hl[i+j] = HL_MULTILINE_COMMENT;
            i += strlen(mcs);
            in_comment = true;
            continue;
        }

        /* Start of single-line comment */
        if (scs && strncmp(&row->render[i], scs, strlen(scs)) == 0) {
            for (int j = i; j < row->render_size; j++)
                row->hl->hl[j] = HL_COMMENT;
            break;
        }

        /* String start or include brackets <> */
        if (c == '"' || c == '\'' ||
            (c == '<' && prev_sep && strstr(row->render, "#include") != NULL)) {
            /* Set appropriate closing character */
            char closing = (c == '<') ? '>' : c;
            in_string = closing;
            row->hl->hl[i] = HL_STRING;
            i++;
            continue;
        }

        /* Number handling */
        if (isdigit(c)) {
            /* Check for special number formats (hex, octal, binary) */
            if (highlight_based_number(row, &i)) {
                prev_sep = false;
                continue;
            }

            /* Regular decimal number */
            if (prev_sep || prev_hl == HL_NUMBER) {
                row->hl->hl[i] = HL_NUMBER;
                i++;
                prev_sep = false;
                continue;
            }
        } else if (c == '.' && prev_hl == HL_NUMBER) {
            /* Decimal point in a number */
            row->hl->hl[i] = HL_NUMBER;
            i++;
            prev_sep = false;
            continue;
        }

        /* Handle special case for colon in array slices */
        if (c == ':' && ((i > 0 && row->hl->hl[i-1] == HL_NUMBER) ||
            (i+1 < row->render_size && isdigit(row->render[i+1])))) {
            i++;
            prev_sep = true; /* Treat colon as separator */
            continue;
        }

        /* Keyword handling */
        if (prev_sep) {
            bool found_keyword = false;
            for (int j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                bool is_kw2 = keywords[j][klen-1] == '|';
                if (is_kw2) klen--;

                if (strncmp(&row->render[i], keywords[j], klen) == 0 &&
                    is_separator(row->render[i + klen])) {

                    /* Apply keyword highlighting */
                    for (int k = 0; k < klen; k++)
                        row->hl->hl[i+k] = is_kw2 ? HL_KEYWORD2 : HL_KEYWORD1;
                    i += klen;
                    found_keyword = true;
                    break;
                }
            }

            if (found_keyword) {
                prev_sep = false;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    /* Update multiline comment status for this row */
    row->hl->hl_multiline_comment = in_comment;
}
