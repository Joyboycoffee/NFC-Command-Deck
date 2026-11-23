#pragma once
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <algorithm>

template<typename T>
inline std::string to_str(const T& val) {
    std::ostringstream oss;
    oss << val;
    return oss.str();
}

template<typename T>
inline std::wstring to_wstr(const T& val) {
    std::wostringstream wss;
    wss << val;
    return wss.str();
}

enum ActionType {
    ACTION_APP,         // Launch .exe, .lnk, game, folder
    ACTION_STEAM,       // Launch Steam Game (e.g. steam://rungameid/730)
    ACTION_SPOTIFY,     // Spotify URI (e.g. spotify:track:..., spotify:playlist:...)
    ACTION_MEDIA,       // Media keys (Play/Pause, Next, Prev, Volume)
    ACTION_COMMAND,     // Shell / CMD / PowerShell Command
    ACTION_URL,         // Open URL in Default Browser
    ACTION_HOTKEY,      // Keystrokes, Hotkeys, Text Typing, or Smart Lock/Unlock PIN
    ACTION_SYSTEM       // Lock PC, Sleep, Monitor Off
};

struct ActionRule {
    std::string id;
    std::string name;
    std::string tagUid;         // e.g. "A2:CC:3C:D7" or "*" for any
    std::string tagPayload;     // Optional payload filter / card data
    std::string matchMode;      // "uid", "payload", "both", "any"
    ActionType type;
    std::string target;         // Path, URL, Command, Keystroke sequence, or PIN
    std::string args;           // Optional arguments
    bool silent;                // Silent execution (hide console)
    bool enabled;
    int triggerCount;
    std::string lastTriggerTime;
    std::string sound;          // Sound effect name (e.g. "09_apple_pay_chime.wav", "none", or "default")
};

#include <functional>

typedef std::function<void(const std::string&)> SerialSenderFunc;

class ActionExecutor {
public:
    static bool execute(const ActionRule& rule, std::string& outStatus, bool sessionIsLocked = false, SerialSenderFunc sendSerialCmd = nullptr, bool bleAlreadyHandled = false) {
        if (!rule.enabled) {
            outStatus = "Rule is disabled";
            return false;
        }

        switch (rule.type) {
            case ACTION_APP: {
                HINSTANCE res = ShellExecuteA(NULL, "open", rule.target.c_str(), 
                                              rule.args.empty() ? NULL : rule.args.c_str(), 
                                              NULL, SW_SHOWNORMAL);
                if ((INT_PTR)res > 32) {
                    outStatus = "Launched: " + rule.name;
                    return true;
                } else {
                    outStatus = "Failed to launch application (Error code " + to_str((INT_PTR)res) + ")";
                    return false;
                }
            }

            case ACTION_STEAM: {
                std::string steamUrl = rule.target;
                if (steamUrl.find("steam://") == std::string::npos) {
                    steamUrl = "steam://rungameid/" + steamUrl;
                }
                ShellExecuteA(NULL, "open", steamUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                outStatus = "Steam Game Launched: " + rule.name;
                return true;
            }

            case ACTION_SPOTIFY: {
                return executeSpotify(rule.target, rule.name, outStatus);
            }

            case ACTION_MEDIA: {
                std::string t = rule.target;
                std::transform(t.begin(), t.end(), t.begin(), ::tolower);
                if (t == "play_pause" || t.find("play") != std::string::npos) {
                    sendMediaKey(VK_MEDIA_PLAY_PAUSE);
                    outStatus = "Media: Play / Pause Toggled";
                } else if (t == "next" || t.find("next") != std::string::npos) {
                    sendMediaKey(VK_MEDIA_NEXT_TRACK);
                    outStatus = "Media: Next Track";
                } else if (t == "prev" || t.find("prev") != std::string::npos) {
                    sendMediaKey(VK_MEDIA_PREV_TRACK);
                    outStatus = "Media: Previous Track";
                } else if (t == "mute" || t.find("mute") != std::string::npos) {
                    sendMediaKey(VK_VOLUME_MUTE);
                    outStatus = "Media: Volume Mute Toggled";
                } else if (t == "vol_up" || t.find("up") != std::string::npos) {
                    for (int i = 0; i < 3; i++) sendMediaKey(VK_VOLUME_UP);
                    outStatus = "Media: Volume Increased (+6%)";
                } else if (t == "vol_down" || t.find("down") != std::string::npos) {
                    for (int i = 0; i < 3; i++) sendMediaKey(VK_VOLUME_DOWN);
                    outStatus = "Media: Volume Decreased (-6%)";
                }
                return true;
            }

            case ACTION_COMMAND: {
                if (rule.silent) {
                    std::string cmd = "cmd.exe /c " + rule.target;
                    STARTUPINFOA si;
                    PROCESS_INFORMATION pi;
                    ZeroMemory(&si, sizeof(si));
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_HIDE;
                    ZeroMemory(&pi, sizeof(pi));

                    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        outStatus = "Executed Silent Command: " + rule.name;
                        return true;
                    } else {
                        outStatus = "Command failed to spawn";
                        return false;
                    }
                } else {
                    ShellExecuteA(NULL, "open", "cmd.exe", ("/k " + rule.target).c_str(), NULL, SW_SHOWNORMAL);
                    outStatus = "Executed Console Command: " + rule.name;
                    return true;
                }
            }

            case ACTION_URL: {
                ShellExecuteA(NULL, "open", rule.target.c_str(), NULL, NULL, SW_SHOWNORMAL);
                outStatus = "Opened Link: " + rule.target;
                return true;
            }

            case ACTION_HOTKEY: {
                std::string target = rule.target;
                std::string lowerTarget = target;
                std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::tolower);

                // 1. Explicit Unlock (e.g. "unlock:1911" or "pin:1911")
                if (lowerTarget.find("unlock:") == 0) {
                    std::string pin = target.substr(7);
                    executeUnlock(pin, outStatus);
                    return true;
                }
                if (lowerTarget.find("pin:") == 0) {
                    std::string pin = target.substr(4);
                    executeUnlock(pin, outStatus);
                    return true;
                }

                // 2. Smart Lock / Unlock with PIN toggle
                // Triggered if starts with "smart_lock:", "toggle_lock:", or if target is purely numeric digits (e.g. "1911")
                bool isPurePin = !target.empty() && std::all_of(target.begin(), target.end(), ::isdigit);
                if (lowerTarget.find("smart_lock:") == 0 || lowerTarget.find("toggle_lock:") == 0 || isPurePin) {
                    std::string pin = target;
                    if (lowerTarget.find("smart_lock:") == 0) pin = target.substr(11);
                    else if (lowerTarget.find("toggle_lock:") == 0) pin = target.substr(12);

                    executeSmartLock(pin, outStatus, sessionIsLocked, sendSerialCmd, bleAlreadyHandled);
                    return true;
                }

                // 3. Direct Lock PC (win+l or lock)
                if (lowerTarget == "win+l" || lowerTarget == "win_l" || lowerTarget == "lock") {
                    LockWorkStation();
                    outStatus = "Locked Workstation (Win+L)";
                    return true;
                }

                // 4. Desktop (win+d)
                if (lowerTarget == "win+d" || lowerTarget == "win_d") {
                    sendKeyCombo(VK_LWIN, 'D');
                    outStatus = "Triggered: Show Desktop (Win+D)";
                    return true;
                }

                // 5. Task Manager (ctrl+shift+esc)
                if (lowerTarget == "ctrl+shift+esc" || lowerTarget == "taskmgr") {
                    ShellExecuteA(NULL, "open", "taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
                    outStatus = "Opened: Task Manager";
                    return true;
                }

                // 6. Explicit Typing (e.g. "type:Hello World" or "keys:1911{enter}")
                if (lowerTarget.find("type:") == 0) {
                    std::string text = target.substr(5);
                    sendKeyString(text);
                    outStatus = "Typed Text: " + text;
                    return true;
                }
                if (lowerTarget.find("keys:") == 0) {
                    std::string seq = target.substr(5);
                    sendKeyString(seq);
                    outStatus = "Executed Keystroke Sequence";
                    return true;
                }

                // 7. Generic Key Combinations (e.g. "ctrl+c", "ctrl+v", "alt+f4", "alt+tab")
                if (lowerTarget.find('+') != std::string::npos) {
                    executeKeyCombo(lowerTarget);
                    outStatus = "Sent Hotkey: " + target;
                    return true;
                }

                // Fallback: Type as text sequence
                sendKeyString(target);
                outStatus = "Sent Keystrokes: " + target;
                return true;
            }

            case ACTION_SYSTEM: {
                std::string t = rule.target;
                std::transform(t.begin(), t.end(), t.begin(), ::tolower);
                if (t == "lock" || t.find("lock") != std::string::npos) {
                    LockWorkStation();
                    outStatus = "PC Locked";
                } else if (t == "monitor_off" || t.find("monitor") != std::string::npos) {
                    SendMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)2);
                    outStatus = "Monitor Powered Off";
                } else if (t == "sleep" || t.find("sleep") != std::string::npos) {
                    HMODULE hPowr = LoadLibraryA("PowrProf.dll");
                    if (hPowr) {
                        typedef BOOLEAN (WINAPI *PSetSuspendState)(BOOLEAN, BOOLEAN, BOOLEAN);
                        PSetSuspendState pfn = (PSetSuspendState)GetProcAddress(hPowr, "SetSuspendState");
                        if (pfn) pfn(FALSE, TRUE, FALSE);
                        FreeLibrary(hPowr);
                    }
                    outStatus = "Entering PC Sleep Mode";
                }
                return true;
            }
        }

        outStatus = "Unknown action type";
        return false;
    }

    static bool isProcessRunning(const std::string& processName) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                if (lstrcmpiA(pe.szExeFile, processName.c_str()) == 0) {
                    CloseHandle(hSnap);
                    return true;
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        return false;
    }

    static std::string normalizeSpotifyTarget(const std::string& input) {
        std::string s = input;
        while (!s.empty() && (unsigned char)s.front() <= 32) s.erase(0, 1);
        while (!s.empty() && (unsigned char)s.back() <= 32) s.pop_back();

        if (s.empty()) return "resume";

        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "resume" || lower == "play" || lower == "last" || lower == "continue" || lower == "open") {
            return "resume";
        }

        // Handle Web URLs (e.g. https://open.spotify.com/track/4cOdK2wGLETKBW3PvgPWqT?si=...)
        if (lower.find("http://") == 0 || lower.find("https://") == 0) {
            size_t openSpot = lower.find("spotify.com/");
            if (openSpot != std::string::npos) {
                std::string path = s.substr(openSpot + 12);
                size_t q = path.find('?');
                if (q != std::string::npos) path = path.substr(0, q);
                size_t h = path.find('#');
                if (h != std::string::npos) path = path.substr(0, h);

                size_t slash = path.find('/');
                if (slash != std::string::npos) {
                    std::string type = path.substr(0, slash);
                    std::string id = path.substr(slash + 1);
                    while (!id.empty() && id.back() == '/') id.pop_back();
                    if (!type.empty() && !id.empty()) {
                        return "spotify:" + type + ":" + id + ":play";
                    }
                }
            }
            return s;
        }

        // Handle URI (e.g. spotify:track:...)
        if (lower.find("spotify:") == 0) {
            if (lower.rfind(":play") == std::string::npos) {
                return s + ":play";
            }
            return s;
        }

        // Fallback: assume track ID if pure alphanumeric string
        if (s.length() >= 15 && s.find(' ') == std::string::npos) {
            return "spotify:track:" + s + ":play";
        }

        return s;
    }

    static bool executeSpotify(const std::string& target, const std::string& ruleName, std::string& outStatus) {
        std::string norm = normalizeSpotifyTarget(target);

        if (norm == "resume") {
            // --- RESUME LAST PLAYED TRACK ---
            bool isRunning = isProcessRunning("Spotify.exe");

            if (!isRunning) {
                char appData[MAX_PATH] = {0};
                GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
                std::string spotifyExe = std::string(appData) + "\\Spotify\\Spotify.exe";

                HINSTANCE res = ShellExecuteA(NULL, "open", spotifyExe.c_str(), NULL, NULL, SW_SHOWNORMAL);
                if ((INT_PTR)res <= 32) {
                    ShellExecuteA(NULL, "open", "spotify:", NULL, NULL, SW_SHOWNORMAL);
                }
                Sleep(1500);
            } else {
                ShellExecuteA(NULL, "open", "spotify:", NULL, NULL, SW_SHOWNORMAL);
                Sleep(250);
            }

            sendMediaKey(VK_MEDIA_PLAY_PAUSE);
            outStatus = "Spotify: Resumed Last Played Track";
            return true;
        }

        // --- PLAY SPECIFIC SONG / PLAYLIST / ALBUM ---
        bool isRunning = isProcessRunning("Spotify.exe");
        if (!isRunning) {
            char appData[MAX_PATH] = {0};
            GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
            std::string spotifyExe = std::string(appData) + "\\Spotify\\Spotify.exe";
            ShellExecuteA(NULL, "open", spotifyExe.c_str(), NULL, NULL, SW_SHOWNORMAL);
            Sleep(1200);
        }

        HINSTANCE res = ShellExecuteA(NULL, "open", norm.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)res <= 32) {
            ShellExecuteA(NULL, "open", target.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }

        outStatus = "Spotify: Playing " + ruleName;
        return true;
    }

    // Checks if the current Windows workstation is locked (Lock Screen / Logon Screen active)
    static bool isWorkstationLocked() {
        HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
        if (hDesk == NULL) {
            return true; // Lock screen is active (access denied to user input desktop)
        }
        CloseDesktop(hDesk);
        return false;
    }

private:
    static void sendMediaKey(BYTE vkCode) {
        BYTE scanCode = (BYTE)MapVirtualKeyA(vkCode, MAPVK_VK_TO_VSC);
        keybd_event(vkCode, scanCode, KEYEVENTF_EXTENDEDKEY, 0);
        Sleep(25);
        keybd_event(vkCode, scanCode, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    }

    static void sendHardwareScanKey(BYTE vkCode, int holdMs = 45) {
        BYTE scanCode = (BYTE)MapVirtualKeyA(vkCode, MAPVK_VK_TO_VSC);
        DWORD flagsDown = KEYEVENTF_SCANCODE;
        DWORD flagsUp = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        if (vkCode == VK_RCONTROL || vkCode == VK_RMENU || vkCode == VK_LWIN || vkCode == VK_RWIN ||
            vkCode == VK_INSERT || vkCode == VK_DELETE || vkCode == VK_HOME || vkCode == VK_END ||
            vkCode == VK_PRIOR || vkCode == VK_NEXT || vkCode == VK_UP || vkCode == VK_DOWN ||
            vkCode == VK_LEFT || vkCode == VK_RIGHT || vkCode == VK_DIVIDE || vkCode == VK_NUMLOCK) {
            flagsDown |= KEYEVENTF_EXTENDEDKEY;
            flagsUp |= KEYEVENTF_EXTENDEDKEY;
        }

        INPUT inDown = {0};
        inDown.type = INPUT_KEYBOARD;
        inDown.ki.wVk = vkCode;
        inDown.ki.wScan = scanCode;
        inDown.ki.dwFlags = flagsDown;

        INPUT inUp = {0};
        inUp.type = INPUT_KEYBOARD;
        inUp.ki.wVk = vkCode;
        inUp.ki.wScan = scanCode;
        inUp.ki.dwFlags = flagsUp;

        UINT resDown = SendInput(1, &inDown, sizeof(INPUT));
        if (resDown == 0) {
            keybd_event(vkCode, scanCode, 0, 0);
        }
        Sleep(holdMs);
        UINT resUp = SendInput(1, &inUp, sizeof(INPUT));
        if (resUp == 0) {
            keybd_event(vkCode, scanCode, KEYEVENTF_KEYUP, 0);
        }
        Sleep(35);
    }

    static void sendHardwareKey(BYTE vkCode) {
        sendHardwareScanKey(vkCode, 40);
    }

    static void sendSingleKey(BYTE vkCode) {
        sendHardwareScanKey(vkCode, 40);
    }

    static void sendKeyCombo(BYTE modifier, BYTE key) {
        BYTE modScan = (BYTE)MapVirtualKeyA(modifier, MAPVK_VK_TO_VSC);
        BYTE keyScan = (BYTE)MapVirtualKeyA(key, MAPVK_VK_TO_VSC);

        keybd_event(modifier, modScan, 0, 0);
        keybd_event(key, keyScan, 0, 0);
        Sleep(25);
        keybd_event(key, keyScan, KEYEVENTF_KEYUP, 0);
        keybd_event(modifier, modScan, KEYEVENTF_KEYUP, 0);
    }

    static void sendUnicodeChar(wchar_t c) {
        INPUT input[2] = {0};
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wScan = c;
        input[0].ki.dwFlags = KEYEVENTF_UNICODE;

        input[1].type = INPUT_KEYBOARD;
        input[1].ki.wScan = c;
        input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(2, input, sizeof(INPUT));
    }

    static void sendKeyString(const std::string& text) {
        for (size_t i = 0; i < text.length(); i++) {
            if (text[i] == '{') {
                size_t closePos = text.find('}', i);
                if (closePos != std::string::npos) {
                    std::string key = text.substr(i + 1, closePos - i - 1);
                    std::string lkey = key;
                    std::transform(lkey.begin(), lkey.end(), lkey.begin(), ::tolower);
                    
                    if (lkey == "enter" || lkey == "return") {
                        sendHardwareScanKey(VK_RETURN, 45);
                    } else if (lkey == "tab") {
                        sendHardwareScanKey(VK_TAB, 45);
                    } else if (lkey == "esc" || lkey == "escape") {
                        sendHardwareScanKey(VK_ESCAPE, 45);
                    } else if (lkey == "space") {
                        sendHardwareScanKey(VK_SPACE, 45);
                    } else if (lkey == "backspace") {
                        sendHardwareScanKey(VK_BACK, 45);
                    } else if (lkey.find("delay:") == 0) {
                        int ms = atoi(lkey.substr(6).c_str());
                        if (ms > 0) Sleep(ms);
                    }
                    i = closePos;
                    continue;
                }
            }

            char ch = text[i];
            if (isdigit(ch)) {
                BYTE vk = (BYTE)('0' + (ch - '0'));
                sendHardwareScanKey(vk, 45);
            } else if (isalpha(ch)) {
                BYTE vk = (BYTE)toupper(ch);
                if (isupper(ch)) {
                    BYTE shiftScan = (BYTE)MapVirtualKeyA(VK_SHIFT, MAPVK_VK_TO_VSC);
                    keybd_event(VK_SHIFT, shiftScan, 0, 0);
                    Sleep(20);
                    sendHardwareScanKey(vk, 45);
                    keybd_event(VK_SHIFT, shiftScan, KEYEVENTF_KEYUP, 0);
                } else {
                    sendHardwareScanKey(vk, 45);
                }
            } else {
                sendUnicodeChar((wchar_t)ch);
            }
            Sleep(30);
        }
    }

    static void executeKeyCombo(const std::string& combo) {
        std::vector<BYTE> vkKeys;
        std::istringstream iss(combo);
        std::string token;
        while (std::getline(iss, token, '+')) {
            while (!token.empty() && token.front() == ' ') token.erase(token.begin());
            while (!token.empty() && token.back() == ' ') token.pop_back();

            if (token == "ctrl" || token == "control") vkKeys.push_back(VK_CONTROL);
            else if (token == "shift") vkKeys.push_back(VK_SHIFT);
            else if (token == "alt") vkKeys.push_back(VK_MENU);
            else if (token == "win" || token == "windows" || token == "cmd") vkKeys.push_back(VK_LWIN);
            else if (token == "enter" || token == "return") vkKeys.push_back(VK_RETURN);
            else if (token == "tab") vkKeys.push_back(VK_TAB);
            else if (token == "esc" || token == "escape") vkKeys.push_back(VK_ESCAPE);
            else if (token == "space") vkKeys.push_back(VK_SPACE);
            else if (token == "backspace") vkKeys.push_back(VK_BACK);
            else if (token.length() >= 2 && token[0] == 'f' && isdigit(token[1])) {
                int fnum = atoi(token.substr(1).c_str());
                if (fnum >= 1 && fnum <= 24) vkKeys.push_back((BYTE)(VK_F1 + fnum - 1));
            } else if (token.length() == 1) {
                char c = toupper(token[0]);
                vkKeys.push_back((BYTE)c);
            }
        }

        // Press down in order with scan codes
        for (BYTE vk : vkKeys) {
            BYTE sc = (BYTE)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
            keybd_event(vk, sc, 0, 0);
        }
        Sleep(30);
        // Release up in reverse order
        for (int i = (int)vkKeys.size() - 1; i >= 0; i--) {
            BYTE sc = (BYTE)MapVirtualKeyA(vkKeys[i], MAPVK_VK_TO_VSC);
            keybd_event(vkKeys[i], sc, KEYEVENTF_KEYUP, 0);
        }
    }

    static void executeUnlock(const std::string& pin, std::string& outStatus) {
        HDESK hPrevDesktop = GetThreadDesktop(GetCurrentThreadId());

        // Attempt to switch thread context to active input / Winlogon desktop
        HDESK hInputDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!hInputDesktop) {
            char winlogonDesk[] = "Winlogon";
            hInputDesktop = OpenDesktopA(winlogonDesk, 0, FALSE, GENERIC_ALL);
        }
        if (hInputDesktop) {
            SetThreadDesktop(hInputDesktop);
        }

        // Single Spacebar to wake display and lift lock screen curtain instantly
        sendHardwareScanKey(VK_SPACE, 30);

        // Fast 350ms delay for Windows lock screen slide-up animation
        Sleep(350);

        // Type each digit of the PIN with crisp timing (30ms hold, 25ms gap)
        for (char ch : pin) {
            if (isdigit(ch)) {
                BYTE vk = (BYTE)('0' + (ch - '0'));
                sendHardwareScanKey(vk, 30);
            } else if (isalpha(ch)) {
                BYTE vk = (BYTE)toupper(ch);
                if (isupper(ch)) {
                    BYTE shiftScan = (BYTE)MapVirtualKeyA(VK_SHIFT, MAPVK_VK_TO_VSC);
                    keybd_event(VK_SHIFT, shiftScan, 0, 0);
                    Sleep(15);
                    sendHardwareScanKey(vk, 30);
                    keybd_event(VK_SHIFT, shiftScan, KEYEVENTF_KEYUP, 0);
                } else {
                    sendHardwareScanKey(vk, 30);
                }
            } else {
                SHORT vkState = VkKeyScanA(ch);
                BYTE vk = LOBYTE(vkState);
                BYTE shiftState = HIBYTE(vkState);
                if (shiftState & 1) {
                    BYTE shiftScan = (BYTE)MapVirtualKeyA(VK_SHIFT, MAPVK_VK_TO_VSC);
                    keybd_event(VK_SHIFT, shiftScan, 0, 0);
                    Sleep(15);
                    sendHardwareScanKey(vk, 30);
                    keybd_event(VK_SHIFT, shiftScan, KEYEVENTF_KEYUP, 0);
                } else {
                    sendHardwareScanKey(vk, 30);
                }
            }
            Sleep(25);
        }

        // Restore desktop context (Windows Hello PIN fields submit automatically without Enter)
        if (hInputDesktop) {
            if (hPrevDesktop) SetThreadDesktop(hPrevDesktop);
            CloseDesktop(hInputDesktop);
        }

        outStatus = "Unlocked PC with PIN (" + pin + ")";
    }

    static void executeSmartLock(const std::string& pin, std::string& outStatus, bool forceUnlock = false, SerialSenderFunc sendSerialCmd = nullptr, bool bleAlreadyHandled = false) {
        bool locked = forceUnlock || isWorkstationLocked();
        if (locked) {
            if (bleAlreadyHandled) {
                // Hardware BLE has already typed the PIN; do NOT double-type in software!
                outStatus = "Unlocked PC with PIN (" + pin + ") via Hardware BLE";
            } else {
                // Software fallback: wake screen and inject PIN
                if (sendSerialCmd) {
                    sendSerialCmd("BLE_UNLOCK:" + pin);
                    sendSerialCmd("SET_LOCK_STATE:0");
                }
                executeUnlock(pin, outStatus);
            }
        } else {
            // PC is UNLOCKED: Immediately lock Windows workstation
            if (sendSerialCmd) {
                sendSerialCmd("BLE_LOCK");
                sendSerialCmd("SET_LOCK_STATE:1");
            }
            LockWorkStation();
            outStatus = "Locked PC (Win+L)";
        }
    }
};
