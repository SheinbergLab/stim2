#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <tcl.h>

#ifdef __cplusplus

#include <string>
#include <vector>

enum DiagColor { DIAG_WHITE = 0, DIAG_GREEN, DIAG_YELLOW, DIAG_RED };

struct DiagEntry {
    std::string key;
    std::string value;
    DiagColor color;
};

struct DiagSection {
    std::string name;
    std::vector<DiagEntry> entries;
};

struct DiagnosticsPanel {
    std::vector<DiagSection> sections;

    void set(const char *section, const char *key, const char *value, DiagColor color = DIAG_WHITE);
    void clear(const char *section = nullptr);
    void drawSections();
};

extern "C" {
#endif

/* Tcl command implementations — called from tclproc.c */
int diagSetCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]);
int diagClearCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]);
int diagShowCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]);

/* render diagnostic sections into current imgui window */
void diagDrawSections(void);

/* reset refresh timer so next draw gets fresh data */
void diagResetRefresh(void);

#ifdef __cplusplus
}
#endif

#endif
