#pragma once
#include <windows.h>
#include <wininet.h>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include "action_executor.h"

struct TagEvent {
    bool cardPresent = false;
    std::string uid = "";
    std::string type = "";
    std::string payload = "";
    uint32_t scanId = 0;
    bool bleHandled = false;
};

typedef std::function<void(const TagEvent&)> TagCallback;
typedef std::function<void(bool connected, const std::string& info)> StatusCallback;

class NFCEngine {
public:
    NFCEngine() 
        : m_hSerial(INVALID_HANDLE_VALUE), m_hThread(NULL), m_running(false),
          m_connected(false), m_activeEndpoint("None"), m_baudRate(115200),
          m_lastScanId(0), m_cardPresent(false), m_lastUid(""), m_lastTriggerTime(0),
          m_lastBleHandledTick(0) {}

    ~NFCEngine() {
        stop();
    }

    void setCallbacks(TagCallback onTag, StatusCallback onStatus) {
        m_onTag = onTag;
        m_onStatus = onStatus;
    }

    void start(const std::string& mode, const std::string& comPort, const std::string& wifiIp, int baudRate = 115200) {
        stop();
        m_mode = mode;
        m_comPort = comPort;
        m_wifiIp = wifiIp;
        m_baudRate = baudRate;
        m_running = true;

        m_hThread = CreateThread(NULL, 0, WorkerThreadProc, this, 0, NULL);
    }

    void switchPort(const std::string& port) {
        m_comPort = port;
        if (m_hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hSerial);
            m_hSerial = INVALID_HANDLE_VALUE;
        }
        m_connected = false;
    }

    void stop() {
        m_running = false;
        if (m_hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hSerial);
            m_hSerial = INVALID_HANDLE_VALUE;
        }
        if (m_hThread) {
            WaitForSingleObject(m_hThread, 1000);
            CloseHandle(m_hThread);
            m_hThread = NULL;
        }
        m_connected = false;
    }

    bool isConnected() const { return m_connected; }
    std::string getActivePortOrIp() const { return m_activeEndpoint; }

    struct AsyncSerialCmd {
        NFCEngine* engine;
        std::string cmd;
    };

    static DWORD WINAPI AsyncSerialThreadProc(LPVOID lp) {
        AsyncSerialCmd* data = (AsyncSerialCmd*)lp;
        if (data && data->engine && data->engine->m_hSerial != INVALID_HANDLE_VALUE) {
            std::string line = data->cmd + "\n";
            DWORD written = 0;
            WriteFile(data->engine->m_hSerial, line.c_str(), (DWORD)line.length(), &written, NULL);
        }
        delete data;
        return 0;
    }

    bool sendCommand(const std::string& cmd) {
        AsyncSerialCmd* data = new AsyncSerialCmd{ this, cmd };
        CreateThread(NULL, 0, AsyncSerialThreadProc, data, 0, NULL);
        return true;
    }

    struct ArmWriterArgs {
        std::string ip;
        std::string type;
        std::string payload;
    };

    static DWORD WINAPI ArmWriterThreadProc(LPVOID lp) {
        ArmWriterArgs* args = (ArmWriterArgs*)lp;
        std::string uIp = args->ip;
        std::string uType = args->type;
        std::string uPayload = args->payload;
        delete args;

        std::string postData = "type=" + uType + "&payload=" + uPayload;
        HINTERNET hInternet = InternetOpenA("NFC_Desktop_Hub", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hInternet) {
            DWORD timeout = 1000;
            InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            HINTERNET hConnect = InternetConnectA(hInternet, uIp.c_str(), 80, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
            if (hConnect) {
                HINTERNET hReq = HttpOpenRequestA(hConnect, "POST", "/api/arm", NULL, NULL, NULL, INTERNET_FLAG_RELOAD, 0);
                if (hReq) {
                    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
                    HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
                    InternetCloseHandle(hReq);
                }
                InternetCloseHandle(hConnect);
            }
            InternetCloseHandle(hInternet);
        }
        return 0;
    }

    static DWORD WINAPI DisarmWriterThreadProc(LPVOID lp) {
        std::string* pIp = (std::string*)lp;
        std::string url = "http://" + *pIp + "/api/disarm";
        delete pIp;
        HINTERNET hInternet = InternetOpenA("NFC_Desktop_Hub", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hInternet) {
            DWORD timeout = 1000;
            InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
            if (hUrl) InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
        }
        return 0;
    }

    bool armWriter(const std::string& type, const std::string& payload) {
        std::string cmd = "ARM:" + type + ":" + payload;
        sendCommand(cmd);

        if (m_mode == "wifi" && !m_wifiIp.empty()) {
            ArmWriterArgs* args = new ArmWriterArgs{ m_wifiIp, type, payload };
            CreateThread(NULL, 0, ArmWriterThreadProc, args, 0, NULL);
        }
        return true;
    }

    bool disarmWriter() {
        sendCommand("DISARM");

        if (m_mode == "wifi" && !m_wifiIp.empty()) {
            std::string* pIp = new std::string(m_wifiIp);
            CreateThread(NULL, 0, DisarmWriterThreadProc, pIp, 0, NULL);
        }
        return true;
    }

    static std::vector<std::string> enumerateComPorts() {
        std::vector<std::string> ports;
        char targetPath[256];
        for (int i = 1; i <= 32; i++) {
            std::string portName = "COM" + to_str(i);
            DWORD res = QueryDosDeviceA(portName.c_str(), targetPath, sizeof(targetPath));
            if (res != 0) {
                ports.push_back(portName);
            }
        }
        return ports;
    }

private:
    static DWORD WINAPI WorkerThreadProc(LPVOID param) {
        NFCEngine* self = (NFCEngine*)param;
        self->runWorker();
        return 0;
    }

    void runWorker() {
        while (m_running) {
            if (m_mode == "serial" || m_mode == "auto") {
                if (trySerialLoop()) {
                    continue;
                }
            }

            if (m_mode == "wifi") {
                if (tryWifiLoop()) {
                    continue;
                }
            }

            // If neither connected, wait and retry
            std::string dispPort = (m_comPort.empty() || m_comPort == "auto") ? "COM8" : m_comPort;
            if (m_onStatus) m_onStatus(false, "Connecting (" + dispPort + ")...");
            m_connected = false;
            m_activeEndpoint = "None";
            for (int i = 0; i < 10 && m_running; i++) Sleep(100);
        }
    }

    bool trySerialLoop() {
        std::vector<std::string> tryPorts;
        if (!m_comPort.empty() && m_comPort != "auto") {
            tryPorts.push_back(m_comPort);
        } else {
            // Prioritize COM8 if present, then enumerate others
            std::vector<std::string> available = enumerateComPorts();
            for (const auto& p : available) {
                if (p == "COM8") tryPorts.insert(tryPorts.begin(), p);
                else tryPorts.push_back(p);
            }
        }

        for (const auto& port : tryPorts) {
            if (!m_running) return false;

            std::string fullPortName = "\\\\.\\" + port;
            m_hSerial = CreateFileA(fullPortName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

            if (m_hSerial == INVALID_HANDLE_VALUE) continue;

            DCB dcbSerialParams = {0};
            dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
            if (GetCommState(m_hSerial, &dcbSerialParams)) {
                dcbSerialParams.BaudRate = m_baudRate;
                dcbSerialParams.ByteSize = 8;
                dcbSerialParams.StopBits = ONESTOPBIT;
                dcbSerialParams.Parity   = NOPARITY;
                // CRUCIAL FOR ESP32-C3 USB-Serial/JTAG CDC: DTR must be asserted, RTS must NOT reset chip!
                dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
                dcbSerialParams.fRtsControl = RTS_CONTROL_DISABLE;
                SetCommState(m_hSerial, &dcbSerialParams);
            }

            EscapeCommFunction(m_hSerial, SETDTR);
            EscapeCommFunction(m_hSerial, CLRRTS);

            COMMTIMEOUTS timeouts = {0};
            timeouts.ReadIntervalTimeout = 20;
            timeouts.ReadTotalTimeoutConstant = 50;
            timeouts.ReadTotalTimeoutMultiplier = 5;
            SetCommTimeouts(m_hSerial, &timeouts);

            // Probe device
            std::string probe = "STATUS\n";
            DWORD written = 0;
            WriteFile(m_hSerial, probe.c_str(), (DWORD)probe.length(), &written, NULL);

            // Synchronize Windows Lock State to ESP32 immediately on connection!
            bool locked = ActionExecutor::isWorkstationLocked();
            std::string syncCmd = locked ? "SET_LOCK_STATE:1\n" : "SET_LOCK_STATE:0\n";
            WriteFile(m_hSerial, syncCmd.c_str(), (DWORD)syncCmd.length(), &written, NULL);

            m_connected = true;
            m_activeEndpoint = "USB (" + port + ")";
            if (m_onStatus) m_onStatus(true, "Connected via USB (" + port + ")");

            std::string lineBuf = "";
            char buffer[512];
            DWORD bytesRead = 0;

            TagEvent currentEvent;
            currentEvent.cardPresent = false;
            currentEvent.scanId = 0;

            while (m_running && m_connected) {
                if (ReadFile(m_hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                    if (bytesRead > 0) {
                        buffer[bytesRead] = '\0';
                        lineBuf += buffer;

                        size_t nlPos;
                        while ((nlPos = lineBuf.find('\n')) != std::string::npos) {
                            std::string line = lineBuf.substr(0, nlPos);
                            lineBuf = lineBuf.substr(nlPos + 1);
                            
                            // Clean carriage return
                            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                            
                            processSerialLine(line, currentEvent);
                        }
                    }
                } else {
                    // Serial disconnected
                    break;
                }
                Sleep(10);
            }

            CloseHandle(m_hSerial);
            m_hSerial = INVALID_HANDLE_VALUE;
            m_connected = false;
        }

        return false;
    }

    void processSerialLine(const std::string& line, TagEvent& evt) {
        if (line.empty()) return;

        // Detect autonomous hardware BLE keystroke execution from ESP32
        if (line.find("Typing PIN") != std::string::npos && line.find("over BLE") != std::string::npos) {
            m_lastBleHandledTick = GetTickCount();
        }

        // Examples:
        // "[NFC] Card Detected! UID: A2:CC:3C:D7 | Type: MIFARE 1KB"
        // "Card Detected! UID: 57:61:A8:E3"
        // "[NFC] UID: 04:6B:A2:1F:33:6C:80"
        if (line.find("Card Detected") != std::string::npos || (line.find("[NFC]") != std::string::npos && line.find("UID:") != std::string::npos)) {
            evt.cardPresent = true;
            evt.scanId++;

            size_t uidPos = line.find("UID: ");
            if (uidPos != std::string::npos) {
                size_t start = uidPos + 5;
                size_t barPos = line.find(" |", start);
                if (barPos != std::string::npos) {
                    evt.uid = line.substr(start, barPos - start);
                } else {
                    size_t endPos = line.find_first_of(" \r\n", start);
                    evt.uid = (endPos != std::string::npos) ? line.substr(start, endPos - start) : line.substr(start);
                }
            }

            size_t typePos = line.find("Type: ");
            if (typePos != std::string::npos) {
                size_t start = typePos + 6;
                size_t endPos = line.find_first_of("|\r\n", start);
                evt.type = (endPos != std::string::npos) ? line.substr(start, endPos - start) : line.substr(start);
                while (!evt.type.empty() && evt.type.back() == ' ') evt.type.pop_back();
            } else if (evt.type.empty()) {
                evt.type = "NFC Tag";
            }

            size_t pPos = line.find("Payload: ");
            if (pPos != std::string::npos) {
                evt.payload = line.substr(pPos + 9);
                while (!evt.payload.empty() && (evt.payload.back() == '\r' || evt.payload.back() == '\n' || evt.payload.back() == ' ')) evt.payload.pop_back();
            } else {
                evt.payload = "";
            }

            evt.bleHandled = (GetTickCount() - m_lastBleHandledTick < 1500);
            m_cardPresent = true;
            m_lastUid = evt.uid;

            // INSTANT DISPATCH: Fire immediately on UID recognition for zero-latency audio and automation!
            if (m_onTag) m_onTag(evt);
        } 
        else if (line.find("Payload: ") != std::string::npos) {
            size_t pPos = line.find("Payload: ");
            evt.payload = line.substr(pPos + 9);
            while (!evt.payload.empty() && (evt.payload.back() == '\r' || evt.payload.back() == '\n' || evt.payload.back() == ' ')) evt.payload.pop_back();
            
            // If card has payload, update event for payload-based rules / UI
            evt.bleHandled = (GetTickCount() - m_lastBleHandledTick < 1500);
            if (m_onTag) m_onTag(evt);
        } 
        else if (line.find("Card Removed") != std::string::npos) {
            evt.cardPresent = false;
            evt.payload = "";
            m_cardPresent = false;
            m_lastUid = "";
            if (m_onTag) m_onTag(evt);
        }
    }

    bool tryWifiLoop() {
        std::string url = "http://" + m_wifiIp + "/api/status";
        
        HINTERNET hInternet = InternetOpenA("NFC_Desktop_Hub", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hInternet) return false;

        DWORD timeoutMs = 1200;
        InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
        InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

        bool wifiOnline = false;
        int failureCount = 0;

        while (m_running && failureCount < 5) {
            HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, 
                                              INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
            if (hUrl) {
                char readBuf[1024];
                DWORD bytesRead = 0;
                std::string jsonResp = "";
                while (InternetReadFile(hUrl, readBuf, sizeof(readBuf) - 1, &bytesRead) && bytesRead > 0) {
                    readBuf[bytesRead] = '\0';
                    jsonResp += readBuf;
                }
                InternetCloseHandle(hUrl);

                if (!jsonResp.empty()) {
                    if (!wifiOnline) {
                        wifiOnline = true;
                        m_connected = true;
                        m_activeEndpoint = "Wi-Fi (" + m_wifiIp + ")";
                        if (m_onStatus) m_onStatus(true, "Connected via Wi-Fi (" + m_wifiIp + ")");
                    }
                    failureCount = 0;
                    parseAndDispatchWifiJson(jsonResp);
                }
            } else {
                failureCount++;
            }
            Sleep(120);
        }

        InternetCloseHandle(hInternet);
        m_connected = false;
        return false;
    }

    void parseAndDispatchWifiJson(const std::string& json) {
        bool cardPresent = (json.find("\"card_present\":true") != std::string::npos);
        uint32_t scanId = (uint32_t)extractNumber(json, "scan_id", 0);
        std::string uid = extractString(json, "uid", "");
        std::string type = extractString(json, "type", "");
        std::string payload = extractString(json, "payload", "");

        TagEvent evt;
        evt.cardPresent = cardPresent;
        evt.scanId = scanId;
        evt.uid = uid;
        evt.type = type.empty() ? "NFC Tag" : type;
        evt.payload = payload;

        if (cardPresent) {
            if (scanId != m_lastScanId || !m_cardPresent || uid != m_lastUid) {
                m_lastScanId = scanId;
                m_cardPresent = true;
                m_lastUid = uid;
                if (m_onTag) m_onTag(evt);
            }
        } else {
            if (m_cardPresent) {
                m_cardPresent = false;
                m_lastUid = "";
                if (m_onTag) m_onTag(evt);
            }
        }
    }

    static std::string extractString(const std::string& json, const std::string& key, const std::string& defaultVal) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return defaultVal;
        size_t colon = json.find(':', pos + pattern.length());
        if (colon == std::string::npos) return defaultVal;
        size_t q1 = json.find('"', colon);
        if (q1 == std::string::npos) return defaultVal;
        size_t q2 = json.find('"', q1 + 1);
        while (q2 != std::string::npos && json[q2 - 1] == '\\') {
            q2 = json.find('"', q2 + 1);
        }
        if (q2 == std::string::npos) return defaultVal;
        return json.substr(q1 + 1, q2 - q1 - 1);
    }

    static double extractNumber(const std::string& json, const std::string& key, double defaultVal) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return defaultVal;
        size_t colon = json.find(':', pos + pattern.length());
        if (colon == std::string::npos) return defaultVal;
        size_t start = json.find_first_of("-0123456789.", colon);
        if (start == std::string::npos) return defaultVal;
        size_t end = json.find_first_not_of("-0123456789.", start);
        std::string numStr = json.substr(start, end - start);
        return atof(numStr.c_str());
    }

    HANDLE m_hSerial;
    HANDLE m_hThread;
    bool m_running;
    bool m_connected;
    std::string m_activeEndpoint;

    std::string m_mode;
    std::string m_comPort;
    std::string m_wifiIp;
    int m_baudRate;

    uint32_t m_lastScanId;
    bool m_cardPresent;
    std::string m_lastUid;
    DWORD m_lastTriggerTime;
    DWORD m_lastBleHandledTick;

    TagCallback m_onTag;
    StatusCallback m_onStatus;
};
