#include "../xboxrecomp/include/nfsu_log.h"
#include "android_env.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void android_env_load(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return;
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        /* Discard an oversized line as a whole, never parse its tail. */
        if (!strchr(line, '\n') && !feof(file)) {
            int ch;
            while ((ch = fgetc(file)) != '\n' && ch != EOF) {}
            NFS_LOG_WARN("system", "[ANDROID] env: oversized line ignored\n");
            continue;
        }
        char *key = line;
        while (isspace((unsigned char)*key)) key++;
        if (!*key || *key == '#') continue;
        char *equal = strchr(key, '=');
        if (!equal) continue;
        char *end = equal;
        while (end > key && isspace((unsigned char)end[-1])) end--;
        *end = 0;
        if (!*key) continue;
        int valid = 1;
        for (char *p = key; *p; p++)
            if (!isalnum((unsigned char)*p) && *p != '_') valid = 0;
        if (!valid) continue;
        char *value = equal + 1;
        while (isspace((unsigned char)*value)) value++;
        end = value + strlen(value);
        while (end > value && isspace((unsigned char)end[-1])) *--end = 0;
        if (setenv(key, value, 1) != 0)
            NFS_LOG_ERROR("system", "[ANDROID] env: cannot set %s\n", key);
    }
    fclose(file);
    NFS_LOG_INFO("system", "[ANDROID] env loaded: %s\n", path);
}
