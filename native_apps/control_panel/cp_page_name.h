/* cp_page_name.h — `control_panel <page>`: the pure name -> page lookup.
 *
 * Pure (strings only, no CpPage, no framebuffer) so a host test can link it
 * alone.  The names come from the page table itself — control_panel.c hands in
 * each CpPage's `name` — so there is no second list of page names. */
#ifndef CP_PAGE_NAME_H
#define CP_PAGE_NAME_H

/* Index into names[0..n) that arg selects, or -1.  Case-insensitive; an exact
 * name wins, otherwise a prefix that matches exactly one name (so `info` opens
 * "Information").  NULL or empty arg, and an ambiguous prefix, are -1. */
int cp_page_find(const char *const *names, int n, const char *arg);

#endif
