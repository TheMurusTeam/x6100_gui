/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 */

/* FT8 CB - country from the division number, see cb_div.h */

#include "cb_div.h"
#include "cb_msg.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIV_MAX 1000            /* division numbers have at most 3 digits */

static char *names[DIV_MAX];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void load(void)
{
    FILE *f = fopen(CB_DIV_FILE, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || !isdigit((unsigned char)line[0])) continue;
        char *sep = strchr(line, ';');
        if (!sep) continue;
        int n = atoi(line);
        char *name = sep + 1;
        name[strcspn(name, "\r\n")] = '\0';
        while (*name == ' ') name++;
        if (n <= 0 || n >= DIV_MAX || !name[0] || names[n]) continue;
        names[n] = strdup(name);
    }
    fclose(f);
}

const char *ft8cb_country(const char *call)
{
    if (!call || !ft8cb_is_cb_call(call)) return NULL;
    pthread_once(&once, load);
    int n = 0;
    for (const char *p = call; isdigit((unsigned char)*p); p++) n = n * 10 + (*p - '0');
    return (n > 0 && n < DIV_MAX) ? names[n] : NULL;
}
