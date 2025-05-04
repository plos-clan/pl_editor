/**
 * syntax.h - Syntax highlighting for pleditor
 */
#ifndef SYNTAX_H
#define SYNTAX_H

#include <stdbool.h>
#include "pleditor.h"

/* Function prototypes */
bool pleditor_syntax_init(pleditor_state *state);
void pleditor_syntax_update_row(pleditor_state *state, int row_idx);
int pleditor_syntax_color_to_vt100(int hl);
void pleditor_syntax_select_by_filename(pleditor_state *state, const char *filename);
void pleditor_syntax_update_all(pleditor_state *state);

#endif /* SYNTAX_H */