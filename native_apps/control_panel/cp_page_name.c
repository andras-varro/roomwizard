/* cp_page_name.c — see cp_page_name.h. */
#include "cp_page_name.h"
#include <ctype.h>
#include <stddef.h>

/* Length of the common case-insensitive prefix is what the caller needs, so
 * report: 2 = whole of name equals arg, 1 = arg is a proper prefix, 0 = no. */
static int match(const char *name, const char *arg) {
    while (*arg) {
        if (!*name || tolower((unsigned char)*name) != tolower((unsigned char)*arg))
            return 0;
        name++; arg++;
    }
    return *name ? 1 : 2;
}

int cp_page_find(const char *const *names, int n, const char *arg) {
    if (!names || !arg || !*arg) return -1;
    int found = -1, count = 0;
    for (int i = 0; i < n; i++) {
        int m = match(names[i], arg);
        if (m == 2) return i;
        if (m == 1) { found = i; count++; }
    }
    return count == 1 ? found : -1;
}
