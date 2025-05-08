/**
 * linux.c - Linux implementation of the platform interface
 */

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <time.h>
#include <fcntl.h>

#include "../platform.h"
#include "../pleditor.h"

/* Original terminal settings */
static struct termios orig_termios;

/* Initialize the terminal for raw mode */
bool pleditor_platform_init(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        return false;
    }

    /* Switch to alternate screen buffer */
    write(STDOUT_FILENO, "\033[?1049h", 8);

    struct termios raw = orig_termios;

    /* Input flags: disable break signal, disable CR to NL translation,
     * disable parity checking, disable stripping high bit */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /* Output flags: disable post-processing */
    raw.c_oflag &= ~(OPOST);

    /* Control flags: set 8-bit chars */
    raw.c_cflag |= (CS8);

    /* Local flags: disable echoing, canonical mode,
     * disable extended functions, disable signal chars */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* Control chars: set return condition to return as soon as any input is available */
    raw.c_cc[VMIN] = 0;  /* No minimum num of bytes required */
    raw.c_cc[VTIME] = 1; /* 100ms timeout between bytes */

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return false;
    }

    return true;
}

/* Restore terminal settings */
void pleditor_platform_cleanup(void) {
    /* Return to normal screen buffer */
    write(STDOUT_FILENO, "\033[?1049l", 8);

    /* Restore original terminal settings */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/* Get the terminal window size */
bool pleditor_platform_get_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return false;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return true;
    }
}

/* Read a key from the terminal */
int pleditor_platform_read_key(void) {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            return PLEDITOR_KEY_ERR;
        }
    }

    /* For non-escape characters, return immediately */
    if (c != PLEDITOR_KEY_ESC) {
        return c;
    }
    
    /* Handle escape sequences */
    char seq[3];

    /* Early returns for incomplete sequences */
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return PLEDITOR_KEY_ESC;
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return PLEDITOR_KEY_ESC;

    /* Handle '[' sequence type (e.g., cursor keys) */
    if (seq[0] == '[') {
        /* Handle numeric escape codes (like ESC[1~) */
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) return PLEDITOR_KEY_ESC;
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return PLEDITOR_HOME_KEY;
                    case '3': return PLEDITOR_DEL_KEY;
                    case '4': return PLEDITOR_END_KEY;
                    case '5': return PLEDITOR_PAGE_UP;
                    case '6': return PLEDITOR_PAGE_DOWN;
                    case '7': return PLEDITOR_HOME_KEY;
                    case '8': return PLEDITOR_END_KEY;
                }
            }
            return PLEDITOR_KEY_ESC;
        }
        
        /* Handle arrow keys and others (like ESC[A) */
        switch (seq[1]) {
            case 'A': return PLEDITOR_ARROW_UP;
            case 'B': return PLEDITOR_ARROW_DOWN;
            case 'C': return PLEDITOR_ARROW_RIGHT;
            case 'D': return PLEDITOR_ARROW_LEFT;
            case 'H': return PLEDITOR_HOME_KEY;
            case 'F': return PLEDITOR_END_KEY;
        }
        return PLEDITOR_KEY_ESC;
    }
    
    /* Handle 'O' sequence type */
    if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return PLEDITOR_HOME_KEY;
            case 'F': return PLEDITOR_END_KEY;
        }
    }

    return PLEDITOR_KEY_ESC;

    return c;
}

/* Write to the terminal */
void pleditor_platform_write(const char *s, size_t len) {
    write(STDOUT_FILENO, s, len);
}

/* Read the contents of a file */
bool pleditor_platform_read_file(const char *filename, char **buffer, size_t *len) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return false;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    size_t filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Allocate buffer */
    *buffer = malloc(filesize + 1);
    if (!*buffer) {
        fclose(fp);
        return false;
    }

    /* Read file */
    size_t bytes_read = fread(*buffer, 1, filesize, fp);
    if (bytes_read < filesize) {
        free(*buffer);
        fclose(fp);
        return false;
    }

    /* Success path - null-terminate and return */
    (*buffer)[bytes_read] = '\0';
    *len = bytes_read;
    fclose(fp);
    return true;
}

/* Write buffer to a file */
bool pleditor_platform_write_file(const char *filename, const char *buffer, size_t len) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        return false;
    }

    size_t bytes_written = fwrite(buffer, 1, len, fp);
    fclose(fp);

    return bytes_written == len;
}
