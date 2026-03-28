#include "diagnostics.h"
#include "stim2.h"
#include "imgui.h"
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif

static DiagnosticsPanel panel;

/* ------------------------------------------------------------------ */
/*  Network info                                                      */
/* ------------------------------------------------------------------ */

struct NetInterface {
    std::string name;
    std::string addr;
    bool up;
};

static std::vector<NetInterface> s_interfaces;
static double s_last_refresh = 0.0;
static std::string s_hostname;
static const double REFRESH_INTERVAL = 5.0;  /* seconds */

static void refreshNetworkInfo()
{
    s_interfaces.clear();

    /* hostname */
    {
        char buf[256];
        if (gethostname(buf, sizeof(buf)) == 0)
            s_hostname = buf;
        else
            s_hostname = "(unknown)";
    }

#if defined(_WIN32)
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES addrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
    if (GetAdaptersAddresses(AF_INET, 0, NULL, addrs, &bufLen) == ERROR_SUCCESS) {
        for (auto a = addrs; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            for (auto u = a->FirstUnicastAddress; u; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
                char ip[INET_ADDRSTRLEN];
                auto sa = (struct sockaddr_in *)u->Address.lpSockaddr;
                inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
                /* convert wide name */
                char name[256];
                wcstombs(name, a->FriendlyName, sizeof(name));
                s_interfaces.push_back({name, ip, true});
            }
        }
    }
    free(addrs);
#else
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (auto ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            /* skip loopback */
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            char ip[INET_ADDRSTRLEN];
            auto sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            bool up = (ifa->ifa_flags & IFF_UP) && (ifa->ifa_flags & IFF_RUNNING);
            s_interfaces.push_back({ifa->ifa_name, ip, up});
        }
        freeifaddrs(ifap);
    }
#endif
}

/* ------------------------------------------------------------------ */
/*  DiagnosticsPanel methods                                          */
/* ------------------------------------------------------------------ */

void DiagnosticsPanel::set(const char *section, const char *key,
                           const char *value, DiagColor color)
{
    DiagSection *sec = nullptr;
    for (auto &s : sections) {
        if (s.name == section) { sec = &s; break; }
    }
    if (!sec) {
        sections.push_back({section, {}});
        sec = &sections.back();
    }
    for (auto &e : sec->entries) {
        if (e.key == key) {
            e.value = value;
            e.color = color;
            return;
        }
    }
    sec->entries.push_back({key, value, color});
}

void DiagnosticsPanel::clear(const char *section)
{
    if (!section) {
        sections.clear();
        return;
    }
    sections.erase(
        std::remove_if(sections.begin(), sections.end(),
                       [section](const DiagSection &s) { return s.name == section; }),
        sections.end());
}

static ImVec4 colorForDiag(DiagColor c)
{
    switch (c) {
    case DIAG_GREEN:  return ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    case DIAG_YELLOW: return ImVec4(1.0f, 1.0f, 0.2f, 1.0f);
    case DIAG_RED:    return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    default:          return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

static void drawTable(const char *id, const std::vector<DiagEntry> &entries)
{
    if (entries.empty()) return;
    if (ImGui::BeginTable(id, 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        for (auto &e : entries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(colorForDiag(e.color), "%s", e.value.c_str());
        }
        ImGui::EndTable();
    }
}

static void drawSectionHeader(const char *label)
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", label);
    ImGui::Separator();
}

void DiagnosticsPanel::drawSections()
{
    /* refresh network info only while visible, throttled */
    double now = ImGui::GetTime();
    if (now - s_last_refresh > REFRESH_INTERVAL) {
        refreshNetworkInfo();
        s_last_refresh = now;
    }

    /* Network section (built-in) */
    drawSectionHeader("Network");
    {
        std::vector<DiagEntry> entries;
        entries.push_back({"Hostname", s_hostname, DIAG_WHITE});
        if (s_interfaces.empty()) {
            entries.push_back({"Status", "No interfaces found", DIAG_RED});
        } else {
            for (auto &iface : s_interfaces) {
                DiagColor c = iface.up ? DIAG_GREEN : DIAG_RED;
                std::string val = iface.addr;
                if (!iface.up) val += "  (down)";
                else val += "  ->  http://" + iface.addr + ":4613";
                entries.push_back({iface.name, val, c});
            }
        }
        drawTable("##network", entries);
    }

    /* User-defined sections (from diagSet) */
    for (auto &sec : sections) {
        drawSectionHeader(sec.name.c_str());
        drawTable(sec.name.c_str(), sec.entries);
    }
}

/* ------------------------------------------------------------------ */
/*  C interface                                                       */
/* ------------------------------------------------------------------ */

void diagDrawSections(void)
{
    panel.drawSections();
}

void diagResetRefresh(void)
{
    s_last_refresh = 0.0;
}

/* ------------------------------------------------------------------ */
/*  Tcl commands                                                      */
/* ------------------------------------------------------------------ */

static DiagColor parseColor(const char *s)
{
    if (!s) return DIAG_WHITE;
    if (!strcmp(s, "green"))  return DIAG_GREEN;
    if (!strcmp(s, "yellow")) return DIAG_YELLOW;
    if (!strcmp(s, "red"))    return DIAG_RED;
    return DIAG_WHITE;
}

int diagSetCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    if (argc < 4 || argc > 5) {
        Tcl_SetResult(interp,
                      (char *)"usage: diagSet section key value ?color?",
                      TCL_STATIC);
        return TCL_ERROR;
    }
    DiagColor color = (argc == 5) ? parseColor(argv[4]) : DIAG_WHITE;
    panel.set(argv[1], argv[2], argv[3], color);
    return TCL_OK;
}

int diagClearCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    if (argc > 2) {
        Tcl_SetResult(interp,
                      (char *)"usage: diagClear ?section?",
                      TCL_STATIC);
        return TCL_ERROR;
    }
    panel.clear(argc == 2 ? argv[1] : nullptr);
    return TCL_OK;
}

int diagShowCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    if (argc > 2) {
        Tcl_SetResult(interp,
                      (char *)"usage: diagShow ?on|off|toggle?",
                      TCL_STATIC);
        return TCL_ERROR;
    }
    if (argc == 1 || !strcmp(argv[1], "toggle")) {
        doToggleImgui();
    } else if (!strcmp(argv[1], "on")) {
        if (!isImguiVisible())
            doToggleImgui();
    } else if (!strcmp(argv[1], "off")) {
        if (isImguiVisible())
            doToggleImgui();
    } else {
        Tcl_SetResult(interp,
                      (char *)"usage: diagShow ?on|off|toggle?",
                      TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(isImguiVisible()));
    return TCL_OK;
}
