/* common functions */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void print_ascii_art() {
    printf("\n");
    printf("Welcome to Chat Roullette v1.0\n");
    printf("\n");
    printf("___ ____ _  _ ___    ____ _  _ ____ ___ ____ ____ _  _ _    _    ____ ___ ___ ____\n");
    printf(" |  |___  \/   |     |    |__| |__|  |  |__/ |  | |  | |    |    |___  |   |  |___\n");
    printf(" |  |___ _/\_  |     |___ |  | |  |  |  |  \ |__| |__| |___ |___ |___  |   |  |___\n");
	printf("\n");
}

// strip whitespace
char* strip(char *s) {
    size_t size;
    char *end;
    size = strlen(s);
    if (!size) return s;
    end = s + size - 1;
    while (end >= s && isspace(*end)) {
    	end--;
    }
    *(end + 1) = '\0';
    while (*s && isspace(*s)) {
    	s++;
    }
    return s;
}

