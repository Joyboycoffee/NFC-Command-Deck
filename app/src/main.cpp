#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0600
#define WINVER 0x0601

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

#define NOTIFY_FOR_THIS_SESSION 0
#define WM_WTSSESSION_CHANGE    0x02B1
#define WTS_SESSION_LOCK        0x7
#define WTS_SESSION_UNLOCK      0x8

typedef BOOL (WINAPI *PWTSRegisterSessionNotification)(HWND, DWORD);
typedef BOOL (WINAPI *PWTSUnRegisterSessionNotification)(HWND);

#include "ui_theme.h"
#include "action_executor.h"
#include "config_manager.h"
#include "sound_player.h"
#include "nfc_engine.h"

#define WM_TRAY_ICON        (WM_USER + 101)
#define WM_ACTION_RESULT    (WM_USER + 105)
#define WM_NFC_TAG_EVENT    (WM_USER + 201)
#define WM_NFC_STATUS_EVENT (WM_USER + 202)

#define ID_TRAY_EXIT   9001
#define ID_TRAY_SHOW   9002
#define ID_TRAY_RECORD 9003

// Edit Control IDs
#define IDC_MODAL_NAME      1001
#define IDC_MODAL_UID       1002
#define IDC_MODAL_PAYLOAD   1003
#define IDC_MODAL_TARGET    1004
#define IDC_MODAL_ARGS      1005
#define IDC_MODAL_SOUND     1006
#define IDC_WRITE_PAYLOAD   2001

enum AppTab {
    TAB_AUTOMATIONS,
    TAB_PROGRAMMER,
    TAB_SETTINGS
};

// Global Application State
HINSTANCE g_hInstance = NULL;
HWND g_hWnd = NULL;
HICON g_hAppIcon = NULL;
ULONG_PTR g_gdiplusToken;
NOTIFYICONDATAW g_nid;
HBRUSH g_hDarkBrush = NULL;
HBRUSH g_hInputBrush = NULL;
HFONT g_hFontNormal = NULL;
HFONT g_hFontBold = NULL;
HMODULE g_hWts = NULL;

AppConfig g_config;
std::string g_configPath = "nfc_actions.json";

NFCEngine g_nfc;
TagEvent g_lastTag;
TagEvent g_lastScannedCard;
bool g_recordMode = false;
std::string g_connectionStatusText = "Connecting (COM8)...";
bool g_connected = false;
bool g_isSessionLocked = false;
DWORD g_appStartTime = 0;

AppTab g_activeTab = TAB_AUTOMATIONS;

// Writer State
int g_writeFormatIndex = 0; // 0: URL, 1: Plain Text, 2: Wipe Tag
std::string g_writeType = "url";
bool g_writerArmed = false;
bool g_portalActive = false;
std::string g_writerStatusText = "Writer Ready. Choose format, enter data, and click Arm Reader.";
HWND g_hWritePayload = NULL;

// In-Window Modal Editor State
bool g_modalActive = false;
bool g_modalIsEdit = false;
int g_modalEditIndex = -1;
ActionRule g_modalRule;
int g_modalMatchIndex = 0; // 0: UID, 1: Payload, 2: Both, 3: Any
int g_modalTypeIndex = 0;  // 0: App, 1: Steam, 2: Spotify, 3: Media, 4: Command, 5: URL, 6: PIN/Lock, 7: System
bool g_modalSilent = true;
bool g_modalEnabled = true;

// Modal Edit HWNDs (Pure borderless edits over dark GDI+ cards)
HWND g_hEditName = NULL;
HWND g_hEditUid = NULL;
HWND g_hEditPayload = NULL;
HWND g_hEditTarget = NULL;
HWND g_hEditArgs = NULL;
HWND g_hComboSound = NULL;
std::vector<std::string> g_availableSounds;

// Physical Card Lift & Cooldown Tracking
bool g_cardLifted = true;
bool g_cardActionFired = false;
std::string g_activeCardUid = "";
DWORD g_lastCardTriggerTime = 0;

// Activity Log Stream
struct LogEntry {
    std::string timestamp;
    std::string text;
    Color color;
};
std::vector<LogEntry> g_activityLog;

// UI Scrolling and Hover
int g_scrollOffset = 0;
int g_hoveredRuleIndex = -1;
int g_hoveredButtonId = 0;

// Async Action Result Struct
struct AsyncActionResult {
    std::string ruleName;
    std::string statusMsg;
    bool success;
    std::string sound;
};

// Forward Declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void OpenModalEditor(const std::string& prefillUid = "", const std::string& prefillPayload = "", int editIndex = -1);
void CloseModalEditor();
void SaveModalEditor();
void UpdateControlsVisibility();
void RepositionModalControls(int clientW, int clientH);
void AddLog(const std::string& text, Color col = Theme::TextMain());
void ExecuteActionAsync(const ActionRule& r, bool isLocked, bool bleHandled = false);

std::wstring s2ws(const std::string& s) {
    if (s.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &s[0], (int)s.size(), NULL, 0);
    std::wstring ws(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &s[0], (int)s.size(), &ws[0], size_needed);
    return ws;
}

std::string ws2s(const std::wstring& ws) {
    if (ws.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(), NULL, 0, NULL, NULL);
    std::string s(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(), &s[0], size_needed, NULL, NULL);
    return s;
}

std::string GetCurrentTimestamp() {
    time_t now = time(0);
    tm* ltm = localtime(&now);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    return std::string(buf);
}

void AddLog(const std::string& text, Color col) {
    LogEntry e;
    e.timestamp = GetCurrentTimestamp();
    e.text = text;
    e.color = col;
    g_activityLog.insert(g_activityLog.begin(), e);
    if (g_activityLog.size() > 60) g_activityLog.pop_back();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

// Windows Background Service & Auto-Start Helpers
std::string GetExeDir() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s(path);
    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return s.substr(0, lastSlash);
    }
    return ".";
}

int GetBackgroundServiceStatus() {
    // Returns: 1 = Running, 2 = Stopped / Other, 0 = Not installed
    SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return 0;

    SC_HANDLE hService = OpenServiceA(hSCM, "NFCCommanderService", SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return 0;
    }

    SERVICE_STATUS_PROCESS ssp = {0};
    DWORD bytesNeeded = 0;
    int status = 0;
    if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) status = 1;
        else status = 2; // Installed but not running
    }
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return status;
}

bool IsAutoStartOnLogon() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char val[MAX_PATH] = {0};
        DWORD sz = sizeof(val);
        DWORD type = 0;
        LONG res = RegQueryValueExA(hKey, "NFCCommandDeck", NULL, &type, (LPBYTE)val, &sz);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void SetAutoStartOnLogon(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            char path[MAX_PATH] = {0};
            GetModuleFileNameA(NULL, path, MAX_PATH);
            std::string cmd = std::string("\"") + path + "\" --minimized";
            RegSetValueExA(hKey, "NFCCommandDeck", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.length() + 1));
        } else {
            RegDeleteValueA(hKey, "NFCCommandDeck");
        }
        RegCloseKey(hKey);
    }
}

void ShowTrayNotification(const std::string& title, const std::string& msg) {
    if (!g_config.notificationsEnabled) return;
#ifdef NIF_INFO
    g_nid.uFlags |= NIF_INFO;
    wcsncpy(g_nid.szInfoTitle, s2ws(title).c_str(), sizeof(g_nid.szInfoTitle) / sizeof(wchar_t) - 1);
    wcsncpy(g_nid.szInfo, s2ws(msg).c_str(), sizeof(g_nid.szInfo) / sizeof(wchar_t) - 1);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
#endif
}

// Background thread executor - never blocks GUI thread!
struct AsyncActionTask {
    ActionRule rule;
    bool isLocked;
    bool bleHandled;
};

static DWORD WINAPI ExecuteActionThreadProc(LPVOID lp) {
    AsyncActionTask* task = (AsyncActionTask*)lp;
    std::string statusMsg = "";
    bool ok = ActionExecutor::execute(task->rule, statusMsg, task->isLocked, [](const std::string& cmd) {
        g_nfc.sendCommand(cmd);
    }, task->bleHandled);
    AsyncActionResult* res = new AsyncActionResult{ task->rule.name, statusMsg, ok, task->rule.sound };
    delete task;
    PostMessageW(g_hWnd, WM_ACTION_RESULT, 0, (LPARAM)res);
    return 0;
}

void ExecuteActionAsync(const ActionRule& r, bool isLocked, bool bleHandled) {
    AsyncActionTask* task = new AsyncActionTask{ r, isLocked, bleHandled };
    CreateThread(NULL, 0, ExecuteActionThreadProc, task, 0, NULL);
}

std::wstring GetClipboardTextString() {
    if (!OpenClipboard(NULL)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return L"";
    }
    wchar_t* pszText = (wchar_t*)GlobalLock(hData);
    std::wstring text = pszText ? pszText : L"";
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

// Dynamically sync Edit HWND positions with GDI+ modal cards
void RepositionModalControls(int clientW, int clientH) {
    if (!g_modalActive) return;

    int mw = 620;
    int mh = 690;
    int mx = (clientW - mw) / 2;
    int my = (clientH - mh) / 2 - 10;

    SetWindowPos(g_hEditName, NULL, mx + 38, my + 90, mw - 76, 22, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
    SetWindowPos(g_hEditUid, NULL, mx + 38, my + 204, mw - 210, 22, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
    SetWindowPos(g_hEditPayload, NULL, mx + 38, my + 264, mw - 76, 22, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

    bool isAppOrScript = (g_modalTypeIndex == 0 || g_modalTypeIndex == 4);
    bool isSpotify = (g_modalTypeIndex == 2);
    int targetW = isAppOrScript ? (mw - 180) : (isSpotify ? (mw - 270) : (mw - 76));
    SetWindowPos(g_hEditTarget, NULL, mx + 38, my + 420, targetW, 22, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

    SetWindowPos(g_hEditArgs, NULL, mx + 38, my + 479, mw - 76, 22, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

    SetWindowPos(g_hComboSound, NULL, mx + 28, my + 532, mw - 256, 300, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
}

// Check tag against rules and execute
void ProcessTagTrigger(const TagEvent& tag) {
    if (!tag.uid.empty()) {
        g_lastScannedCard = tag;
    }

    // Clean payload
    std::string cleanPayload = tag.payload;
    while (!cleanPayload.empty() && (unsigned char)cleanPayload.front() <= 32) cleanPayload.erase(0, 1);
    while (!cleanPayload.empty() && (unsigned char)cleanPayload.back() <= 32) cleanPayload.pop_back();

    // If Writer is armed, confirm write
    if (g_writerArmed) {
        g_writerArmed = false;
        if (g_config.soundEnabled) SoundPlayer::playActionSuccess();
        g_writerStatusText = "Programmed Tag (" + tag.uid + ") successfully!";
        AddLog("Programmed Tag: " + tag.uid + " with " + g_writeType, Theme::Emerald());
        UpdateControlsVisibility();
        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
        return;
    }

    // If In-Window Modal Editor is active, auto-populate the modal without executing actions
    if (g_modalActive) {
        if (!tag.uid.empty()) SetWindowTextW(g_hEditUid, s2ws(tag.uid).c_str());
        if (!cleanPayload.empty()) {
            SetWindowTextW(g_hEditPayload, s2ws(cleanPayload).c_str());
            SetWindowTextW(g_hEditTarget, s2ws(cleanPayload).c_str());
        }
        if (g_config.soundEnabled) SoundPlayer::playTap();
        AddLog("Captured Tag in Editor: " + tag.uid, Theme::Cyan());
        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
        return;
    }

    // If Tap-to-Rule / Record Mode is active, open Modal Editor pre-filled
    if (g_recordMode) {
        g_recordMode = false;
        if (g_config.soundEnabled) SoundPlayer::playTap();
        AddLog("Tag Captured for Automation: " + tag.uid, Theme::Amber());
        OpenModalEditor(tag.uid, cleanPayload);
        return;
    }

    // Ignore phantom triggers during app startup (first 2.5s)
    if (GetTickCount() - g_appStartTime < 2500) {
        return;
    }

    DWORD now = GetTickCount();

    // 1. Snappy 1-Second Hold / Debounce Cooldown:
    // Once any card action or read has triggered, lock out all repeat triggers for 1000 ms.
    // Fast and responsive for quick re-taps while eliminating RF bounce and pull-up re-triggering.
    if (g_lastCardTriggerTime > 0 && (now - g_lastCardTriggerTime < 1000)) {
        return;
    }

    // 2. Continuous Placement Guard:
    // If the card is resting continuously on the sensor, only execute once per physical placement.
    if (!g_cardLifted && tag.uid == g_activeCardUid && g_cardActionFired) {
        return;
    }

    // --- MASTER AUTOMATIONS GLOBAL TOGGLE CHECK ---
    if (!g_config.masterAutomationsEnabled) {
        // Master switch is OFF: All automations are bypassed!
        // When OFF, even cards configured with automations will open their URL in the browser!
        std::string urlToOpen = "";

        // Check if card payload itself contains a web URL
        std::string lowerPayload = cleanPayload;
        for (char& c : lowerPayload) c = (char)tolower((unsigned char)c);
        if (lowerPayload.rfind("http://", 0) == 0 || lowerPayload.rfind("https://", 0) == 0) {
            urlToOpen = cleanPayload;
        } else if (lowerPayload.rfind("www.", 0) == 0) {
            urlToOpen = "https://" + cleanPayload;
        } else {
            // Check if any rule for this tag UID has an action of type url
            for (const auto& r : g_config.rules) {
                if ((r.tagUid == tag.uid || r.tagUid == "*") && r.type == ACTION_URL && !r.target.empty()) {
                    urlToOpen = r.target;
                    break;
                }
            }
        }

        if (!urlToOpen.empty()) {
            HINSTANCE hInst = ShellExecuteA(NULL, "open", urlToOpen.c_str(), NULL, NULL, SW_SHOWNORMAL);
            if ((INT_PTR)hInst > 32) {
                if (g_config.soundEnabled) SoundPlayer::playActionSuccess();
                AddLog("Master Automations OFF - Opened Card URL: " + urlToOpen, Theme::Cyan());
                ShowTrayNotification("Opened Web URL", urlToOpen);
            } else {
                AddLog("Failed to open URL: " + urlToOpen, Theme::Rose());
            }
        } else {
            if (g_config.soundEnabled) SoundPlayer::playTap();
            AddLog("Master Automations OFF - Card scanned: " + tag.uid + " (Automations Bypassed)", Theme::Amber());
        }

        if (!urlToOpen.empty()) g_cardActionFired = true;
        g_cardLifted = false;
        g_activeCardUid = tag.uid;
        g_lastCardTriggerTime = now;
        return;
    }

    bool matched = false;
    for (size_t i = 0; i < g_config.rules.size(); i++) {
        ActionRule& r = g_config.rules[i];
        bool isMatch = false;
        std::string mode = r.matchMode.empty() ? "uid" : r.matchMode;

        if (mode == "payload") {
            isMatch = (!cleanPayload.empty() && (cleanPayload == r.tagPayload || cleanPayload.find(r.tagPayload) != std::string::npos));
        } else if (mode == "both") {
            bool uidOk = (r.tagUid == "*" || r.tagUid == tag.uid);
            bool payOk = (!cleanPayload.empty() && (cleanPayload == r.tagPayload || cleanPayload.find(r.tagPayload) != std::string::npos));
            isMatch = uidOk && payOk;
        } else if (mode == "any") {
            isMatch = true;
        } else {
            isMatch = (r.tagUid == "*" || r.tagUid == tag.uid);
        }

        if (isMatch) {
            matched = true;
            if (!r.enabled) {
                AddLog(r.name + " is Disabled (OFF)", Theme::TextDim());
                break;
            }

            // INSTANT AUDIO FEEDBACK: Play assigned sound immediately on card recognition!
            if (g_config.soundEnabled) {
                SoundPlayer::playSound(r.sound, GetExeDir());
            }

            r.triggerCount++;
            r.lastTriggerTime = GetCurrentTimestamp();
            ConfigManager::save(g_configPath, g_config);

            // Check if this action is the Smart Lock / Hardware Auto-Unlock Card
            bool isSmartLock = false;
            std::string t = r.target;
            for (char& c : t) c = (char)tolower((unsigned char)c);
            if (t == "win+l" || t == "win_l" || t == "lock" ||
                t.find("smart_lock:") == 0 || t.find("toggle_lock:") == 0 ||
                (!t.empty() && std::all_of(t.begin(), t.end(), ::isdigit))) {
                isSmartLock = true;
            }
            if (r.tagUid == "00:00:00:00") {
                isSmartLock = true;
            }

            if (isSmartLock) {
                bool realTimeLocked = ActionExecutor::isWorkstationLocked();
                if (!realTimeLocked) {
                    // PC IS CURRENTLY UNLOCKED -> LOCK IT IMMEDIATELY VIA WIN32!
                    // This works 100% of the time, instantly, zero dependencies on Bluetooth!
                    LockWorkStation();
                    g_isSessionLocked = true;
                    g_nfc.sendCommand("SET_LOCK_STATE:1");
                    AddLog("Auto-Unlock Card Scanned: Locked PC (Win+L)", Theme::Amber());
                } else {
                    // PC IS CURRENTLY LOCKED -> ESP32 hardware BLE types the PIN to unlock.
                    // The desktop app leaves lock screen typing entirely to ESP32 BLE and avoids re-locking!
                    AddLog("Auto-Unlock Card Scanned: Unlocking PC via Hardware BLE...", Theme::CyanLight());
                }
            } else {
                AddLog("Triggering: " + r.name + "...", Theme::Cyan());
                bool realTimeLocked = ActionExecutor::isWorkstationLocked();
                g_isSessionLocked = realTimeLocked;
                ExecuteActionAsync(r, realTimeLocked, tag.bleHandled);
            }

            g_cardActionFired = true;
            g_cardLifted = false;
            g_activeCardUid = tag.uid;
            g_lastCardTriggerTime = now;
            InvalidateRect(g_hWnd, NULL, FALSE);
            break;
        }
    }

    // Auto-Open Unmapped Web URLs
    if (!matched) {
        std::string lowerPayload = cleanPayload;
        for (char& c : lowerPayload) c = (char)tolower((unsigned char)c);

        bool isUrl = (lowerPayload.rfind("http://", 0) == 0 || 
                      lowerPayload.rfind("https://", 0) == 0 ||
                      lowerPayload.rfind("www.", 0) == 0);

        if (lowerPayload.rfind("www.", 0) == 0) {
            cleanPayload = "https://" + cleanPayload;
            isUrl = true;
        }

        if (g_config.autoOpenUrls && isUrl) {
            HINSTANCE hInst = ShellExecuteA(NULL, "open", cleanPayload.c_str(), NULL, NULL, SW_SHOWNORMAL);
            if ((INT_PTR)hInst > 32) {
                if (g_config.soundEnabled) SoundPlayer::playActionSuccess();
                AddLog("Auto-opened Web URL: " + cleanPayload, Theme::Cyan());
                ShowTrayNotification("Opened URL in Browser", cleanPayload);
                g_cardActionFired = true;
                g_lastCardTriggerTime = now;
            } else {
                AddLog("Failed to open URL: " + cleanPayload, Theme::Rose());
            }
        } else {
            if (!cleanPayload.empty()) {
                if (g_config.soundEnabled) SoundPlayer::playTap();
                std::string logTxt = "Scanned Tag: " + tag.uid + " (" + tag.type + ") [Data: " + cleanPayload + "]";
                AddLog(logTxt, Theme::CyanLight());
                g_cardActionFired = true;
                g_lastCardTriggerTime = now;
            }
        }

        g_cardLifted = false;
        g_activeCardUid = tag.uid;
    }
}

// ---------------------------------------------------------------------------------
// IN-WINDOW DARK MODAL EDITOR
// ---------------------------------------------------------------------------------
void OpenModalEditor(const std::string& prefillUid, const std::string& prefillPayload, int editIndex) {
    g_modalActive = true;
    g_modalIsEdit = (editIndex >= 0 && editIndex < (int)g_config.rules.size());
    g_modalEditIndex = editIndex;

    if (g_modalIsEdit) {
        g_modalRule = g_config.rules[editIndex];
    } else {
        g_modalRule.id = "rule_" + to_str((long long)GetTickCount());
        g_modalRule.name = "";
        g_modalRule.tagUid = prefillUid;
        g_modalRule.tagPayload = prefillPayload;
        g_modalRule.matchMode = (!prefillPayload.empty() && prefillUid.empty()) ? "payload" : "uid";
        g_modalRule.target = prefillPayload;
        g_modalRule.args = "";
        g_modalRule.type = ACTION_APP;
        g_modalRule.silent = true;
        g_modalRule.enabled = true;
        g_modalRule.triggerCount = 0;
        g_modalRule.lastTriggerTime = "Never";

        if (!prefillPayload.empty()) {
            std::string lp = prefillPayload;
            std::transform(lp.begin(), lp.end(), lp.begin(), ::tolower);

            if (lp.find("http://") == 0 || lp.find("https://") == 0) {
                g_modalRule.type = ACTION_URL;
                std::string domain = prefillPayload;
                if (domain.find("https://") == 0) domain = domain.substr(8);
                else if (domain.find("http://") == 0) domain = domain.substr(7);
                if (domain.find("www.") == 0) domain = domain.substr(4);
                size_t slash = domain.find('/');
                if (slash != std::string::npos && slash < domain.length() - 1) {
                    domain = domain.substr(0, slash) + " (" + domain.substr(slash + 1) + ")";
                }
                g_modalRule.name = "Open " + domain;
            } else if (lp.find("spotify:") == 0) {
                g_modalRule.type = ACTION_SPOTIFY;
                g_modalRule.name = "Play Spotify";
            } else if (lp.find("steam://") == 0) {
                g_modalRule.type = ACTION_STEAM;
                g_modalRule.name = "Launch Steam Game";
            } else if (lp.find("win+") != std::string::npos || lp.find("ctrl+") != std::string::npos || lp.find("alt+") != std::string::npos || (prefillPayload.length() >= 4 && std::all_of(prefillPayload.begin(), prefillPayload.end(), ::isdigit))) {
                g_modalRule.type = ACTION_HOTKEY;
                g_modalRule.name = (std::all_of(prefillPayload.begin(), prefillPayload.end(), ::isdigit)) ? "Smart Lock / Unlock" : "Keystrokes / Hotkey";
            }
        }
    }

    // Sync match index
    if (g_modalRule.matchMode == "payload") g_modalMatchIndex = 1;
    else if (g_modalRule.matchMode == "both") g_modalMatchIndex = 2;
    else if (g_modalRule.matchMode == "any") g_modalMatchIndex = 3;
    else g_modalMatchIndex = 0;

    // Sync type index
    g_modalTypeIndex = (int)g_modalRule.type;
    if (g_modalTypeIndex < 0 || g_modalTypeIndex > 7) g_modalTypeIndex = 0;

    g_modalSilent = g_modalRule.silent;
    g_modalEnabled = g_modalRule.enabled;

    SetWindowTextW(g_hEditName, s2ws(g_modalRule.name).c_str());
    SetWindowTextW(g_hEditUid, s2ws(g_modalRule.tagUid).c_str());
    SetWindowTextW(g_hEditPayload, s2ws(g_modalRule.tagPayload).c_str());
    SetWindowTextW(g_hEditTarget, s2ws(g_modalRule.target).c_str());
    SetWindowTextW(g_hEditArgs, s2ws(g_modalRule.args).c_str());

    // Populate Sound ComboBox
    g_availableSounds = SoundPlayer::getAvailableSounds(GetExeDir());
    SendMessageW(g_hComboSound, CB_RESETCONTENT, 0, 0);
    int selectedSoundIndex = 0;
    std::string ruleSound = g_modalRule.sound.empty() ? "Default Glass Chime" : g_modalRule.sound;
    for (size_t i = 0; i < g_availableSounds.size(); i++) {
        SendMessageW(g_hComboSound, CB_ADDSTRING, 0, (LPARAM)s2ws(g_availableSounds[i]).c_str());
    }
    int foundIdx = (int)SendMessageW(g_hComboSound, CB_FINDSTRINGEXACT, -1, (LPARAM)s2ws(ruleSound).c_str());
    if (foundIdx != CB_ERR) {
        selectedSoundIndex = foundIdx;
    }
    SendMessageW(g_hComboSound, CB_SETCURSEL, selectedSoundIndex, 0);

    RECT rc;
    GetClientRect(g_hWnd, &rc);
    RepositionModalControls(rc.right - rc.left, rc.bottom - rc.top);
    UpdateControlsVisibility();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, TRUE);
}

void CloseModalEditor() {
    g_modalActive = false;
    UpdateControlsVisibility();
    if (g_hWnd) {
        SetFocus(g_hWnd);
        InvalidateRect(g_hWnd, NULL, TRUE);
    }
}

void SaveModalEditor() {
    wchar_t buf[1024];

    GetWindowTextW(g_hEditName, buf, 1023);
    g_modalRule.name = ws2s(buf);
    if (g_modalRule.name.empty()) g_modalRule.name = "Custom NFC Action";

    GetWindowTextW(g_hEditUid, buf, 1023);
    g_modalRule.tagUid = ws2s(buf);

    GetWindowTextW(g_hEditPayload, buf, 1023);
    g_modalRule.tagPayload = ws2s(buf);

    GetWindowTextW(g_hEditTarget, buf, 1023);
    g_modalRule.target = ws2s(buf);

    GetWindowTextW(g_hEditArgs, buf, 1023);
    g_modalRule.args = ws2s(buf);

    int selSound = (int)SendMessageW(g_hComboSound, CB_GETCURSEL, 0, 0);
    if (selSound != CB_ERR) {
        wchar_t soundNameBuf[256] = {0};
        SendMessageW(g_hComboSound, CB_GETLBTEXT, selSound, (LPARAM)soundNameBuf);
        g_modalRule.sound = ws2s(soundNameBuf);
    } else {
        g_modalRule.sound = "Default Glass Chime";
    }

    g_modalRule.type = (ActionType)g_modalTypeIndex;

    if (g_modalRule.type == ACTION_SPOTIFY) {
        g_modalRule.target = ActionExecutor::normalizeSpotifyTarget(g_modalRule.target);
        if (g_modalRule.name.empty() || g_modalRule.name == "Custom NFC Action" || g_modalRule.name == "New Automation") {
            if (g_modalRule.target == "resume") g_modalRule.name = "Resume Spotify";
            else g_modalRule.name = "Play Spotify Track";
        }
    }

    if (g_modalMatchIndex == 1) g_modalRule.matchMode = "payload";
    else if (g_modalMatchIndex == 2) g_modalRule.matchMode = "both";
    else if (g_modalMatchIndex == 3) g_modalRule.matchMode = "any";
    else g_modalRule.matchMode = "uid";

    g_modalRule.silent = g_modalSilent;
    g_modalRule.enabled = g_modalEnabled;

    if (g_modalIsEdit && g_modalEditIndex >= 0 && g_modalEditIndex < (int)g_config.rules.size()) {
        g_config.rules[g_modalEditIndex] = g_modalRule;
        AddLog("Updated Action: " + g_modalRule.name + " [Sound: " + g_modalRule.sound + "]", Theme::Cyan());
    } else {
        g_config.rules.push_back(g_modalRule);
        AddLog("Added New Action: " + g_modalRule.name + " [Sound: " + g_modalRule.sound + "]", Theme::Emerald());
    }

    ConfigManager::save(g_configPath, g_config);

    // If this is a numeric PIN rule or Smart PIN type, sync to ESP32 Flash NVS for Cold Boot Autonomous Unlock!
    bool isPurePin = !g_modalRule.target.empty() && std::all_of(g_modalRule.target.begin(), g_modalRule.target.end(), ::isdigit);
    if ((g_modalTypeIndex == 6 || isPurePin) && !g_modalRule.tagUid.empty() && g_modalRule.tagUid != "*") {
        g_nfc.sendCommand("SET_AUTO_UNLOCK:" + g_modalRule.tagUid + ":" + g_modalRule.target);
        AddLog("Synced Hardware Auto-Unlock to ESP32 Flash: " + g_modalRule.tagUid + " -> PIN " + g_modalRule.target, Theme::Emerald());
    }

    CloseModalEditor();
}

void UpdateControlsVisibility() {
    int modalShow = g_modalActive ? SW_SHOW : SW_HIDE;
    if (g_hEditName) ShowWindow(g_hEditName, modalShow);
    if (g_hEditUid) ShowWindow(g_hEditUid, modalShow);
    if (g_hEditPayload) ShowWindow(g_hEditPayload, modalShow);
    if (g_hEditTarget) ShowWindow(g_hEditTarget, modalShow);
    if (g_hEditArgs) ShowWindow(g_hEditArgs, modalShow);
    if (g_hComboSound) ShowWindow(g_hComboSound, modalShow);

    int writeShow = (!g_modalActive && g_activeTab == TAB_PROGRAMMER && g_writeFormatIndex != 2) ? SW_SHOW : SW_HIDE;
    if (g_hWritePayload) ShowWindow(g_hWritePayload, writeShow);
}

// ---------------------------------------------------------------------------------
// MAIN PAINT ENGINE (HIGH-DEFINITION 60 FPS DOUBLE-BUFFERED GDI+)
// ---------------------------------------------------------------------------------
void RenderUI(HDC hdc, int w, int h) {
    Bitmap memBmp(w, h, PixelFormat32bppARGB);
    Graphics g(&memBmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Deep Obsidian Canvas
    SolidBrush bgBrush(Theme::BgMain());
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    FontFamily ff(L"Segoe UI");
    SolidBrush textMainBrush(Theme::TextMain());
    SolidBrush textMutedBrush(Theme::TextMuted());
    SolidBrush textDimBrush(Theme::TextDim());

    // 1. Executive Top Header Bar
    int topBarH = 68;
    Theme::DrawCard(g, 16, 12, w - 32, topBarH, 10, Theme::Surface(), Theme::Border());

    // Vector NFC Wave Emblem
    Theme::DrawNfcEmblem(g, 30, 22, 44);

    Font fontBrand(&ff, 13.0f, FontStyleBold, UnitPoint);
    g.DrawString(L"NFC COMMAND DECK", -1, &fontBrand, PointF(86, 22), &textMainBrush);

    Font fontTagline(&ff, 7.5f, FontStyleBold, UnitPoint);
    g.DrawString(L"OBSIDIAN PRO STUDIO - ZERO-LATENCY DESKTOP AUTOMATION", -1, &fontTagline, PointF(88, 44), &textDimBrush);

    // Status Badge & Mute Toggle (Dynamically positioned without overlap)
    std::wstring connStr = s2ws(g_connectionStatusText);
    Color statColor = g_connected ? Theme::Emerald() : Theme::Amber();

    int audioW = 96;
    int audioX = w - 116;
    std::wstring audioLabel = g_config.soundEnabled ? L"AUDIO ON" : L"MUTED";
    Color audioColor = g_config.soundEnabled ? Theme::Cyan() : Theme::TextDim();
    Theme::DrawButton(g, audioX, 28, audioW, 28, audioLabel, Theme::Surface(), Theme::Border(), audioColor, false);

    FontFamily ffBadge(L"Segoe UI");
    Font fontBadge(&ffBadge, 8.5f, FontStyleBold, UnitPoint);
    RectF layoutRect(0, 0, 1000, 50);
    RectF bounds;
    g.MeasureString(connStr.c_str(), -1, &fontBadge, layoutRect, &bounds);
    int badgeW = (int)(bounds.Width + 30.0f);
    int badgeX = audioX - 12 - badgeW;
    Theme::DrawStatusBadge(g, badgeX, 28, connStr, statColor);

    // 2. Navigation Tab Bar (3 Segmented Studio Tabs)
    int tabY = 86;
    int tabH = 36;
    int tabW = (w - 32) / 3;

    auto DrawNavTab = [&](AppTab tab, int idx, const std::wstring& label) {
        int tx = 16 + idx * tabW;
        bool isActive = (g_activeTab == tab);
        Color tBg = isActive ? Theme::SurfaceHover() : Theme::Surface();
        Color tBorder = isActive ? Theme::Cyan() : Theme::Border();
        Color tText = isActive ? Theme::CyanLight() : Theme::TextMuted();

        Theme::DrawCard(g, tx, tabY, tabW - 4, tabH, 6, tBg, tBorder);
        Font fontTab(&ff, 9.0f, FontStyleBold, UnitPoint);
        SolidBrush tabBrush(tText);
        StringFormat tabFmt;
        tabFmt.SetAlignment(StringAlignmentCenter);
        tabFmt.SetLineAlignment(StringAlignmentCenter);
        RectF tabRect((float)tx, (float)tabY, (float)(tabW - 4), (float)tabH);
        g.DrawString(label.c_str(), -1, &fontTab, tabRect, &tabFmt, &tabBrush);
    };

    DrawNavTab(TAB_AUTOMATIONS, 0, L"AUTOMATIONS");
    DrawNavTab(TAB_PROGRAMMER,  1, L"TAG PROGRAMMER");
    DrawNavTab(TAB_SETTINGS,    2, L"HARDWARE & SETTINGS");

    // 3. Tab Contents
    if (g_activeTab == TAB_AUTOMATIONS) {
        // --- LIVE NFC SENSOR STAGE (HERO CARD) ---
        int stageY = 128;
        int stageH = 104;
        Theme::DrawCard(g, 16, stageY, w - 32, stageH, 10, Theme::Surface(), Theme::Border());

        bool cardActive = g_lastTag.cardPresent;
        bool hasScanned = !g_lastScannedCard.uid.empty();
        Color radarBg = cardActive ? Theme::EmeraldGlow() : (hasScanned ? Theme::CyanGlow() : Theme::SurfaceHover());
        Color radarBorder = cardActive ? Theme::Emerald() : (hasScanned ? Theme::Cyan() : Theme::Border());

        int heroW = w - 240;
        Theme::DrawCard(g, 26, stageY + 8, heroW, stageH - 16, 8, radarBg, radarBorder);

        // Indicator Micro-Dot
        SolidBrush dotBrush(cardActive ? Theme::Emerald() : (hasScanned ? Theme::Cyan() : Theme::TextDim()));
        g.FillEllipse(&dotBrush, 40, stageY + 22, 10, 10);

        std::string showUid = hasScanned ? g_lastScannedCard.uid : "--:--:--:--";
        std::string showType = hasScanned ? g_lastScannedCard.type : "None";
        std::string showData = hasScanned ? g_lastScannedCard.payload : "";

        Font fontTagTitle(&ff, 9.5f, FontStyleBold, UnitPoint);
        std::wstring statusStr = cardActive ? (L"NFC TAG MOUNTED: [ " + s2ws(showUid) + L" ]") : 
                                 (hasScanned ? (L"LAST SCANNED TAG: [ " + s2ws(showUid) + L" ]") : L"READY - TAP ANY NFC CARD ON SENSOR");
        SolidBrush statusBrush(cardActive ? Theme::Emerald() : (hasScanned ? Theme::CyanLight() : Theme::TextMain()));
        g.DrawString(statusStr.c_str(), -1, &fontTagTitle, PointF(58, stageY + 18), &statusBrush);

        Font fontTagSub(&ff, 8.0f, FontStyleRegular, UnitPoint);
        std::wstring infoStr = L"STANDARD: " + s2ws(showType) + L"   |   UID: " + s2ws(showUid);
        g.DrawString(infoStr.c_str(), -1, &fontTagSub, PointF(58, stageY + 38), &textDimBrush);

        // Payload Data line or Instruction
        if (!showData.empty()) {
            Font fontData(&ff, 8.0f, FontStyleBold, UnitPoint);
            SolidBrush dataBrush(Theme::CyanLight());
            std::wstring dataStr = L"DATA: " + s2ws(showData);
            if (dataStr.length() > 55) dataStr = dataStr.substr(0, 52) + L"...";
            g.DrawString(dataStr.c_str(), -1, &fontData, PointF(58, stageY + 58), &dataBrush);
        } else {
            Font fontHint(&ff, 7.5f, FontStyleRegular, UnitPoint);
            g.DrawString(L"Zero-latency detection active across MIFARE, NTAG213/215/216, & EMV tokens", -1, &fontHint, PointF(58, stageY + 58), &textDimBrush);
        }

        // Inline Action Assignment Button inside Hero Card if tag was scanned
        if (hasScanned) {
            Theme::DrawButton(g, 26 + heroW - 130, stageY + 20, 118, 28, L"+ Map Action", Theme::Surface(), Theme::Border(), Theme::Cyan(), (g_hoveredButtonId == 4));
        }

        // Top-Right of Stage: Interactive Action Controls
        int ctrlX = w - 200;
        Theme::DrawButton(g, ctrlX, stageY + 12, 174, 34, L"+ New Automation", Theme::Cyan(), Theme::Cyan(), Color(255, 9, 10, 15), (g_hoveredButtonId == 3), true);

        Color recBg = g_recordMode ? Theme::Amber() : Theme::SurfaceHover();
        Color recBorder = g_recordMode ? Theme::Amber() : Theme::Border();
        Color recText = g_recordMode ? Color(255, 9, 10, 15) : Theme::Amber();
        std::wstring recTextStr = g_recordMode ? L"[TAP CARD TO CAPTURE]" : L"Tap to Record";
        Theme::DrawButton(g, ctrlX, stageY + 54, 174, 34, recTextStr, recBg, recBorder, recText, (g_hoveredButtonId == 2));

        // --- ACTIVE AUTOMATIONS DECK ---
        int listY = 240;
        int listH = h - 365;
        Theme::DrawCard(g, 16, listY, w - 32, listH, 10, Theme::Surface(), Theme::Border());

        Font fontSecTitle(&ff, 9.5f, FontStyleBold, UnitPoint);
        g.DrawString(L"ACTIVE AUTOMATIONS DECK", -1, &fontSecTitle, PointF(32, listY + 14), &textMainBrush);

        // Master Automations Global Switch Toggle
        int masterX = w - 240;
        Theme::DrawToggleSwitch(g, masterX, listY + 12, g_config.masterAutomationsEnabled);
        Font fontMaster(&ff, 8.2f, FontStyleBold, UnitPoint);
        SolidBrush masterBrush(g_config.masterAutomationsEnabled ? Theme::Emerald() : Theme::Amber());
        g.DrawString(g_config.masterAutomationsEnabled ? L"MASTER: ACTIVE" : L"MASTER: OFF", -1, &fontMaster, PointF((float)(masterX + 44), (float)(listY + 14)), &masterBrush);

        Font fontCount(&ff, 8.0f, FontStyleBold, UnitPoint);
        std::wstring countStr = to_wstr((int)g_config.rules.size()) + L" CONFIGURED";
        g.DrawString(countStr.c_str(), -1, &fontCount, PointF((float)(w - 380), (float)(listY + 15)), &textDimBrush);

        if (g_config.rules.empty()) {
            Font fontEmpty(&ff, 9.0f, FontStyleRegular, UnitPoint);
            StringFormat centerFmt;
            centerFmt.SetAlignment(StringAlignmentCenter);
            centerFmt.SetLineAlignment(StringAlignmentCenter);
            RectF emptyRect((float)16, (float)(listY + 30), (float)(w - 32), (float)(listH - 40));
            g.DrawString(L"No automations configured yet.\nClick '+ New Automation' or tap 'Tap to Record' to map your NFC tags.", 
                         -1, &fontEmpty, emptyRect, &centerFmt, &textDimBrush);
        } else {
            int totalH = (int)g_config.rules.size() * 54;
            int maxScroll = (totalH > (listH - 46)) ? (totalH - (listH - 46)) : 0;
            if (g_scrollOffset > maxScroll) g_scrollOffset = maxScroll;
            if (g_scrollOffset < 0) g_scrollOffset = 0;

            Region origClip;
            g.GetClip(&origClip);
            Rect listClip(18, listY + 36, w - 36, listH - 44);
            g.SetClip(listClip);

            int itemY = listY + 40 - g_scrollOffset;
            for (size_t i = 0; i < g_config.rules.size(); i++) {
                const ActionRule& r = g_config.rules[i];
                if (itemY + 50 >= (listY + 36) && itemY <= (listY + listH)) {
                    bool hovered = ((int)i == g_hoveredRuleIndex);
                    Color rowBg = hovered ? Theme::SurfaceHover() : Color(255, 13, 17, 26);
                    Color rowBorder = r.enabled ? (hovered ? Theme::Cyan() : Theme::Border()) : Theme::Border();

                    Theme::DrawCard(g, 26, itemY, w - 52, 48, 6, rowBg, rowBorder);

                    // Action Type Badge
                    Theme::DrawActionBadge(g, 36, itemY + 13, r.type);

                    // Name & Details
                    Font fontRuleName(&ff, 9.0f, FontStyleBold, UnitPoint);
                    SolidBrush ruleNameBrush(r.enabled ? Theme::TextMain() : Theme::TextDim());
                    g.DrawString(s2ws(r.name).c_str(), -1, &fontRuleName, PointF(100, itemY + 8), &ruleNameBrush);

                    Font fontRuleSub(&ff, 7.8f, FontStyleRegular, UnitPoint);
                    std::string matchDesc = (r.matchMode == "payload") ? ("DATA: " + r.tagPayload) : ("UID: " + (r.tagUid.empty() ? "*" : r.tagUid));
                    std::wstring targetDisp = s2ws(r.target);
                    if (r.type == ACTION_SPOTIFY) {
                        if (r.target.empty() || r.target == "resume" || r.target == "play" || r.target == "last") {
                            targetDisp = L"Resume Last Played Track";
                        } else if (r.target.find("spotify:track:") == 0) {
                            targetDisp = L"Track: " + s2ws(r.target.substr(14));
                        } else if (r.target.find("spotify:playlist:") == 0) {
                            targetDisp = L"Playlist: " + s2ws(r.target.substr(17));
                        } else if (r.target.find("spotify:album:") == 0) {
                            targetDisp = L"Album: " + s2ws(r.target.substr(14));
                        }
                        if (targetDisp.length() > 6 && targetDisp.rfind(L":play") == targetDisp.length() - 5) {
                            targetDisp = targetDisp.substr(0, targetDisp.length() - 5);
                        }
                    }
                    std::wstring subDetails = s2ws(matchDesc) + L"   -   " + targetDisp;
                    if (!r.sound.empty() && r.sound != "Default Glass Chime" && r.sound != "default") {
                        subDetails += L"   [ " + s2ws(r.sound) + L" ]";
                    }
                    if (subDetails.length() > 54) subDetails = subDetails.substr(0, 51) + L"...";
                    g.DrawString(subDetails.c_str(), -1, &fontRuleSub, PointF(100, itemY + 27), &textDimBrush);

                    // Action Controls: Toggle [Switch], Run [Run], Edit [Edit], Del [X]
                    Theme::DrawToggleSwitch(g, w - 210, itemY + 14, r.enabled);
                    Theme::DrawButton(g, w - 160, itemY + 11, 38, 26, L"Run", Theme::Surface(), Theme::Border(), Theme::CyanLight(), false);
                    Theme::DrawButton(g, w - 114, itemY + 11, 40, 26, L"Edit", Theme::Surface(), Theme::Border(), Theme::TextMuted(), false);
                    Theme::DrawButton(g, w - 66, itemY + 11, 28, 26, L"X", Theme::Surface(), Theme::Border(), Theme::Rose(), false);
                }
                itemY += 54;
            }

            g.SetClip(&origClip);
        }
    } else if (g_activeTab == TAB_PROGRAMMER) {
        // --- 100% STUDIO GDI+ TAG PROGRAMMER DECK ---
        int progY = 128;
        int progH = h - 245;
        Theme::DrawCard(g, 16, progY, w - 32, progH, 10, Theme::Surface(), Theme::Border());

        Font fontHead(&ff, 11.5f, FontStyleBold, UnitPoint);
        g.DrawString(L"NFC CARD PROGRAMMER & WRITER", -1, &fontHead, PointF(32, progY + 18), &textMainBrush);

        Font fontDesc(&ff, 8.2f, FontStyleRegular, UnitPoint);
        g.DrawString(L"Encode URLs, Spotify links, plain text notes, or wipe NTAG213/215/216 & Mifare Classic tags.", 
                     -1, &fontDesc, PointF(32, progY + 42), &textDimBrush);

        // Section 1: Format Picker (3 Segmented Studio Pills)
        Font fontSec(&ff, 8.5f, FontStyleBold, UnitPoint);
        g.DrawString(L"Select Write Format:", -1, &fontSec, PointF(32, progY + 76), &textMainBrush);

        auto DrawFormatPill = [&](int idx, int x, int pw, const std::wstring& label) {
            bool isSel = (g_writeFormatIndex == idx);
            Color pBg = isSel ? Theme::CyanGlow() : Theme::SurfaceHover();
            Color pBorder = isSel ? Theme::Cyan() : Theme::Border();
            Color pText = isSel ? Theme::CyanLight() : Theme::TextMuted();

            Theme::DrawCard(g, x, progY + 98, pw, 34, 6, pBg, pBorder);
            Font fp(&ff, 8.5f, FontStyleBold, UnitPoint);
            SolidBrush pb(pText);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            RectF pr((float)x, (float)(progY + 98), (float)pw, 34.0f);
            g.DrawString(label.c_str(), -1, &fp, pr, &sf, &pb);
        };

        DrawFormatPill(0, 32, 190, L"Website / URL");
        DrawFormatPill(1, 232, 160, L"Plain Text");
        DrawFormatPill(2, 402, 160, L"Wipe Tag");

        // Section 2: Payload Data Input Card
        int inputCardY = progY + 148;
        if (g_writeFormatIndex == 2) {
            // Wipe Mode Info Card
            g.DrawString(L"Wipe Mode Selected:", -1, &fontSec, PointF(32, inputCardY), &textMainBrush);
            Theme::DrawCard(g, 32, inputCardY + 22, w - 64, 46, 8, Theme::SurfaceHover(), Theme::Amber());
            Font fw(&ff, 8.5f, FontStyleRegular, UnitPoint);
            SolidBrush bAmber(Theme::Amber());
            g.DrawString(L"[!] All user data sectors on the detected tag will be zeroed (0x00) and reset to factory blank.", 
                         -1, &fw, PointF(46, inputCardY + 36), &bAmber);
        } else {
            std::wstring inputLbl = (g_writeFormatIndex == 0) ? L"Enter Website or Social Link (URL):" : L"Enter Plain Text Payload Data:";
            g.DrawString(inputLbl.c_str(), -1, &fontSec, PointF(32, inputCardY), &textMainBrush);

            // Dark rounded container for g_hWritePayload
            Theme::DrawCard(g, 32, inputCardY + 22, w - 64, 42, 6, Theme::SurfaceInput(), Theme::BorderLight());
        }

        // Section 3: Native Studio Action Buttons
        int btnY = progY + 226;
        if (g_writerArmed) {
            Theme::DrawButton(g, 32, btnY, 240, 38, L"ARMED: Tap Card Now", Theme::Amber(), Theme::Amber(), Color(255, 9, 10, 15), false, true);
        } else {
            Theme::DrawButton(g, 32, btnY, 240, 38, L"Arm Reader & Program Tag", Theme::Cyan(), Theme::Cyan(), Color(255, 9, 10, 15), false, true);
        }
        Theme::DrawButton(g, 284, btnY, 150, 38, L"Cancel / Disarm", Theme::Surface(), Theme::Border(), Theme::TextMuted(), false);

        // Section 4: Live Telemetry & Status Card
        int statY = progY + 280;
        Color statusBg = g_writerArmed ? Theme::AmberGlow() : Theme::SurfaceHover();
        Color statusBorder = g_writerArmed ? Theme::Amber() : Theme::Border();
        Theme::DrawCard(g, 32, statY, w - 64, 88, 8, statusBg, statusBorder);

        Font fontStatus(&ff, 9.0f, FontStyleBold, UnitPoint);
        SolidBrush statBrush(g_writerArmed ? Theme::Amber() : (g_writerStatusText.find("success") != std::string::npos ? Theme::Emerald() : Theme::TextMuted()));
        std::wstring statLine = g_writerArmed ? L"ARM ACTIVE: Place blank NFC card on sensor to program..." : s2ws(g_writerStatusText);
        g.DrawString(statLine.c_str(), -1, &fontStatus, PointF(48, statY + 16), &statBrush);

        Font fontNotes(&ff, 7.8f, FontStyleRegular, UnitPoint);
        g.DrawString(L"* Compatible Tags: NTAG213, NTAG215, NTAG216, Mifare Classic 1K / 4K.", -1, &fontNotes, PointF(48, statY + 40), &textDimBrush);
        g.DrawString(L"* Smartphone Ready: NDEF URLs written here launch automatically on Android & iOS without any app.", -1, &fontNotes, PointF(48, statY + 58), &textDimBrush);

    } else if (g_activeTab == TAB_SETTINGS) {
        // --- HARDWARE TELEMETRY & SYSTEM PREFERENCES ---
        int setY = 128;
        int setH = h - 245;
        Theme::DrawCard(g, 16, setY, w - 32, setH, 10, Theme::Surface(), Theme::Border());

        Font fontHead(&ff, 11.5f, FontStyleBold, UnitPoint);
        g.DrawString(L"HARDWARE INTERFACE & TELEMETRY", -1, &fontHead, PointF(32, setY + 16), &textMainBrush);

        // Row 1: Hardware COM Port Selection
        int portY = setY + 48;
        Theme::DrawCard(g, 32, portY, w - 64, 52, 8, Theme::SurfaceHover(), Theme::Border());
        Font fl(&ff, 9.0f, FontStyleBold, UnitPoint);
        g.DrawString(L"Active COM Port Selection", -1, &fl, PointF(46, portY + 9), &textMainBrush);
        Font fd(&ff, 7.5f, FontStyleRegular, UnitPoint);
        g.DrawString(L"Select target serial port for ESP32-C3 SuperMini reader", -1, &fd, PointF(46, portY + 29), &textDimBrush);

        std::vector<std::string> ports = {"COM8", "COM1", "COM5", "COM6", "Auto"};
        int px = w - 340;
        for (const auto& p : ports) {
            bool isCurrent = (g_config.comPort == p || (p == "Auto" && g_config.comPort == "auto"));
            Color pBg = isCurrent ? Theme::Cyan() : Theme::Surface();
            Color pBorder = isCurrent ? Theme::Cyan() : Theme::Border();
            Color pText = isCurrent ? Color(255, 9, 10, 15) : Theme::TextMuted();
            Theme::DrawButton(g, px, portY + 12, 50, 28, s2ws(p), pBg, pBorder, pText, false);
            px += 54;
        }

        // Row 2: Diagnostics & Bluetooth Tools (Separated vertically, zero overlap!)
        int diagY = setY + 108;
        Theme::DrawCard(g, 32, diagY, w - 64, 52, 8, Theme::SurfaceHover(), Theme::Border());
        g.DrawString(L"Diagnostic & Bluetooth Tools", -1, &fl, PointF(46, diagY + 9), &textMainBrush);
        g.DrawString(L"Probe reader telemetry or clear stale BLE pairing bonds", -1, &fd, PointF(46, diagY + 29), &textDimBrush);

        Theme::DrawButton(g, w - 215, diagY + 12, 60, 28, L"Ping", Theme::Surface(), Theme::Border(), Theme::Emerald(), false);
        Theme::DrawButton(g, w - 145, diagY + 12, 95, 28, L"Reset BLE", Theme::Surface(), Theme::Border(), Theme::Amber(), false);

        // Row 3: Wake-on-LAN (WoL) PC Auto Power-On
        int wolY = setY + 168;
        Theme::DrawCard(g, 32, wolY, w - 64, 52, 8, Theme::SurfaceHover(), Theme::Border());
        g.DrawString(L"Wake-on-LAN (WoL) Auto Power-On", -1, &fl, PointF(46, wolY + 9), &textMainBrush);
        std::wstring wolDesc = L"Wake PC via Wi-Fi Magic Packet on card tap [Target MAC: " + s2ws(g_config.wolMac) + L"]";
        g.DrawString(wolDesc.c_str(), -1, &fd, PointF(46, wolY + 29), &textDimBrush);

        Theme::DrawButton(g, w - 265, wolY + 12, 72, 28, L"Test WoL", Theme::Surface(), Theme::Border(), Theme::CyanLight(), false);
        Theme::DrawButton(g, w - 185, wolY + 12, 70, 28, L"Sync MAC", Theme::Surface(), Theme::Border(), Theme::Amber(), false);
        std::wstring wolToggleText = g_config.wolEnabled ? L"ON" : L"OFF";
        Color wolToggleCol = g_config.wolEnabled ? Theme::Emerald() : Theme::TextDim();
        Theme::DrawButton(g, w - 105, wolY + 12, 59, 28, wolToggleText, Theme::Surface(), Theme::Border(), wolToggleCol, false);

        // Row 4: Windows Background Service (Cold Boot Daemon)
        int svcY = setY + 228;
        Theme::DrawCard(g, 32, svcY, w - 64, 52, 8, Theme::SurfaceHover(), Theme::Border());
        g.DrawString(L"Windows Background Service (Cold Boot Daemon)", -1, &fl, PointF(46, svcY + 9), &textMainBrush);
        g.DrawString(L"Starts at boot before logon to unlock Windows with zero CPU & 1MB RAM", -1, &fd, PointF(46, svcY + 29), &textDimBrush);

        int svcStatus = GetBackgroundServiceStatus();
        std::wstring svcText = (svcStatus == 1) ? L"RUNNING" : (svcStatus == 2) ? L"STOPPED" : L"NOT INSTALLED";
        Color svcCol = (svcStatus == 1) ? Theme::Emerald() : (svcStatus == 2) ? Theme::Amber() : Theme::TextDim();
        Theme::DrawButton(g, w - 235, svcY + 12, 115, 28, svcText, Theme::Surface(), Theme::Border(), svcCol, false);

        std::wstring svcActionText = (svcStatus == 0) ? L"Install" : L"Remove";
        Color svcActionCol = (svcStatus == 0) ? Theme::CyanLight() : Theme::Rose();
        Theme::DrawButton(g, w - 110, svcY + 12, 64, 28, svcActionText, Theme::Surface(), Theme::Border(), svcActionCol, false);

        auto DrawSettingRow = [&](int y, const std::wstring& label, const std::wstring& desc, const std::wstring& val, Color valCol) {
            Theme::DrawCard(g, 32, y, w - 64, 52, 8, Theme::SurfaceHover(), Theme::Border());
            Font fl2(&ff, 9.0f, FontStyleBold, UnitPoint);
            g.DrawString(label.c_str(), -1, &fl2, PointF(46, y + 9), &textMainBrush);
            Font fd2(&ff, 7.5f, FontStyleRegular, UnitPoint);
            g.DrawString(desc.c_str(), -1, &fd2, PointF(46, y + 29), &textDimBrush);
            Theme::DrawButton(g, w - 110, y + 12, 64, 28, val, Theme::Surface(), Theme::Border(), valCol, false);
        };

        // Row 5: Auto-start Studio on Windows Logon
        bool autoStart = IsAutoStartOnLogon();
        DrawSettingRow(setY + 288, L"Launch Studio on Windows Logon", L"Start NFC Command Deck minimized in the system tray on user login",
                       autoStart ? L"ON" : L"OFF", autoStart ? Theme::Emerald() : Theme::TextDim());

        // Row 6: Desktop Tray Notifications
        DrawSettingRow(setY + 348, L"Desktop Tray Notifications", L"Show Windows toast notifications when cards trigger automations", 
                       g_config.notificationsEnabled ? L"ON" : L"OFF", g_config.notificationsEnabled ? Theme::Emerald() : Theme::TextDim());

        // Row 7: Synthesized Audio Chimes + Sounds Folder
        DrawSettingRow(setY + 408, L"Synthesized Audio Chimes", L"Play audio feedback cues on hardware scan and action execution", 
                       g_config.soundEnabled ? L"ON" : L"OFF", g_config.soundEnabled ? Theme::Emerald() : Theme::TextDim());
        Theme::DrawButton(g, w - 215, setY + 408 + 12, 95, 28, L"Sounds Folder", Theme::Surface(), Theme::Border(), Theme::CyanLight(), false);

        // Row 8: Auto-Open URLs
        DrawSettingRow(setY + 468, L"Auto-Open Unmapped Web URLs", L"Open default browser when an unmapped tag containing a URL is tapped", 
                       g_config.autoOpenUrls ? L"ON" : L"OFF", g_config.autoOpenUrls ? Theme::Emerald() : Theme::TextDim());

        // Row 9: Minimize to Tray
        DrawSettingRow(setY + 528, L"Minimize to Notification Tray", L"Keep daemon running in Windows system tray when closed or minimized", 
                       g_config.minimizeToTray ? L"ON" : L"OFF", g_config.minimizeToTray ? Theme::Emerald() : Theme::TextDim());

        // Row 10: ESP32 Web Portal (On-demand Wi-Fi)
        int row10Y = setY + 588;
        Theme::DrawCard(g, 32, row10Y, w - 64, 52, 8, Theme::SurfaceHover(), Theme::Border());
        Font flPort(&ff, 9.0f, FontStyleBold, UnitPoint);
        g.DrawString(L"ESP32 Web Portal (Wi-Fi)", -1, &flPort, PointF(46, row10Y + 9), &textMainBrush);
        Font fdPort(&ff, 7.5f, FontStyleRegular, UnitPoint);
        g.DrawString(L"On-demand ESP32 web portal. Kept OFF by default for BLE stability.", -1, &fdPort, PointF(46, row10Y + 29), &textDimBrush);

        std::wstring portalBtnText = g_portalActive ? L"Stop Portal" : L"Start Portal";
        Color portalBtnCol = g_portalActive ? Theme::Amber() : Theme::Emerald();
        Theme::DrawButton(g, w - 215, row10Y + 12, 95, 28, portalBtnText, Theme::Surface(), Theme::Border(), portalBtnCol, false);
        Theme::DrawButton(g, w - 110, row10Y + 12, 64, 28, L"Open", Theme::Surface(), Theme::Border(), Theme::CyanLight(), false);
    }

    // 4. Studio Live Event Stream (Footer Console)
    int logY = h - 94;
    Theme::DrawCard(g, 16, logY, w - 32, 80, 8, Theme::Surface(), Theme::Border());

    Font fontLogHead(&ff, 7.5f, FontStyleBold, UnitPoint);
    g.DrawString(L"LIVE TELEMETRY & HARDWARE EVENT STREAM", -1, &fontLogHead, PointF(28, logY + 8), &textDimBrush);

    Font fontLog(&ff, 8.0f, FontStyleRegular, UnitPoint);
    int logItemY = logY + 26;
    for (size_t i = 0; i < 3 && i < g_activityLog.size(); i++) {
        const LogEntry& le = g_activityLog[i];
        std::wstring line = L"[" + s2ws(le.timestamp) + L"]  " + s2ws(le.text);
        SolidBrush logBrush(le.color);
        g.DrawString(line.c_str(), -1, &fontLog, PointF(28, logItemY), &logBrush);
        logItemY += 17;
    }

    // 5. IN-WINDOW DARK MODAL OVERLAY (Solid Dark Canvas, Zero Bleed-Through)
    if (g_modalActive) {
        SolidBrush dimBrush(Color(250, 9, 10, 15));
        g.FillRectangle(&dimBrush, 0, 0, w, h);

        int mw = 620;
        int mh = 690;
        int mx = (w - mw) / 2;
        int my = (h - mh) / 2 - 10;

        Theme::DrawCard(g, mx, my, mw, mh, 12, Theme::Surface(), Theme::Cyan());

        Font fontModTitle(&ff, 12.0f, FontStyleBold, UnitPoint);
        std::wstring modTitle = g_modalIsEdit ? L"EDIT NFC AUTOMATION ACTION" : L"CONFIGURE NFC AUTOMATION";
        g.DrawString(modTitle.c_str(), -1, &fontModTitle, PointF(mx + 28, my + 18), &textMainBrush);

        Font fontModDesc(&ff, 8.0f, FontStyleRegular, UnitPoint);
        g.DrawString(L"Map physical NFC card scan to applications, web links, keystrokes, or smart unlock.", 
                     -1, &fontModDesc, PointF(mx + 28, my + 42), &textDimBrush);

        Font fontLbl(&ff, 8.2f, FontStyleBold, UnitPoint);

        // Field 1: Name (Card: mx+28, my+84, mw-56, 34)
        g.DrawString(L"Rule Friendly Name:", -1, &fontLbl, PointF(mx + 28, my + 64), &textMainBrush);
        Theme::DrawCard(g, mx + 28, my + 84, mw - 56, 34, 6, Theme::SurfaceInput(), Theme::BorderLight());

        // Field 2: Match Mode Pills (my+144, h=28)
        g.DrawString(L"Trigger Match Mode:", -1, &fontLbl, PointF(mx + 28, my + 126), &textMainBrush);
        auto DrawMatchPill = [&](int idx, int px, int pw, const std::wstring& plbl) {
            bool isSel = (g_modalMatchIndex == idx);
            Color pBg = isSel ? Theme::CyanGlow() : Theme::SurfaceHover();
            Color pBorder = isSel ? Theme::Cyan() : Theme::Border();
            Color pText = isSel ? Theme::CyanLight() : Theme::TextMuted();
            Theme::DrawCard(g, px, my + 144, pw, 28, 5, pBg, pBorder);
            Font fp(&ff, 8.0f, FontStyleBold, UnitPoint);
            SolidBrush pb(pText);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            RectF pr((float)px, (float)(my + 144), (float)pw, 28.0f);
            g.DrawString(plbl.c_str(), -1, &fp, pr, &sf, &pb);
        };
        DrawMatchPill(0, mx + 28, 120, L"Card UID");
        DrawMatchPill(1, mx + 154, 150, L"Tag Payload / URL");
        DrawMatchPill(2, mx + 310, 160, L"Both UID & Payload");

        // Field 3: UID + Paste Button (Card: mx+28, my+198, mw-190, 34 | Btn: mx+mw-152, my+198, 124, 34)
        g.DrawString(L"NFC Card UID (* for any card):", -1, &fontLbl, PointF(mx + 28, my + 180), &textMainBrush);
        Theme::DrawCard(g, mx + 28, my + 198, mw - 190, 34, 6, Theme::SurfaceInput(), Theme::BorderLight());
        Theme::DrawButton(g, mx + mw - 152, my + 198, 124, 34, L"Paste Scanned", Theme::Surface(), Theme::Border(), Theme::Cyan(), false);

        // Field 4: Payload Filter (Card: mx+28, my+258, mw-56, 34)
        g.DrawString(L"Tag Payload Filter (Optional):", -1, &fontLbl, PointF(mx + 28, my + 240), &textMainBrush);
        Theme::DrawCard(g, mx + 28, my + 258, mw - 56, 34, 6, Theme::SurfaceInput(), Theme::BorderLight());

        // Field 5: Action Type Grid (2x4 Cards at my+320, h=30)
        g.DrawString(L"Action Type to Execute:", -1, &fontLbl, PointF(mx + 28, my + 300), &textMainBrush);
        struct TypeDef { int id; std::wstring label; };
        TypeDef types[8] = {
            { 0, L"App / Game" },
            { 1, L"Steam" },
            { 2, L"Spotify" },
            { 3, L"Media Key" },
            { 4, L"Shell Script" },
            { 5, L"Web URL" },
            { 6, L"Smart PIN" },
            { 7, L"Lock PC" }
        };

        int cardW = (mw - 56 - 18) / 4;
        for (int i = 0; i < 8; i++) {
            int row = i / 4;
            int col = i % 4;
            int cx = mx + 28 + col * (cardW + 6);
            int cy = my + 320 + row * 36;

            bool isSel = (g_modalTypeIndex == types[i].id);
            Color cBg = isSel ? Theme::CyanGlow() : Theme::SurfaceHover();
            Color cBorder = isSel ? Theme::Cyan() : Theme::Border();
            Color cText = isSel ? Theme::CyanLight() : Theme::TextMuted();

            Theme::DrawCard(g, cx, cy, cardW, 30, 5, cBg, cBorder);
            Font ft(&ff, 8.0f, FontStyleBold, UnitPoint);
            SolidBrush tb(cText);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            RectF cr((float)cx, (float)cy, (float)cardW, 30.0f);
            g.DrawString(types[i].label.c_str(), -1, &ft, cr, &sf, &tb);
        }

        // Field 6: Target Value + Browse (my+414, h=34)
        std::wstring targetLbl = L"Target Path / Value / PIN:";
        if (g_modalTypeIndex == 0) targetLbl = L"Application Path (.exe / .lnk) or Game:";
        else if (g_modalTypeIndex == 1) targetLbl = L"Steam Game ID (e.g. 730 for CS2):";
        else if (g_modalTypeIndex == 2) targetLbl = L"Spotify Song/Playlist/Album Link (or click 'Resume Last'):";
        else if (g_modalTypeIndex == 3) targetLbl = L"Media Command (play_pause, next, prev, vol_up, vol_down):";
        else if (g_modalTypeIndex == 4) targetLbl = L"Shell Command or Script (.bat, .ps1):";
        else if (g_modalTypeIndex == 5) targetLbl = L"Website URL (e.g. https://...):";
        else if (g_modalTypeIndex == 6) targetLbl = L"Windows Hello PIN (e.g. 1234) or Keystrokes:";
        else if (g_modalTypeIndex == 7) targetLbl = L"System Command (lock, sleep, monitor_off):";

        g.DrawString(targetLbl.c_str(), -1, &fontLbl, PointF(mx + 28, my + 394), &textMainBrush);

        bool isAppOrScript = (g_modalTypeIndex == 0 || g_modalTypeIndex == 4);
        bool isSpotify = (g_modalTypeIndex == 2);
        int targetCardW = isAppOrScript ? (mw - 160) : (isSpotify ? (mw - 250) : (mw - 56));
        Theme::DrawCard(g, mx + 28, my + 414, targetCardW, 34, 6, Theme::SurfaceInput(), Theme::BorderLight());
        if (isAppOrScript) {
            Theme::DrawButton(g, mx + mw - 122, my + 414, 94, 34, L"Browse...", Theme::Surface(), Theme::Border(), Theme::TextMuted(), false);
        } else if (isSpotify) {
            Theme::DrawButton(g, mx + mw - 212, my + 414, 84, 34, L"Paste Link", Theme::Surface(), Theme::Border(), Theme::Cyan(), false);
            Theme::DrawButton(g, mx + mw - 120, my + 414, 92, 34, L"Resume Last", Theme::Surface(), Theme::Border(), Theme::Emerald(), false);
        }

        // Field 7: Arguments (my+473, h=34)
        g.DrawString(L"Execution Arguments (Optional):", -1, &fontLbl, PointF(mx + 28, my + 456), &textMainBrush);
        Theme::DrawCard(g, mx + 28, my + 473, mw - 56, 34, 6, Theme::SurfaceInput(), Theme::BorderLight());

        // Field 8: Trigger Sound Effect (my+514)
        g.DrawString(L"Trigger Sound Effect:", -1, &fontLbl, PointF(mx + 28, my + 514), &textMainBrush);
        Theme::DrawButton(g, mx + 400, my + 532, 98, 28, L"Preview", Theme::Surface(), Theme::Border(), Theme::CyanLight(), false);
        Theme::DrawButton(g, mx + 504, my + 532, 88, 28, L"Sounds", Theme::Surface(), Theme::Border(), Theme::TextMuted(), false);

        // Field 9: Switches & Buttons (my+580)
        Theme::DrawToggleSwitch(g, mx + 32, my + 580, g_modalSilent);
        Font fontOpt(&ff, 8.2f, FontStyleRegular, UnitPoint);
        g.DrawString(L"Silent Mode (Hide Console)", -1, &fontOpt, PointF(mx + 76, my + 580), &textMutedBrush);

        Theme::DrawToggleSwitch(g, mx + 280, my + 580, g_modalEnabled);
        g.DrawString(L"Automation Active (Enabled)", -1, &fontOpt, PointF(mx + 324, my + 580), &textMutedBrush);

        // Save & Cancel Buttons (my+630)
        Theme::DrawButton(g, mx + mw - 264, my + 630, 154, 38, L"Save Automation", Theme::Cyan(), Theme::Cyan(), Color(255, 9, 10, 15), false, true);
        Theme::DrawButton(g, mx + mw - 98, my + 630, 70, 38, L"Cancel", Theme::Surface(), Theme::Border(), Theme::TextMuted(), false);
    }

    // Direct blit to screen
    Graphics screenG(hdc);
    screenG.DrawImage(&memBmp, 0, 0);
}

// ---------------------------------------------------------------------------------
// WINDOW PROCEDURE & INTERACTIVE DISPATCHER
// ---------------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_hWnd = hWnd;
            g_appStartTime = GetTickCount();

            g_hDarkBrush = CreateSolidBrush(RGB(15, 19, 29));
            g_hInputBrush = CreateSolidBrush(RGB(20, 26, 40));

            g_hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

            // Modal Pure Borderless Edit Controls (No WS_EX_CLIENTEDGE!)
            g_hEditName = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                          0, 0, 100, 22, hWnd, (HMENU)IDC_MODAL_NAME, g_hInstance, NULL);

            g_hEditUid = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                         0, 0, 100, 22, hWnd, (HMENU)IDC_MODAL_UID, g_hInstance, NULL);

            g_hEditPayload = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                             0, 0, 100, 22, hWnd, (HMENU)IDC_MODAL_PAYLOAD, g_hInstance, NULL);

            g_hEditTarget = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                            0, 0, 100, 22, hWnd, (HMENU)IDC_MODAL_TARGET, g_hInstance, NULL);

            g_hEditArgs = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                          0, 0, 100, 20, hWnd, (HMENU)IDC_MODAL_ARGS, g_hInstance, NULL);

            g_hComboSound = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                            0, 0, 100, 300, hWnd, (HMENU)IDC_MODAL_SOUND, g_hInstance, NULL);

            // Writer Tab Pure Borderless Edit Control (No WS_EX_CLIENTEDGE!)
            int progY = 128;
            int inputCardY = progY + 148;
            g_hWritePayload = CreateWindowExW(0, L"EDIT", L"https://", WS_CHILD | ES_AUTOHSCROLL,
                                              42, inputCardY + 30, 760 - 84, 24, hWnd, (HMENU)IDC_WRITE_PAYLOAD, g_hInstance, NULL);

            HWND edits[] = { g_hEditName, g_hEditUid, g_hEditPayload, g_hEditTarget, g_hEditArgs, g_hWritePayload };
            for (HWND e : edits) {
                SetWindowTheme(e, L"", L"");
                SendMessageW(e, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            }
            SendMessageW(g_hComboSound, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            RECT rc;
            GetClientRect(hWnd, &rc);
            RepositionModalControls(rc.right - rc.left, rc.bottom - rc.top);
            UpdateControlsVisibility();

            // System Tray Notification Icon
            ZeroMemory(&g_nid, sizeof(g_nid));
            g_nid.cbSize = sizeof(g_nid);
            g_nid.hWnd = hWnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAY_ICON;
            g_nid.hIcon = g_hAppIcon;
            wcscpy(g_nid.szTip, L"NFC Command Deck Pro");
            Shell_NotifyIconW(NIM_ADD, &g_nid);

            // Load Configuration
            ConfigManager::load(g_configPath, g_config);

            // Register Callbacks & Start NFC Engine
            g_nfc.setCallbacks(
                [](const TagEvent& tag) {
                    TagEvent* copy = new TagEvent(tag);
                    PostMessageW(g_hWnd, WM_NFC_TAG_EVENT, 0, (LPARAM)copy);
                },
                [](bool connected, const std::string& info) {
                    std::string* pInfo = new std::string(info);
                    PostMessageW(g_hWnd, WM_NFC_STATUS_EVENT, connected ? 1 : 0, (LPARAM)pInfo);
                }
            );

            g_isSessionLocked = ActionExecutor::isWorkstationLocked();
            g_nfc.start(g_config.connectionMode, g_config.comPort, g_config.wifiIp, g_config.baudRate);
            SetTimer(hWnd, 1001, 1500, NULL);
            break;
        }

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (g_hWritePayload) {
                int progY = 128;
                int inputCardY = progY + 148;
                SetWindowPos(g_hWritePayload, NULL, 42, inputCardY + 30, w - 84, 24, SWP_NOZORDER);
            }
            RepositionModalControls(w, h);
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(248, 250, 252));
            SetBkColor(hdc, RGB(20, 26, 40));
            return (INT_PTR)g_hInputBrush;
        }

        case WM_NFC_TAG_EVENT: {
            TagEvent* pTag = (TagEvent*)lParam;
            if (pTag) {
                g_lastTag = *pTag;
                if (!pTag->cardPresent) {
                    g_cardLifted = true;
                    g_cardActionFired = false;
                    g_activeCardUid = "";
                    AddLog("Card Lifted from Reader.", Theme::TextDim());
                } else {
                    ProcessTagTrigger(*pTag);
                }
                delete pTag;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }

        case WM_ACTION_RESULT: {
            AsyncActionResult* res = (AsyncActionResult*)lParam;
            if (res) {
                if (res->success) {
                    AddLog("Triggered: " + res->ruleName + " [Sound: " + res->sound + "] (" + res->statusMsg + ")", Theme::Emerald());
                    ShowTrayNotification("NFC Action Executed", res->ruleName + "\n" + res->statusMsg);
                } else {
                    if (g_config.soundEnabled) SoundPlayer::playError();
                    AddLog("Failed: " + res->ruleName + " - " + res->statusMsg, Theme::Rose());
                }
                delete res;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }

        case WM_NFC_STATUS_EVENT: {
            g_connected = (wParam == 1);
            std::string* pInfo = (std::string*)lParam;
            if (pInfo) {
                g_connectionStatusText = *pInfo;
                AddLog(g_connected ? "Hardware Reader Connected: " + *pInfo : "Hardware Reader Disconnected (" + *pInfo + ")", 
                       g_connected ? Theme::Emerald() : Theme::Amber());
                delete pInfo;
            }
            if (g_connected) {
                bool realTimeLocked = ActionExecutor::isWorkstationLocked();
                g_isSessionLocked = realTimeLocked;
                g_nfc.sendCommand(std::string("SET_LOCK_STATE:") + (realTimeLocked ? "1" : "0"));
            }
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        case WM_WTSSESSION_CHANGE: {
            if (wParam == WTS_SESSION_LOCK) {
                g_isSessionLocked = true;
                g_nfc.sendCommand("SET_LOCK_STATE:1");
                AddLog("Session Locked (Lock Screen Active)", Theme::Amber());
            } else if (wParam == WTS_SESSION_UNLOCK) {
                g_isSessionLocked = false;
                g_nfc.sendCommand("SET_LOCK_STATE:0");
                AddLog("Session Unlocked (User Desktop Active)", Theme::Emerald());
            }
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        case WM_TIMER: {
            if (wParam == 1001) {
                if (g_activeTab == TAB_SETTINGS) {
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            break;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            int oldHoverRule = g_hoveredRuleIndex;
            int oldHoverBtn = g_hoveredButtonId;

            g_hoveredRuleIndex = -1;
            g_hoveredButtonId = 0;

            if (!g_modalActive && g_activeTab == TAB_AUTOMATIONS) {
                int stageY = 128;
                if (x >= w - 200 && x <= w - 26 && y >= stageY + 12 && y <= stageY + 46) g_hoveredButtonId = 3;
                else if (x >= w - 200 && x <= w - 26 && y >= stageY + 54 && y <= stageY + 88) g_hoveredButtonId = 2;

                int heroW = w - 240;
                if (!g_lastScannedCard.uid.empty() && x >= 26 + heroW - 130 && x <= 26 + heroW - 12 && y >= stageY + 20 && y <= stageY + 48) g_hoveredButtonId = 4;

                int listY = 240 + 36;
                int listH = h - 365 - 44;
                if (x >= 26 && x <= w - 26 && y >= listY && y <= listY + listH) {
                    int itemY = 240 + 40 - g_scrollOffset;
                    for (size_t i = 0; i < g_config.rules.size(); i++) {
                        if (y >= itemY && y <= itemY + 48) {
                            g_hoveredRuleIndex = (int)i;
                            break;
                        }
                        itemY += 54;
                    }
                }
            }

            if (g_hoveredButtonId != oldHoverBtn || g_hoveredRuleIndex != oldHoverRule) {
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            if (g_activeTab == TAB_AUTOMATIONS && !g_modalActive) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                g_scrollOffset -= (delta / WHEEL_DELTA) * 28;
                if (g_scrollOffset < 0) g_scrollOffset = 0;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            // Handle Modal Clicks
            if (g_modalActive) {
                int mw = 620;
                int mh = 690;
                int mx = (w - mw) / 2;
                int my = (h - mh) / 2 - 10;

                // Click outside modal card -> close modal
                if (x < mx || x > mx + mw || y < my || y > my + mh) {
                    CloseModalEditor();
                    return 0;
                }

                // Match Mode Pills
                if (y >= my + 144 && y <= my + 172) {
                    if (x >= mx + 28 && x <= mx + 148) g_modalMatchIndex = 0;
                    else if (x >= mx + 154 && x <= mx + 304) g_modalMatchIndex = 1;
                    else if (x >= mx + 310 && x <= mx + 470) g_modalMatchIndex = 2;
                    SoundPlayer::playTap();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Paste Scanned Tag UID Button (mx+mw-152, my+198, 124, 34)
                if (x >= mx + mw - 152 && x <= mx + mw - 28 && y >= my + 198 && y <= my + 232) {
                    if (!g_lastScannedCard.uid.empty()) {
                        SetWindowTextW(g_hEditUid, s2ws(g_lastScannedCard.uid).c_str());
                        if (!g_lastScannedCard.payload.empty()) {
                            SetWindowTextW(g_hEditPayload, s2ws(g_lastScannedCard.payload).c_str());
                            SetWindowTextW(g_hEditTarget, s2ws(g_lastScannedCard.payload).c_str());
                        }
                        SoundPlayer::playTap();
                        AddLog("Pasted Scanned Tag: " + g_lastScannedCard.uid, Theme::Cyan());
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Action Type Grid (2x4 Cards at my+320, h=30)
                int cardW = (mw - 56 - 18) / 4;
                for (int i = 0; i < 8; i++) {
                    int row = i / 4;
                    int col = i % 4;
                    int cx = mx + 28 + col * (cardW + 6);
                    int cy = my + 320 + row * 36;
                    if (x >= cx && x <= cx + cardW && y >= cy && y <= cy + 30) {
                        g_modalTypeIndex = i;
                        if (g_modalTypeIndex == 2) {
                            wchar_t curTarget[512] = {0};
                            GetWindowTextW(g_hEditTarget, curTarget, 511);
                            if (wcslen(curTarget) == 0) {
                                SetWindowTextW(g_hEditTarget, L"resume");
                            }
                            wchar_t curName[256] = {0};
                            GetWindowTextW(g_hEditName, curName, 255);
                            if (wcslen(curName) == 0 || wcscmp(curName, L"New Automation") == 0 || wcscmp(curName, L"Custom NFC Action") == 0) {
                                SetWindowTextW(g_hEditName, L"Resume Spotify");
                            }
                        }
                        SoundPlayer::playTap();
                        RepositionModalControls(w, h);
                        InvalidateRect(hWnd, NULL, TRUE);
                        return 0;
                    }
                }

                // Spotify Helper Buttons: [Paste Link] (mx+mw-212 to mx+mw-128), [Resume Last] (mx+mw-120 to mx+mw-28)
                if (g_modalTypeIndex == 2 && y >= my + 414 && y <= my + 448) {
                    if (x >= mx + mw - 212 && x <= mx + mw - 128) {
                        std::wstring clip = GetClipboardTextString();
                        if (!clip.empty()) {
                            SetWindowTextW(g_hEditTarget, clip.c_str());
                            std::string lowerClip = ws2s(clip);
                            std::transform(lowerClip.begin(), lowerClip.end(), lowerClip.begin(), ::tolower);
                            if (lowerClip.find("playlist") != std::string::npos) SetWindowTextW(g_hEditName, L"Play Spotify Playlist");
                            else if (lowerClip.find("album") != std::string::npos) SetWindowTextW(g_hEditName, L"Play Spotify Album");
                            else if (lowerClip.find("artist") != std::string::npos) SetWindowTextW(g_hEditName, L"Play Spotify Artist");
                            else SetWindowTextW(g_hEditName, L"Play Spotify Song");
                        }
                        SoundPlayer::playTap();
                        InvalidateRect(hWnd, NULL, TRUE);
                        return 0;
                    }
                    if (x >= mx + mw - 120 && x <= mx + mw - 28) {
                        SetWindowTextW(g_hEditTarget, L"resume");
                        SetWindowTextW(g_hEditName, L"Resume Spotify");
                        SoundPlayer::playTap();
                        InvalidateRect(hWnd, NULL, TRUE);
                        return 0;
                    }
                }

                // File Browse Button for App (0) and Script (4)
                bool showBrowse = (g_modalTypeIndex == 0 || g_modalTypeIndex == 4);
                if (showBrowse && x >= mx + mw - 122 && x <= mx + mw - 28 && y >= my + 414 && y <= my + 448) {
                    wchar_t szFile[MAX_PATH] = {0};
                    OPENFILENAMEW ofn;
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
                    ofn.lpstrFilter = L"Executable / Shortcut (*.exe;*.lnk;*.bat;*.ps1)\0*.exe;*.lnk;*.bat;*.ps1\0All Files (*.*)\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        SetWindowTextW(g_hEditTarget, szFile);
                        wchar_t* pName = PathFindFileNameW(szFile);
                        if (pName) {
                            wchar_t baseName[MAX_PATH];
                            wcscpy(baseName, pName);
                            PathRemoveExtensionW(baseName);
                            wchar_t curName[256];
                            GetWindowTextW(g_hEditName, curName, 255);
                            if (wcslen(curName) == 0) SetWindowTextW(g_hEditName, baseName);
                        }
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Preview Sound Button (mx+400, my+532, 98, 28)
                if (x >= mx + 400 && x <= mx + 498 && y >= my + 532 && y <= my + 560) {
                    int curSel = (int)SendMessageW(g_hComboSound, CB_GETCURSEL, 0, 0);
                    wchar_t soundNameBuf[256] = {0};
                    if (curSel != CB_ERR) {
                        SendMessageW(g_hComboSound, CB_GETLBTEXT, curSel, (LPARAM)soundNameBuf);
                    }
                    std::string snd = ws2s(soundNameBuf);
                    AddLog("Auditioning: " + snd, Theme::Emerald());
                    SoundPlayer::playSound(snd, GetExeDir());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Sounds Folder Button (mx+504, my+532, 88, 28)
                if (x >= mx + 504 && x <= mx + 592 && y >= my + 532 && y <= my + 560) {
                    SoundPlayer::playTap();
                    std::string sndDir = GetExeDir() + "\\sounds";
                    ShellExecuteA(NULL, "open", sndDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    return 0;
                }

                // Silent Toggle
                if (x >= mx + 32 && x <= mx + 70 && y >= my + 580 && y <= my + 602) {
                    g_modalSilent = !g_modalSilent;
                    SoundPlayer::playTap();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Enabled Toggle
                if (x >= mx + 280 && x <= mx + 318 && y >= my + 580 && y <= my + 602) {
                    g_modalEnabled = !g_modalEnabled;
                    SoundPlayer::playTap();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Save Automation Button (mx+mw-264, my+630, 154, 38)
                if (x >= mx + mw - 264 && x <= mx + mw - 110 && y >= my + 630 && y <= my + 668) {
                    SoundPlayer::playTap();
                    SaveModalEditor();
                    return 0;
                }

                // Cancel Button (mx+mw-98, my+630, 70, 38)
                if (x >= mx + mw - 98 && x <= mx + mw - 28 && y >= my + 630 && y <= my + 668) {
                    SoundPlayer::playTap();
                    CloseModalEditor();
                    return 0;
                }

                return 0;
            }

            // Audio Mute Toggle Button
            if (x >= w - 116 && x <= w - 20 && y >= 28 && y <= 56) {
                g_config.soundEnabled = !g_config.soundEnabled;
                ConfigManager::save(g_configPath, g_config);
                if (g_config.soundEnabled) SoundPlayer::playTap();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Navigation Tabs Click
            if (y >= 86 && y <= 122) {
                int tabW = (w - 32) / 3;
                if (x >= 16 && x < 16 + tabW) g_activeTab = TAB_AUTOMATIONS;
                else if (x >= 16 + tabW && x < 16 + tabW * 2) g_activeTab = TAB_PROGRAMMER;
                else if (x >= 16 + tabW * 2 && x <= w - 16) g_activeTab = TAB_SETTINGS;
                
                SoundPlayer::playTap();
                UpdateControlsVisibility();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // --- TAB AUTOMATIONS CLICKS ---
            if (g_activeTab == TAB_AUTOMATIONS) {
                int stageY = 128;
                int heroW = w - 240;

                // Inline + Map Action Button inside Hero Card
                if (!g_lastScannedCard.uid.empty() && x >= 26 + heroW - 130 && x <= 26 + heroW - 12 && y >= stageY + 20 && y <= stageY + 48) {
                    SoundPlayer::playTap();
                    OpenModalEditor(g_lastScannedCard.uid, g_lastScannedCard.payload);
                    return 0;
                }

                // + New Automation Button
                if (x >= w - 200 && x <= w - 26 && y >= stageY + 12 && y <= stageY + 46) {
                    SoundPlayer::playTap();
                    OpenModalEditor();
                    return 0;
                }

                // Tap-to-Rule / Record Mode Button
                if (x >= w - 200 && x <= w - 26 && y >= stageY + 54 && y <= stageY + 88) {
                    g_recordMode = !g_recordMode;
                    SoundPlayer::playTap();
                    AddLog(g_recordMode ? "Tap to Record Active: Tap any card to auto-configure rule..." : "Tap to Record Cancelled", Theme::Amber());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Master Automations Global Switch Toggle Click (Header: y >= 240 && y <= 274)
                if (x >= w - 240 && x <= w - 100 && y >= 240 && y <= 274) {
                    g_config.masterAutomationsEnabled = !g_config.masterAutomationsEnabled;
                    ConfigManager::save(g_configPath, g_config);
                    if (g_config.soundEnabled) SoundPlayer::playTap();
                    AddLog(g_config.masterAutomationsEnabled ? "Master Automations Enabled: All triggers active" : "Master Automations Disabled: Rules bypassed, direct URL launch active", g_config.masterAutomationsEnabled ? Theme::Emerald() : Theme::Amber());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Automations List Row Clicks
                int listY = 240 + 36;
                int listH = h - 365 - 44;
                if (x >= 26 && x <= w - 26 && y >= listY && y <= listY + listH) {
                    int itemY = 240 + 40 - g_scrollOffset;
                    for (size_t i = 0; i < g_config.rules.size(); i++) {
                        if (y >= itemY && y <= itemY + 48) {
                            ActionRule& r = g_config.rules[i];

                            // Toggle Switch (ON/OFF)
                            if (x >= w - 210 && x <= w - 170) {
                                r.enabled = !r.enabled;
                                ConfigManager::save(g_configPath, g_config);
                                if (g_config.soundEnabled) SoundPlayer::playTap();
                                AddLog(r.name + (r.enabled ? " Enabled (ON)" : " Disabled (OFF)"), Theme::Cyan());
                                InvalidateRect(hWnd, NULL, FALSE);
                                return 0;
                            }

                            // Run Button
                            if (x >= w - 160 && x <= w - 122) {
                                AddLog("Executing: " + r.name + "...", Theme::Cyan());
                                ExecuteActionAsync(r, g_isSessionLocked);
                                return 0;
                            }

                            // Edit Button
                            if (x >= w - 114 && x <= w - 74) {
                                SoundPlayer::playTap();
                                OpenModalEditor(r.tagUid, r.tagPayload, (int)i);
                                return 0;
                            }

                            // Delete Button
                            if (x >= w - 66 && x <= w - 38) {
                                std::string deletedName = r.name;
                                g_config.rules.erase(g_config.rules.begin() + i);
                                ConfigManager::save(g_configPath, g_config);
                                if (g_config.soundEnabled) SoundPlayer::playTap();
                                AddLog("Deleted Action: " + deletedName, Theme::Rose());
                                InvalidateRect(hWnd, NULL, FALSE);
                                return 0;
                            }
                        }
                        itemY += 54;
                    }
                }
            } 
            // --- TAB PROGRAMMER CLICKS ---
            else if (g_activeTab == TAB_PROGRAMMER) {
                int progY = 128;

                // Format Pills Clicks
                if (y >= progY + 98 && y <= progY + 132) {
                    if (x >= 32 && x <= 222) {
                        g_writeFormatIndex = 0;
                        g_writeType = "url";
                        SoundPlayer::playTap();
                        UpdateControlsVisibility();
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    } else if (x >= 232 && x <= 392) {
                        g_writeFormatIndex = 1;
                        g_writeType = "text";
                        SoundPlayer::playTap();
                        UpdateControlsVisibility();
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    } else if (x >= 402 && x <= 562) {
                        g_writeFormatIndex = 2;
                        g_writeType = "wipe";
                        SoundPlayer::playTap();
                        UpdateControlsVisibility();
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                }

                // Arm Reader & Program Tag Button (Asynchronous, Zero UI Freeze)
                int btnY = progY + 226;
                if (x >= 32 && x <= 272 && y >= btnY && y <= btnY + 38) {
                    wchar_t buf[1024];
                    GetWindowTextW(g_hWritePayload, buf, 1023);
                    std::string payload = ws2s(buf);

                    // Clean payload
                    while (!payload.empty() && (unsigned char)payload.front() <= 32) payload.erase(0, 1);
                    while (!payload.empty() && (unsigned char)payload.back() <= 32) payload.pop_back();

                    if (g_writeFormatIndex == 0) {
                        if (payload.rfind("http://", 0) != 0 && payload.rfind("https://", 0) != 0) {
                            payload = "https://" + payload;
                        }
                        g_writeType = "url";
                    } else if (g_writeFormatIndex == 1) {
                        g_writeType = "text";
                    } else {
                        g_writeType = "wipe";
                        payload = "";
                    }

                    g_writerArmed = true;
                    g_nfc.armWriter(g_writeType, payload);
                    SoundPlayer::playTap();
                    g_writerStatusText = "ARMED! Place NFC Card/Sticker on reader now to write...";
                    AddLog("Writer Armed: " + g_writeType + (payload.empty() ? "" : (" (" + payload + ")")), Theme::Amber());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Cancel / Disarm Button
                if (x >= 284 && x <= 434 && y >= btnY && y <= btnY + 38) {
                    g_writerArmed = false;
                    g_nfc.disarmWriter();
                    SoundPlayer::playTap();
                    g_writerStatusText = "Writer Disarmed. Ready.";
                    AddLog("Writer Disarmed", Theme::TextMuted());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
            // --- TAB SETTINGS CLICKS ---
            else if (g_activeTab == TAB_SETTINGS) {
                int setY = 128;
                int portY = setY + 48;

                // Port Selection Buttons: COM8, COM1, COM5, COM6, Auto
                std::vector<std::string> ports = {"COM8", "COM1", "COM5", "COM6", "Auto"};
                int px = w - 340;
                for (const auto& p : ports) {
                    if (x >= px && x <= px + 50 && y >= portY + 12 && y <= portY + 40) {
                        std::string targetPort = (p == "Auto") ? "auto" : p;
                        g_config.comPort = targetPort;
                        ConfigManager::save(g_configPath, g_config);
                        g_nfc.switchPort(targetPort);
                        SoundPlayer::playTap();
                        AddLog("Switched Active Port to: " + targetPort, Theme::Cyan());
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    px += 54;
                }

                // Diagnostics: Ping & Reset BLE (diagY = setY + 108)
                int diagY = setY + 108;
                if (x >= w - 215 && x <= w - 155 && y >= diagY + 12 && y <= diagY + 40) {
                    g_nfc.sendCommand("STATUS");
                    SoundPlayer::playTap();
                    AddLog("Sent STATUS diagnostic probe to reader...", Theme::Emerald());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                if (x >= w - 145 && x <= w - 50 && y >= diagY + 12 && y <= diagY + 40) {
                    g_nfc.sendCommand("BLE_RESET");
                    SoundPlayer::playTap();
                    AddLog("Sent BLE_RESET: Disconnected clients & cleared pairing cache", Theme::Amber());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Row 3: Wake-on-LAN (WoL) PC Auto Power-On (wolY = setY + 168)
                int wolY = setY + 168;
                // Test WoL (w - 265 to w - 193)
                if (x >= w - 265 && x <= w - 193 && y >= wolY + 12 && y <= wolY + 40) {
                    g_nfc.sendCommand("TEST_WOL");
                    SoundPlayer::playTap();
                    AddLog("Sent TEST_WOL: Reader broadcasting Magic Packet to " + g_config.wolMac + "...", Theme::CyanLight());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                // Sync MAC (w - 185 to w - 115)
                if (x >= w - 185 && x <= w - 115 && y >= wolY + 12 && y <= wolY + 40) {
                    std::string cmd = "SET_WOL:" + std::string(g_config.wolEnabled ? "1" : "0") + ":" + g_config.wolMac;
                    g_nfc.sendCommand(cmd);
                    SoundPlayer::playTap();
                    AddLog("Synced WoL MAC & State to ESP32 Flash: " + g_config.wolMac, Theme::Emerald());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                // Toggle WoL (w - 105 to w - 46)
                if (x >= w - 105 && x <= w - 46 && y >= wolY + 12 && y <= wolY + 40) {
                    g_config.wolEnabled = !g_config.wolEnabled;
                    ConfigManager::save(g_configPath, g_config);
                    std::string cmd = "SET_WOL:" + std::string(g_config.wolEnabled ? "1" : "0") + ":" + g_config.wolMac;
                    g_nfc.sendCommand(cmd);
                    SoundPlayer::playTap();
                    AddLog(g_config.wolEnabled ? "Wake-on-LAN (WoL) Auto Power-On Enabled" : "Wake-on-LAN (WoL) Disabled", Theme::Cyan());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Row 4: Windows Background Service Install / Remove (svcY = setY + 228)
                int svcY = setY + 228;
                if (x >= w - 110 && x <= w - 46 && y >= svcY + 12 && y <= svcY + 40) {
                    int svcStatus = GetBackgroundServiceStatus();
                    std::string dir = GetExeDir();
                    if (svcStatus == 0) {
                        std::string svcExe = dir + "\\NFC_Service.exe";
                        ShellExecuteA(NULL, "runas", svcExe.c_str(), "-install", dir.c_str(), SW_SHOWNORMAL);
                        AddLog("Requesting Service Installation (UAC)...", Theme::Cyan());
                    } else {
                        // Clean removal: Stop service, terminate daemon instances, delete service
                        ShellExecuteA(NULL, "runas", "cmd.exe", "/c net stop NFCCommanderService & taskkill /F /IM NFC_Service.exe /T & taskkill /F /IM NFC_Service_old.exe /T & sc delete NFCCommanderService", dir.c_str(), SW_HIDE);
                        AddLog("Requesting Service Removal & Port Release (UAC)...", Theme::Cyan());
                    }
                    SoundPlayer::playTap();
                    Sleep(1200);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Row 5: Auto-start Studio on Windows Logon (setY + 288)
                if (x >= w - 110 && x <= w - 46 && y >= setY + 300 && y <= setY + 328) {
                    bool cur = IsAutoStartOnLogon();
                    SetAutoStartOnLogon(!cur);
                    SoundPlayer::playTap();
                    AddLog(!cur ? "Auto-start on Windows Logon Enabled" : "Auto-start on Windows Logon Disabled", Theme::Cyan());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Row 7: Sounds Folder Button (w - 215, setY + 408 + 12, 95, 28)
                if (x >= w - 215 && x <= w - 120 && y >= setY + 420 && y <= setY + 448) {
                    SoundPlayer::playTap();
                    std::string sndDir = GetExeDir() + "\\sounds";
                    ShellExecuteA(NULL, "open", sndDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    return 0;
                }

                // Settings Toggles
                if (x >= w - 110 && x <= w - 46) {
                    // Row 6: Desktop Tray Notifications (setY + 348)
                    if (y >= setY + 360 && y <= setY + 388) {
                        g_config.notificationsEnabled = !g_config.notificationsEnabled;
                        ConfigManager::save(g_configPath, g_config);
                        SoundPlayer::playTap();
                        AddLog(g_config.notificationsEnabled ? "Desktop Notifications Enabled" : "Desktop Notifications Disabled", Theme::Cyan());
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    // Row 7: Synthesized Audio Chimes (setY + 408)
                    if (y >= setY + 420 && y <= setY + 448) {
                        g_config.soundEnabled = !g_config.soundEnabled;
                        ConfigManager::save(g_configPath, g_config);
                        SoundPlayer::playTap();
                        AddLog(g_config.soundEnabled ? "Audio Chimes Enabled" : "Audio Chimes Muted", Theme::Cyan());
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    // Row 8: Auto-Open URLs (setY + 468)
                    if (y >= setY + 480 && y <= setY + 508) {
                        g_config.autoOpenUrls = !g_config.autoOpenUrls;
                        ConfigManager::save(g_configPath, g_config);
                        SoundPlayer::playTap();
                        AddLog(g_config.autoOpenUrls ? "Auto-Open URLs Enabled" : "Auto-Open URLs Disabled", Theme::Cyan());
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    // Row 9: Minimize to System Tray (setY + 528)
                    if (y >= setY + 540 && y <= setY + 568) {
                        g_config.minimizeToTray = !g_config.minimizeToTray;
                        ConfigManager::save(g_configPath, g_config);
                        SoundPlayer::playTap();
                        AddLog(g_config.minimizeToTray ? "Minimize to Tray Enabled" : "Minimize to Tray Disabled", Theme::Cyan());
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                }

                // Row 10: ESP32 Web Portal (row10Y = setY + 588)
                int row10Y = setY + 588;
                // Start/Stop Portal Button
                if (x >= w - 215 && x <= w - 120 && y >= row10Y + 12 && y <= row10Y + 40) {
                    if (!g_portalActive) {
                        g_nfc.sendCommand("WIFI_START");
                        g_portalActive = true;
                        SoundPlayer::playTap();
                        AddLog("Sent WIFI_START: Starting Web Portal at http://" + g_config.wifiIp + "...", Theme::Emerald());
                    } else {
                        g_nfc.sendCommand("WIFI_STOP");
                        g_portalActive = false;
                        SoundPlayer::playTap();
                        AddLog("Sent WIFI_STOP: Powered OFF Wi-Fi radio (BLE dedicated)", Theme::Amber());
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                // Open Web Portal in Browser Button
                if (x >= w - 110 && x <= w - 46 && y >= row10Y + 12 && y <= row10Y + 40) {
                    SoundPlayer::playTap();
                    std::string portalUrl = "http://" + g_config.wifiIp + "/";
                    ShellExecuteA(NULL, "open", portalUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    AddLog("Opening " + portalUrl + " in default browser", Theme::Cyan());
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            if (wmId == IDC_MODAL_SOUND && wmEvent == CBN_SELCHANGE) {
                int curSel = (int)SendMessageW(g_hComboSound, CB_GETCURSEL, 0, 0);
                if (curSel != CB_ERR) {
                    wchar_t soundNameBuf[256] = {0};
                    SendMessageW(g_hComboSound, CB_GETLBTEXT, curSel, (LPARAM)soundNameBuf);
                    g_modalRule.sound = ws2s(soundNameBuf);
                    AddLog("Selected Sound: " + g_modalRule.sound, Theme::Cyan());
                    SoundPlayer::playSound(g_modalRule.sound, GetExeDir());
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            if (wmId == ID_TRAY_SHOW) {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
                return 0;
            }
            if (wmId == ID_TRAY_RECORD) {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
                g_recordMode = true;
                AddLog("Tap to Record Activated from Tray", Theme::Amber());
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (wmId == ID_TRAY_EXIT) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }

        case WM_TRAY_ICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                    ShowWindow(hWnd, SW_RESTORE);
                    ShowWindow(hWnd, SW_SHOW);
                    SetForegroundWindow(hWnd);
                } else {
                    POINT pt;
                    GetCursorPos(&pt);
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Open NFC Command Deck");
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RECORD, L"Tap to Record Next Tag...");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
                    SetForegroundWindow(hWnd);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                }
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            RenderUI(hdc, rc.right - rc.left, rc.bottom - rc.top);
            EndPaint(hWnd, &ps);
            break;
        }

        case WM_ERASEBKGND:
            return 1; // Prevent flicker

        case WM_CLOSE: {
            if (g_config.minimizeToTray) {
                ShowWindow(hWnd, SW_HIDE);
                ShowTrayNotification("NFC Command Deck", "Running in background system tray.");
            } else {
                DestroyWindow(hWnd);
            }
            break;
        }

        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hWts) {
                PWTSUnRegisterSessionNotification pfn = (PWTSUnRegisterSessionNotification)GetProcAddress(g_hWts, "WTSUnRegisterSessionNotification");
                if (pfn) pfn(hWnd);
                FreeLibrary(g_hWts);
            }
            g_nfc.stop();
            if (g_hDarkBrush) DeleteObject(g_hDarkBrush);
            if (g_hInputBrush) DeleteObject(g_hInputBrush);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hFontBold) DeleteObject(g_hFontBold);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

LONG WINAPI DebugCrashHandler(EXCEPTION_POINTERS* ep) {
    FILE* f = fopen("debug_crash.txt", "a");
    if (f) {
        fprintf(f, "CRASH! Exception code: 0x%08lX at address %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(DebugCrashHandler);
    g_hInstance = hInstance;

    // 1. Force working directory to the application executable directory so configs/icons always load
    std::string exeDir = GetExeDir();
    SetCurrentDirectoryA(exeDir.c_str());
    g_configPath = exeDir + "\\nfc_actions.json";

    // 2. Strict Single-Instance Enforcement
    // Mutex handover: Signals background NFC_Service that interactive GUI is active so service releases COM8
    SetLastError(0);
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), &sd, FALSE };
    HANDLE hGuiMutex = CreateMutexA(&sa, TRUE, "Global\\NFC_Command_Deck_GUI_Active");
    DWORD mutexErr = GetLastError();
    if (!hGuiMutex && mutexErr == ERROR_ACCESS_DENIED) {
        SetLastError(0);
        hGuiMutex = CreateMutexA(&sa, TRUE, "NFC_Command_Deck_GUI_Active");
        mutexErr = GetLastError();
    }

    if (mutexErr == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"NFCCommandDeckClass", NULL);
        if (hExisting) {
            ShowWindow(hExisting, SW_RESTORE);
            ShowWindow(hExisting, SW_SHOW);
            SetForegroundWindow(hExisting);
            if (hGuiMutex) CloseHandle(hGuiMutex);
            return 0;
        }
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    std::string iconPath = exeDir + "\\app.ico";
    g_hAppIcon = (HICON)LoadImageA(hInstance, iconPath.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!g_hAppIcon) {
        g_hAppIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    }
    if (!g_hAppIcon) {
        g_hAppIcon = Theme::CreateAppLogoIcon();
    }

    WNDCLASSEXW wcex;
    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = L"NFCCommandDeckClass";
    wcex.hIcon = g_hAppIcon;
    wcex.hIconSm = g_hAppIcon;
    if (!RegisterClassExW(&wcex)) {
        if (hGuiMutex) { ReleaseMutex(hGuiMutex); CloseHandle(hGuiMutex); }
        return FALSE;
    }

    // High-end Studio Pro Dimensions (760 x 920) - Generous height for all telemetry, WoL & service cards
    int winW = 760;
    int winH = 920;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    bool startMinimized = (lpCmdLine && strstr(lpCmdLine, "--minimized") != NULL);
    DWORD winStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    if (!startMinimized) winStyle |= WS_VISIBLE;

    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"NFCCommandDeckClass",
        L"NFC Command Deck Pro",
        winStyle,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        if (hGuiMutex) { ReleaseMutex(hGuiMutex); CloseHandle(hGuiMutex); }
        return FALSE;
    }

    g_hWts = LoadLibraryA("wtsapi32.dll");
    if (g_hWts) {
        PWTSRegisterSessionNotification pfn = (PWTSRegisterSessionNotification)GetProcAddress(g_hWts, "WTSRegisterSessionNotification");
        if (pfn) pfn(hWnd, NOTIFY_FOR_THIS_SESSION);
    }

    if (g_hAppIcon) {
        SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)g_hAppIcon);
        SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_hAppIcon);
    }

    if (!startMinimized) {
        ShowWindow(hWnd, SW_SHOWNORMAL);
        UpdateWindow(hWnd);
        SetForegroundWindow(hWnd);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hGuiMutex) {
        ReleaseMutex(hGuiMutex);
        CloseHandle(hGuiMutex);
    }

    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
