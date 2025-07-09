/**
 * windows.c - Windows implementation of platform.h
 */

#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include <io.h>
#include <fcntl.h>
#include "../platform.h"

static HANDLE hStdin;
static HANDLE hStdout;
static DWORD fdwOrigInputMode;
static DWORD fdwOrigOutputMode;

bool pleditor_platform_init(void) {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    
    // Retrieve original console modes
    if (!GetConsoleMode(hStdin, &fdwOrigInputMode))
        return false;
    if (!GetConsoleMode(hStdout, &fdwOrigOutputMode)) 
        return false;

    // Configure input mode
    DWORD inputMode = fdwOrigInputMode;
    inputMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    
    // Configure output mode (preserve original flags, add VT support)
    DWORD outputMode = fdwOrigOutputMode;
    outputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    
    // Apply input mode settings
    if (!SetConsoleMode(hStdin, inputMode)) {
        // Fallback: only disable line buffering and echo
        inputMode = fdwOrigInputMode;
        inputMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
        if (!SetConsoleMode(hStdin, inputMode)) {
            return false;
        }
    }
    
    // Apply output mode settings
    if (!SetConsoleMode(hStdout, outputMode)) {
        // Fallback: revert to original output mode
        outputMode = fdwOrigOutputMode;
        if (!SetConsoleMode(hStdout, outputMode)) {
            return false;
        }
    }
    
    // Set binary I/O mode
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    
    return true;
}

void pleditor_platform_cleanup(void) {
    // Restore original console modes
    SetConsoleMode(hStdin, fdwOrigInputMode);
    SetConsoleMode(hStdout, fdwOrigOutputMode);
}

bool pleditor_platform_get_size(int *rows, int *cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hStdout, &csbi)) 
        return false;
    
    // Calculate visible window dimensions
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return true;
}

int pleditor_platform_read_key(void) {
    INPUT_RECORD ir[128];
    DWORD count;
    
    while (1) {
        // Read console input events
        if (!ReadConsoleInput(hStdin, ir, 128, &count)) 
            return PLEDITOR_KEY_ERR;
        
        for (DWORD i = 0; i < count; i++) {
            // Filter key press events only
            if (ir[i].EventType != KEY_EVENT || !ir[i].Event.KeyEvent.bKeyDown) 
                continue;
            
            WORD vk = ir[i].Event.KeyEvent.wVirtualKeyCode;
            CHAR ch = ir[i].Event.KeyEvent.uChar.AsciiChar;
            
            // Process special keys
            switch (vk) {
                case VK_UP:     return PLEDITOR_ARROW_UP;
                case VK_DOWN:   return PLEDITOR_ARROW_DOWN;
                case VK_LEFT:   return PLEDITOR_ARROW_LEFT;
                case VK_RIGHT:  return PLEDITOR_ARROW_RIGHT;
                case VK_HOME:   return PLEDITOR_HOME_KEY;
                case VK_END:    return PLEDITOR_END_KEY;
                case VK_PRIOR:  return PLEDITOR_PAGE_UP;
                case VK_NEXT:   return PLEDITOR_PAGE_DOWN;
                case VK_DELETE: return PLEDITOR_DEL_KEY;
                case VK_ESCAPE: return PLEDITOR_KEY_ESC;
            }
            
            // Process printable characters
            if ((unsigned char)ch > 0 && (unsigned char)ch != 0xE0) 
                return ch;
        }
    }
}

void pleditor_platform_write(const char *s, size_t len) {
    DWORD written;
    // Output string to console
    WriteConsole(hStdout, s, (DWORD)len, &written, NULL);
}

bool pleditor_platform_read_file(const char *filename, char **buffer, size_t *len) {
    // Open file in binary read mode
    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;
    
    // Determine file size
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Allocate memory buffer
    *buffer = malloc(fsize + 1);
    if (!*buffer) {
        fclose(fp);
        return false;
    }
    
    // Read file contents
    size_t read = fread(*buffer, 1, fsize, fp);
    fclose(fp);
    
    // Verify complete read
    if (read != (size_t)fsize) {
        free(*buffer);
        return false;
    }
    
    // Null-terminate buffer
    (*buffer)[fsize] = '\0';
    *len = fsize;
    return true;
}

bool pleditor_platform_write_file(const char *filename, const char *buffer, size_t len) {
    // Open file in binary write mode
    FILE *fp = fopen(filename, "wb");
    if (!fp) return false;
    
    // Write buffer to file
    size_t written = fwrite(buffer, 1, len, fp);
    fclose(fp);
    
    // Verify complete write
    return written == len;
}