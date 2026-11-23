#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include <tlhelp32.h>
#include <string.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>

#define WTS_SESSION_LOCK        0x7
#define WTS_SESSION_UNLOCK      0x8
#define WTS_SESSION_LOGON       0x5
#define WTS_SESSION_LOGOFF      0x6

typedef struct tagWTSSESSION_NOTIFICATION {
    DWORD cbSize;
    DWORD dwSessionId;
} WTSSESSION_NOTIFICATION, *PWTSSESSION_NOTIFICATION;

#define SERVICE_NAME        "NFCCommanderService"
#define SERVICE_DISPLAY     "NFC Commander Background Service"
#define SERVICE_DESC        "Zero-overhead background hardware daemon for NFC Command Deck. Manages reader serial link, pre-logon unlock, and workstation lock states."
#define GUI_ACTIVE_MUTEX    "Global\\NFC_Command_Deck_GUI_Active"

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = (SERVICE_STATUS_HANDLE)0;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

bool g_running = true;
HANDLE g_hSerial = INVALID_HANDLE_VALUE;
std::string g_comPort = "COM8";
int g_baudRate = 115200;
bool g_isSessionLocked = true; // Assume locked on cold boot
std::string g_autoUnlockUid = "00:00:00:00";
std::string g_autoUnlockPin = "1234";

// Utility: get directory where the exe is located
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

// Read configured COM port from nfc_actions.json if available
void LoadConfigPort() {
    std::string configPath = GetExeDir() + "\\nfc_actions.json";
    std::ifstream f(configPath.c_str());
    if (f.is_open()) {
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        size_t p = content.find("\"comPort\"");
        if (p != std::string::npos) {
            size_t colon = content.find(':', p);
            if (colon != std::string::npos) {
                size_t q1 = content.find('"', colon);
                if (q1 != std::string::npos) {
                    size_t q2 = content.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        std::string port = content.substr(q1 + 1, q2 - q1 - 1);
                        if (!port.empty() && port != "auto") {
                            g_comPort = port;
                        }
                    }
                }
            }
        }
        // Dynamic PIN & Card UID discovery from rules
        size_t rPos = 0;
        while ((rPos = content.find("\"target\"", rPos)) != std::string::npos) {
            size_t colon = content.find(':', rPos);
            if (colon != std::string::npos) {
                size_t q1 = content.find('"', colon);
                if (q1 != std::string::npos) {
                    size_t q2 = content.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        std::string targetVal = content.substr(q1 + 1, q2 - q1 - 1);
                        bool isDigits = !targetVal.empty();
                        for (char c : targetVal) if (!isdigit((unsigned char)c)) { isDigits = false; break; }
                        if (isDigits) {
                            g_autoUnlockPin = targetVal;
                            size_t ruleStart = content.rfind('{', rPos);
                            size_t uidKey = content.find("\"tagUid\"", ruleStart);
                            if (uidKey != std::string::npos && uidKey < rPos) {
                                size_t uc = content.find(':', uidKey);
                                size_t uq1 = content.find('"', uc);
                                size_t uq2 = (uq1 != std::string::npos) ? content.find('"', uq1 + 1) : std::string::npos;
                                if (uq1 != std::string::npos && uq2 != std::string::npos) {
                                    g_autoUnlockUid = content.substr(uq1 + 1, uq2 - uq1 - 1);
                                }
                            }
                            break;
                        }
                    }
                }
            }
            rPos += 8;
        }
    }
}

// Send command to ESP32 over serial
bool SendSerialCommand(const std::string& cmd) {
    if (g_hSerial == INVALID_HANDLE_VALUE) return false;
    std::string fullCmd = cmd + "\n";
    DWORD written = 0;
    return WriteFile(g_hSerial, fullCmd.c_str(), (DWORD)fullCmd.length(), &written, NULL) && (written == fullCmd.length());
}

// Check if interactive GUI is currently running (via mutex or process snapshot)
bool IsGuiActive() {
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, GUI_ACTIVE_MUTEX);
    if (hMutex != NULL) {
        CloseHandle(hMutex);
        return true;
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                if (lstrcmpiA(pe.szExeFile, "NFC_Command_Deck.exe") == 0) {
                    CloseHandle(hSnap);
                    return true;
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return false;
}

// Open COM port with CDC DTR/RTS assertion
bool OpenSerialPort() {
    if (g_hSerial != INVALID_HANDLE_VALUE) return true;

    std::string portName = "\\\\.\\" + g_comPort;
    g_hSerial = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (g_hSerial == INVALID_HANDLE_VALUE) return false;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(g_hSerial, &dcb)) {
        dcb.BaudRate = g_baudRate;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity   = NOPARITY;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        SetCommState(g_hSerial, &dcb);
    }

    EscapeCommFunction(g_hSerial, SETDTR);
    EscapeCommFunction(g_hSerial, SETRTS);

    // Non-blocking timeouts: ReadFile returns immediately so loop never hangs
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(g_hSerial, &timeouts);

    // Sync initial lock state to hardware
    SendSerialCommand(std::string("SET_LOCK_STATE:") + (g_isSessionLocked ? "1" : "0"));
    return true;
}

void CloseSerialPort() {
    if (g_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
    }
}

// Background Worker Loop
void RunDaemonWorker() {
    LoadConfigPort();

    std::string lineBuf = "";
    char buffer[512];
    DWORD bytesRead = 0;

    while (g_running) {
        // 1. If interactive GUI is active, yield COM port immediately
        if (IsGuiActive()) {
            if (g_hSerial != INVALID_HANDLE_VALUE) {
                CloseSerialPort();
            }
            Sleep(500);
            continue;
        }

        // 2. Ensure Serial is open when GUI is idle/closed
        if (g_hSerial == INVALID_HANDLE_VALUE) {
            LoadConfigPort();
            if (!OpenSerialPort()) {
                Sleep(1000);
                continue;
            }
        }

        // 3. Read incoming serial telemetry (non-blocking)
        if (ReadFile(g_hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                lineBuf += buffer;

                size_t nlPos;
                while ((nlPos = lineBuf.find('\n')) != std::string::npos) {
                    std::string line = lineBuf.substr(0, nlPos);
                    lineBuf = lineBuf.substr(nlPos + 1);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                    // ESP32 firmware manages hardware BLE auto-unlock natively from flash NVS.
                    // Background daemon acts purely as session monitor and COM bridge.
                }
            }
        } else {
            // Error reading serial; port may have been disconnected
            CloseSerialPort();
            Sleep(500);
        }

        Sleep(20);
    }

    CloseSerialPort();
}

// ---------------------------------------------------------------------------------
// WINDOWS SERVICE CONTROL HANDLER (Captures Lock Screen & Session Transitions)
// ---------------------------------------------------------------------------------
DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
    switch (dwControl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            g_running = false;
            SetEvent(g_ServiceStopEvent);
            break;

        case SERVICE_CONTROL_SESSIONCHANGE: {
            WTSSESSION_NOTIFICATION* pNotify = (WTSSESSION_NOTIFICATION*)lpEventData;
            if (dwEventType == WTS_SESSION_LOCK) {
                g_isSessionLocked = true;
                SendSerialCommand("SET_LOCK_STATE:1");
            } else if (dwEventType == WTS_SESSION_UNLOCK) {
                g_isSessionLocked = false;
                SendSerialCommand("SET_LOCK_STATE:0");
            } else if (dwEventType == WTS_SESSION_LOGOFF) {
                g_isSessionLocked = true;
                SendSerialCommand("SET_LOCK_STATE:1");
            }
            break;
        }

        default:
            break;
    }
    return NO_ERROR;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerExA(SERVICE_NAME, ServiceCtrlHandlerEx, NULL);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Launch daemon worker
    RunDaemonWorker();

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

// ---------------------------------------------------------------------------------
// SERVICE INSTALLER / UNINSTALLER CLI HELPERS
// ---------------------------------------------------------------------------------
bool InstallService() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string quotedPath = std::string("\"") + exePath + "\"";

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        std::cerr << "[!] Failed to open Service Control Manager (Administrator privileges required)." << std::endl;
        MessageBoxA(NULL, "Failed to open Service Control Manager.\nAdministrator privileges are required.", "NFC Commander Service", MB_ICONERROR | MB_OK);
        return false;
    }

    SC_HANDLE svc = CreateServiceA(
        scm,
        SERVICE_NAME,
        SERVICE_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, // Starts automatically at Windows boot!
        SERVICE_ERROR_NORMAL,
        quotedPath.c_str(),
        NULL, NULL, NULL, NULL, NULL
    );

    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            std::cout << "[*] Service is already registered." << std::endl;
            svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
        } else {
            std::cerr << "[!] CreateService failed with error: " << err << std::endl;
            char errBuf[256];
            snprintf(errBuf, sizeof(errBuf), "Failed to register Windows Service.\nError code: %lu", err);
            MessageBoxA(NULL, errBuf, "NFC Commander Service", MB_ICONERROR | MB_OK);
            CloseServiceHandle(scm);
            return false;
        }
    }

    // Set description
    SERVICE_DESCRIPTIONA sd;
    sd.lpDescription = (LPSTR)SERVICE_DESC;
    ChangeServiceConfig2A(svc, SERVICE_CONFIG_DESCRIPTION, &sd);

    // Start service immediately
    StartServiceA(svc, 0, NULL);
    std::cout << "[+] NFC Commander Service successfully installed and started (Boot Auto-Start Enabled)." << std::endl;
    MessageBoxA(NULL, "NFC Commander Background Service successfully registered and started!\n\nIt will now run at Windows boot to unlock the PC with 0% CPU & 1MB RAM.", "NFC Commander Service", MB_ICONINFORMATION | MB_OK);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

bool UninstallService() {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        std::cerr << "[!] Failed to open Service Control Manager (Administrator privileges required)." << std::endl;
        return false;
    }

    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_STOP | DELETE);
    if (!svc) {
        std::cout << "[*] Service not found or already uninstalled." << std::endl;
        CloseServiceHandle(scm);
        return true;
    }

    SERVICE_STATUS status;
    ControlService(svc, SERVICE_CONTROL_STOP, &status);
    Sleep(500);

    if (DeleteService(svc)) {
        std::cout << "[+] NFC Commander Service removed successfully." << std::endl;
        MessageBoxA(NULL, "NFC Commander Background Service has been stopped and removed.", "NFC Commander Service", MB_ICONINFORMATION | MB_OK);
    } else {
        std::cerr << "[!] DeleteService failed: " << GetLastError() << std::endl;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

void PrintStatus() {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        std::cout << "STATUS: UNKNOWN (Access Denied)" << std::endl;
        return;
    }
    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!svc) {
        std::cout << "STATUS: NOT_INSTALLED" << std::endl;
        CloseServiceHandle(scm);
        return;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) std::cout << "STATUS: RUNNING" << std::endl;
        else if (ssp.dwCurrentState == SERVICE_STOPPED) std::cout << "STATUS: STOPPED" << std::endl;
        else std::cout << "STATUS: PENDING" << std::endl;
    } else {
        std::cout << "STATUS: UNKNOWN" << std::endl;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "-install" || arg == "--install" || arg == "/i") {
            return InstallService() ? 0 : 1;
        } else if (arg == "-uninstall" || arg == "--uninstall" || arg == "/u") {
            return UninstallService() ? 0 : 1;
        } else if (arg == "-status" || arg == "--status") {
            PrintStatus();
            return 0;
        } else if (arg == "-debug" || arg == "--standalone" || arg == "/d") {
            std::cout << "[*] Running NFC Commander Background Daemon in Standalone Debug Mode..." << std::endl;
            RunDaemonWorker();
            return 0;
        }
    }

    // Default: Dispatch as Windows Service
    SERVICE_TABLE_ENTRYA ServiceTable[] = {
        { (LPSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcherA(ServiceTable)) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // Not launched by SCM, show help
            std::cout << "NFC Commander Background Service" << std::endl;
            std::cout << "Usage:" << std::endl;
            std::cout << "  NFC_Service.exe -install     Install and start boot background service" << std::endl;
            std::cout << "  NFC_Service.exe -uninstall   Stop and remove background service" << std::endl;
            std::cout << "  NFC_Service.exe -status      Check service running status" << std::endl;
            std::cout << "  NFC_Service.exe -debug       Run standalone daemon in console" << std::endl;
        }
    }

    return 0;
}
