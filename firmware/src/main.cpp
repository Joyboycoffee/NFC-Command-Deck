#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "BleHidCombo.h"
#include <vector>
#include <Preferences.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "html_gz.h"

// Wake-on-LAN (WoL) Engine Preferences
bool g_wolEnabled = true;
String g_wolMac = "10:FF:E0:4E:5A:20";


// ---------------------------------------------------------------------------------
// ESP-NOW WIRELESS RFID BROADCAST TO RETRO-GO / BRUCE ESP32-S3
// ---------------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    char magic[4];        // "RFID"
    uint8_t uidLength;    // Length of UID (4, 7, or 10 bytes)
    uint8_t uidBytes[10]; // Raw UID bytes
    char uidString[24];   // Formatted HEX string (e.g. "B4:A1:72:0F")
    char piccType[24];    // PICC card type name (e.g. "MIFARE 1KB")
    uint32_t scanCount;   // Cumulative scan counter
} RFID_Packet;

static uint8_t s_espNowBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool s_espNowReady = false;
bool g_wifiActive = false;

// Autonomous Hardware Unlock & Lock Engine (Flash NVS)
Preferences g_prefs;
String g_autoUnlockUid = "27:82:D3:A9";
String g_autoUnlockPin = "1911";
bool g_autoUnlockEnabled = true;
bool g_pcIsLocked = true; // Initialized to true for Cold Boot Unlock

void initEspNowSender() {
    if (!g_wifiActive || WiFi.getMode() == WIFI_OFF) {
        return; // Wi-Fi PHY is unpowered, do not call esp_now_init
    }
    if (esp_now_init() == ESP_OK) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, s_espNowBroadcastMac, 6);
        peerInfo.channel = 0; // 0 = track current WiFi channel
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            s_espNowReady = true;
            Serial.println("[ESP-NOW] Broadcast sender registered successfully!");
        } else {
            Serial.println("[ESP-NOW] Failed to add broadcast peer!");
        }
    } else {
        Serial.println("[ESP-NOW] esp_now_init failed!");
    }
}

void broadcastEspNowCard(const String &uidStr, const String &typeName, byte *uidBytes, byte uidSize, uint32_t count) {
    if (!g_wifiActive || WiFi.getMode() == WIFI_OFF) {
        return; // Skip ESP-NOW broadcast completely when Wi-Fi is OFF to prevent kernel panic
    }
    if (!s_espNowReady) {
        initEspNowSender();
    }
    if (s_espNowReady) {
        RFID_Packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        memcpy(pkt.magic, "RFID", 4);
        pkt.uidLength = uidSize;
        memcpy(pkt.uidBytes, uidBytes, uidSize < 10 ? uidSize : 10);
        strncpy(pkt.uidString, uidStr.c_str(), sizeof(pkt.uidString) - 1);
        strncpy(pkt.piccType, typeName.c_str(), sizeof(pkt.piccType) - 1);
        pkt.scanCount = count;

        for (int i = 0; i < 3; i++) {
            esp_now_send(s_espNowBroadcastMac, (const uint8_t *)&pkt, sizeof(pkt));
            delay(5);
        }
        Serial.printf("[ESP-NOW] Broadcasted card UID %s (#%u) to S3\n", uidStr.c_str(), count);
    }
}


// ---------------------------------------------------------------------------------
// HARDWARE CONFIGURATION & PINOUT (ESP32-C3 Super Mini + RFID-RC522)
// ---------------------------------------------------------------------------------
#define PIN_RST    3     // RC522 RST
#define PIN_SCK    4     // RC522 SCK
#define PIN_MISO   5     // RC522 MISO
#define PIN_MOSI   6     // RC522 MOSI
#define PIN_SS     7     // RC522 SDA (SS)
#define PIN_LED    8     // Onboard Blue LED (Active LOW on Super Mini)

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef WIFI_SSID1
#define WIFI_SSID1 "YOUR_WIFI_SSID_1"
#endif
#ifndef WIFI_SSID2
#define WIFI_SSID2 "YOUR_WIFI_SSID_2"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#endif
#ifndef AP_SSID
#define AP_SSID "NFC-Studio"
#endif
#ifndef AP_PASS
#define AP_PASS "password123"
#endif

const char* sta_ssid1 = WIFI_SSID1;
const char* sta_ssid2 = WIFI_SSID2;
const char* sta_pass  = WIFI_PASS;

const char* ap_ssid   = AP_SSID;
const char* ap_pass   = AP_PASS;

// Static IP Configuration (Subnet 192.168.1.0/24)
IPAddress staticIP(192, 168, 1, 37);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1);
IPAddress secondaryDNS(8, 8, 8, 8);

AsyncWebServer server(80);
MFRC522 mfrc522(PIN_SS, PIN_RST);
byte rc522Version = 0x00;

// LED Helpers (Active LOW)
void ledOn()  { digitalWrite(PIN_LED, LOW); }
void ledOff() { digitalWrite(PIN_LED, HIGH); }

// Tag State
struct TagData {
    String uid = "--:--:--:--";
    String type = "None";
    String sak = "00";
    String payload = "";
} currentTag;

bool cardPresent = false;
uint8_t consecutiveMisses = 0;
uint32_t scanId = 0;

enum OpMode {
    MODE_IDLE,
    MODE_WRITE_URL,
    MODE_WRITE_TEXT,
    MODE_RESTORE_DUMP,
    MODE_WIPE
};

OpMode currentMode = MODE_IDLE;
String armedType = "none";
String armedPayload = "";
String lastActionStatus = "Idle";
String lastDumpJson = "{}";

// ---------------------------------------------------------------------------------
// HEX HELPERS
// ---------------------------------------------------------------------------------
String bytesToHex(const byte* buffer, byte bufferSize) {
    String hexStr = "";
    for (byte i = 0; i < bufferSize; i++) {
        if (buffer[i] < 0x10) hexStr += "0";
        hexStr += String(buffer[i], HEX);
    }
    hexStr.toUpperCase();
    return hexStr;
}

bool hexToBytes(const String& hexStr, byte* buffer, size_t bufferSize) {
    String cleanHex = "";
    for (size_t i = 0; i < hexStr.length(); i++) {
        char c = hexStr[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            cleanHex += c;
        }
    }
    if (cleanHex.length() < bufferSize * 2) return false;
    for (size_t i = 0; i < bufferSize; i++) {
        String byteString = cleanHex.substring(i * 2, i * 2 + 2);
        buffer[i] = (byte)strtol(byteString.c_str(), NULL, 16);
    }
    return true;
}

// ---------------------------------------------------------------------------------
// NDEF PARSER (URI & TEXT)
// ---------------------------------------------------------------------------------
bool parseNdef(const byte* data, size_t len, String& outPayload) {
    for (size_t i = 0; i + 4 < len; i++) {
        if (data[i] == 0x03) { // NDEF Message TLV
            size_t recStart = i + 2;
            uint32_t tlvLen = data[i + 1];
            if (tlvLen == 0xFF) {
                if (i + 4 >= len) continue;
                tlvLen = (data[i + 2] << 8) | data[i + 3];
                recStart = i + 4;
            }

            if (recStart < len) {
                byte header = data[recStart];
                bool sr = (header & 0x10) != 0;
                bool il = (header & 0x08) != 0;
                byte typeLen = data[recStart + 1];
                uint32_t payloadLen = 0;
                size_t pos = recStart + 2;

                if (sr) {
                    payloadLen = data[pos++];
                } else if (pos + 4 <= len) {
                    payloadLen = (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3];
                    pos += 4;
                }

                if (il && pos < len) pos++; // Skip ID length

                // URI Record ('U' / 0x55)
                if (typeLen == 1 && pos < len && data[pos] == 0x55) {
                    pos++;
                    if (pos < len) {
                        byte prefixCode = data[pos++];
                        String prefix = "";
                        switch (prefixCode) {
                            case 0x01: prefix = "http://www."; break;
                            case 0x02: prefix = "https://www."; break;
                            case 0x03: prefix = "http://"; break;
                            case 0x04: prefix = "https://"; break;
                            case 0x05: prefix = "tel:"; break;
                            case 0x06: prefix = "mailto:"; break;
                            default:   prefix = ""; break;
                        }
                        String body = "";
                        for (uint32_t k = 1; k < payloadLen && pos < len; k++) {
                            body += (char)data[pos++];
                        }
                        outPayload = prefix + body;
                        return true;
                    }
                }

                // Text Record ('T' / 0x54)
                if (typeLen == 1 && pos < len && data[pos] == 0x54) {
                    pos++;
                    if (pos < len) {
                        byte statusByte = data[pos++];
                        byte langLen = statusByte & 0x3F;
                        pos += langLen; // Skip language code
                        String text = "";
                        for (uint32_t k = 1 + langLen; k < payloadLen && pos < len; k++) {
                            text += (char)data[pos++];
                        }
                        outPayload = text;
                        return true;
                    }
                }
            }
        }
    }

    // Fallback: Check if there is raw ASCII URL in the data
    String s = "";
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) s += (char)data[i];
        else s += '\0';
    }
    int idxHttps = s.indexOf("https://");
    int idxHttp = s.indexOf("http://");
    int startIdx = (idxHttps != -1) ? idxHttps : idxHttp;
    if (startIdx != -1) {
        String url = "";
        for (size_t i = startIdx; i < s.length(); i++) {
            char c = s[i];
            if (c == '\0' || c == ' ' || c == '"' || c == '<' || c == '>' || c == '\'') break;
            url += c;
        }
        if (url.length() > 8) {
            outPayload = url;
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------------
// CARD READING ROUTINES
// ---------------------------------------------------------------------------------
void reselectPicc() {
    mfrc522.PCD_StopCrypto1();
    byte atqa[2];
    byte atqaSz = sizeof(atqa);
    mfrc522.PICC_WakeupA(atqa, &atqaSz);
    mfrc522.PICC_Select(&(mfrc522.uid));
}

bool authSector(byte sector, MFRC522::MIFARE_Key& matchedKey, byte& matchedCmd) {
    byte blockAddr = sector * 4;
    static const byte knownKeys[][6] = {
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Factory Default Key A/B
        {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}, // NFC Forum NDEF Key A
        {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}  // MAD Key A
    };

    for (size_t k = 0; k < 3; k++) {
        MFRC522::MIFARE_Key testKey;
        memcpy(testKey.keyByte, knownKeys[k], 6);

        if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &testKey, &(mfrc522.uid)) == MFRC522::STATUS_OK) {
            matchedKey = testKey;
            matchedCmd = MFRC522::PICC_CMD_MF_AUTH_KEY_A;
            return true;
        }
        reselectPicc();

        if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_B, blockAddr, &testKey, &(mfrc522.uid)) == MFRC522::STATUS_OK) {
            matchedKey = testKey;
            matchedCmd = MFRC522::PICC_CMD_MF_AUTH_KEY_B;
            return true;
        }
        reselectPicc();
    }
    return false;
}

void readMifareClassic(String& outPayload, const String& uidStr, const String& typeName, const String& sakStr) {
    MFRC522::MIFARE_Key activeKey;
    byte activeCmd = 0;
    byte rawData[128] = {0};
    int validBytes = 0;

    // Fast check: authenticate Sector 1 first (standard NDEF sector)
    if (!authSector(1, activeKey, activeCmd)) {
        outPayload = "(Mifare Access-Protected / Non-default Keys)";
        return;
    }

    // Read Sector 1
    for (byte b = 0; b < 3; b++) {
        byte blockAddr = 1 * 4 + b;
        byte buf[18];
        byte sz = sizeof(buf);
        if (mfrc522.MIFARE_Read(blockAddr, buf, &sz) == MFRC522::STATUS_OK) {
            memcpy(rawData + validBytes, buf, 16);
            validBytes += 16;
        }
    }
    reselectPicc();

    // Check if there is an NDEF message in Sector 1
    if (validBytes > 0 && parseNdef(rawData, validBytes, outPayload)) {
        return;
    }

    // If needed, check sectors 2 and 3 using the activeKey that already worked on sector 1
    for (byte sector = 2; sector <= 3; sector++) {
        byte blockAddr = sector * 4;
        if (mfrc522.PCD_Authenticate(activeCmd, blockAddr, &activeKey, &(mfrc522.uid)) == MFRC522::STATUS_OK) {
            for (byte b = 0; b < 3; b++) {
                byte buf[18];
                byte sz = sizeof(buf);
                if (mfrc522.MIFARE_Read(blockAddr + b, buf, &sz) == MFRC522::STATUS_OK) {
                    if (validBytes + 16 <= 128) {
                        memcpy(rawData + validBytes, buf, 16);
                        validBytes += 16;
                    }
                }
            }
            reselectPicc();
        }
    }

    if (validBytes > 0 && parseNdef(rawData, validBytes, outPayload)) {
        return;
    }

    bool allZero = true;
    for (int i = 0; i < validBytes; i++) {
        if (rawData[i] != 0) { allZero = false; break; }
    }
    outPayload = allZero ? "(Tag Memory Blank / Unformatted)" : "(No NDEF Record Found)";
}

void readNtag(String& outPayload, const String& uidStr, const String& typeName, const String& sakStr) {
    byte memory[128] = {0};
    int bytesRead = 0;

    for (byte page = 4; page <= 36; page += 4) {
        byte buf[18];
        byte sz = sizeof(buf);
        if (mfrc522.MIFARE_Read(page, buf, &sz) == MFRC522::STATUS_OK) {
            for (int b = 0; b < 16 && (bytesRead + b) < 128; b++) {
                memory[bytesRead + b] = buf[b];
            }
            bytesRead += 16;
        } else break;
    }

    if (bytesRead == 0) {
        outPayload = "(Read Error - Re-align tag)";
        return;
    }

    if (parseNdef(memory, bytesRead, outPayload)) {
        return;
    }

    bool allZero = true;
    for (int i = 0; i < bytesRead; i++) {
        if (memory[i] != 0) { allZero = false; break; }
    }
    outPayload = allZero ? "(Tag Memory Blank / Unformatted)" : "(No NDEF Record Found)";
}

// ---------------------------------------------------------------------------------
// CARD DUMP ROUTINES
// ---------------------------------------------------------------------------------
void dumpTagMemory(MFRC522::PICC_Type piccType, const String& uidStr, const String& typeName, const String& sakStr) {
    String json = "{\n";
    json += "  \"uid\": \"" + uidStr + "\",\n";
    json += "  \"type\": \"" + typeName + "\",\n";
    json += "  \"sak\": \"" + sakStr + "\",\n";

    if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
        json += "  \"tag_standard\": \"ntag\",\n";
        json += "  \"pages\": {\n";
        bool first = true;
        for (byte page = 0; page <= 44; page += 4) {
            byte buf[18];
            byte sz = sizeof(buf);
            if (mfrc522.MIFARE_Read(page, buf, &sz) == MFRC522::STATUS_OK) {
                for (byte p = 0; p < 4 && (page + p) <= 44; p++) {
                    if (!first) json += ",\n";
                    first = false;
                    byte pageBytes[4] = { buf[p * 4], buf[p * 4 + 1], buf[p * 4 + 2], buf[p * 4 + 3] };
                    json += "    \"" + String(page + p) + "\": \"" + bytesToHex(pageBytes, 4) + "\"";
                }
            } else {
                break;
            }
        }
        json += "\n  }\n}";
    } else if (piccType == MFRC522::PICC_TYPE_MIFARE_1K || piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
        json += "  \"tag_standard\": \"mifare_classic\",\n";
        json += "  \"blocks\": {\n";
        bool first = true;
        byte numSectors = (piccType == MFRC522::PICC_TYPE_MIFARE_1K) ? 16 : 40;
        MFRC522::MIFARE_Key activeKey;
        byte activeCmd = 0;

        for (byte sector = 0; sector < numSectors; sector++) {
            if (authSector(sector, activeKey, activeCmd)) {
                byte numBlocks = (sector < 32) ? 4 : 16;
                byte firstBlock = (sector < 32) ? (sector * 4) : (128 + (sector - 32) * 16);
                for (byte b = 0; b < numBlocks; b++) {
                    byte blockAddr = firstBlock + b;
                    byte buf[18];
                    byte sz = sizeof(buf);
                    if (mfrc522.MIFARE_Read(blockAddr, buf, &sz) == MFRC522::STATUS_OK) {
                        if (!first) json += ",\n";
                        first = false;
                        json += "    \"" + String(blockAddr) + "\": \"" + bytesToHex(buf, 16) + "\"";
                    }
                }
            }
            reselectPicc();
        }
        json += "\n  }\n}";
    } else {
        json += "  \"tag_standard\": \"generic\",\n";
        json += "  \"blocks\": {}\n}";
    }

    lastDumpJson = json;
}

// ---------------------------------------------------------------------------------
// CARD RESTORE ROUTINES
// ---------------------------------------------------------------------------------
bool restoreDumpToTag(MFRC522::PICC_Type piccType, const String& dumpPayload) {
    if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
        // Restore pages for NTAG (Pages 3 to 44)
        int written = 0;
        for (int page = 3; page <= 44; page++) {
            String key = "\"" + String(page) + "\"";
            int idx = dumpPayload.indexOf(key);
            if (idx == -1) continue;
            int colonIdx = dumpPayload.indexOf(':', idx);
            if (colonIdx == -1) continue;
            int q1 = dumpPayload.indexOf('"', colonIdx);
            if (q1 == -1) continue;
            int q2 = dumpPayload.indexOf('"', q1 + 1);
            if (q2 == -1) continue;

            String hexData = dumpPayload.substring(q1 + 1, q2);
            byte pageBytes[4];
            if (hexToBytes(hexData, pageBytes, 4)) {
                if (mfrc522.MIFARE_Ultralight_Write(page, pageBytes, 4) == MFRC522::STATUS_OK) {
                    written++;
                }
            }
        }
        return (written > 0);
    } else if (piccType == MFRC522::PICC_TYPE_MIFARE_1K || piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
        // Restore blocks for MIFARE Classic
        int written = 0;
        MFRC522::MIFARE_Key activeKey;
        byte activeCmd = 0;

        for (int block = 1; block < 64; block++) {
            // Skip sector trailers (block % 4 == 3) to protect keys/access bits
            if ((block % 4) == 3) continue;

            String key = "\"" + String(block) + "\"";
            int idx = dumpPayload.indexOf(key);
            if (idx == -1) continue;
            int colonIdx = dumpPayload.indexOf(':', idx);
            if (colonIdx == -1) continue;
            int q1 = dumpPayload.indexOf('"', colonIdx);
            if (q1 == -1) continue;
            int q2 = dumpPayload.indexOf('"', q1 + 1);
            if (q2 == -1) continue;

            String hexData = dumpPayload.substring(q1 + 1, q2);
            byte blockBytes[16];
            if (hexToBytes(hexData, blockBytes, 16)) {
                byte sector = block / 4;
                if (authSector(sector, activeKey, activeCmd)) {
                    if (mfrc522.MIFARE_Write(block, blockBytes, 16) == MFRC522::STATUS_OK) {
                        written++;
                    }
                }
                reselectPicc();
            }
        }
        return (written > 0);
    }
    return false;
}

// ---------------------------------------------------------------------------------
// CARD WRITING ROUTINES
// ---------------------------------------------------------------------------------
bool writeNdefUrlToNtag(const String& fullUrl) {
    byte prefixCode = 0x00;
    String urlBody = fullUrl;

    if (fullUrl.startsWith("http://www.")) { prefixCode = 0x01; urlBody = fullUrl.substring(11); }
    else if (fullUrl.startsWith("https://www.")) { prefixCode = 0x02; urlBody = fullUrl.substring(12); }
    else if (fullUrl.startsWith("http://")) { prefixCode = 0x03; urlBody = fullUrl.substring(7); }
    else if (fullUrl.startsWith("https://")) { prefixCode = 0x04; urlBody = fullUrl.substring(8); }

    uint8_t payloadLen = 1 + urlBody.length();
    uint8_t ndefRecordLen = 1 + 1 + 1 + 1 + payloadLen;
    uint8_t totalTlvLen = 2 + ndefRecordLen + 1;

    std::vector<uint8_t> buffer;
    buffer.reserve(totalTlvLen + 4);
    buffer.push_back(0x03);
    buffer.push_back(ndefRecordLen);
    buffer.push_back(0xD1);
    buffer.push_back(0x01);
    buffer.push_back(payloadLen);
    buffer.push_back(0x55);
    buffer.push_back(prefixCode);

    for (size_t i = 0; i < urlBody.length(); i++) buffer.push_back((uint8_t)urlBody[i]);
    buffer.push_back(0xFE);
    while (buffer.size() % 4 != 0) buffer.push_back(0x00);

    // Capability Container on Page 3
    byte ccData[4] = { 0xE1, 0x10, 0x12, 0x00 };
    mfrc522.MIFARE_Ultralight_Write(3, ccData, 4);

    byte page = 4;
    for (size_t offset = 0; offset < buffer.size(); offset += 4) {
        byte pageData[4] = { buffer[offset], buffer[offset + 1], buffer[offset + 2], buffer[offset + 3] };
        if (mfrc522.MIFARE_Ultralight_Write(page, pageData, 4) != MFRC522::STATUS_OK) return false;
        page++;
    }
    return true;
}

bool writeNdefUrlToMifareClassic(const String& fullUrl) {
    MFRC522::MIFARE_Key activeKey;
    byte activeCmd = 0;
    if (!authSector(1, activeKey, activeCmd)) return false;

    byte prefixCode = 0x00;
    String urlBody = fullUrl;

    if (fullUrl.startsWith("http://www.")) { prefixCode = 0x01; urlBody = fullUrl.substring(11); }
    else if (fullUrl.startsWith("https://www.")) { prefixCode = 0x02; urlBody = fullUrl.substring(12); }
    else if (fullUrl.startsWith("http://")) { prefixCode = 0x03; urlBody = fullUrl.substring(7); }
    else if (fullUrl.startsWith("https://")) { prefixCode = 0x04; urlBody = fullUrl.substring(8); }

    uint8_t payloadLen = 1 + urlBody.length();
    uint8_t ndefRecordLen = 1 + 1 + 1 + 1 + payloadLen;

    std::vector<uint8_t> buffer;
    buffer.reserve(48);
    buffer.push_back(0x03);
    buffer.push_back(ndefRecordLen);
    buffer.push_back(0xD1);
    buffer.push_back(0x01);
    buffer.push_back(payloadLen);
    buffer.push_back(0x55);
    buffer.push_back(prefixCode);

    for (size_t i = 0; i < urlBody.length(); i++) buffer.push_back((uint8_t)urlBody[i]);
    buffer.push_back(0xFE);
    while (buffer.size() < 48) buffer.push_back(0x00);

    for (byte b = 0; b < 3; b++) {
        byte blockAddr = 4 + b;
        byte blockData[16];
        memcpy(blockData, buffer.data() + (b * 16), 16);
        if (mfrc522.MIFARE_Write(blockAddr, blockData, 16) != MFRC522::STATUS_OK) {
            return false;
        }
    }
    return true;
}

bool writeNdefTextToNtag(const String& text) {
    uint8_t textLen = text.length();
    uint8_t payloadLen = 1 + 2 + textLen;
    uint8_t ndefRecordLen = 1 + 1 + 1 + 1 + payloadLen;
    uint8_t totalTlvLen = 2 + ndefRecordLen + 1;

    std::vector<uint8_t> buffer;
    buffer.reserve(totalTlvLen + 4);
    buffer.push_back(0x03);
    buffer.push_back(ndefRecordLen);
    buffer.push_back(0xD1);
    buffer.push_back(0x01);
    buffer.push_back(payloadLen);
    buffer.push_back(0x54); // 'T'
    buffer.push_back(0x02); // UTF-8, lang len 2
    buffer.push_back('e');
    buffer.push_back('n');

    for (size_t i = 0; i < textLen; i++) buffer.push_back((uint8_t)text[i]);
    buffer.push_back(0xFE);
    while (buffer.size() % 4 != 0) buffer.push_back(0x00);

    byte ccData[4] = { 0xE1, 0x10, 0x12, 0x00 };
    mfrc522.MIFARE_Ultralight_Write(3, ccData, 4);

    byte page = 4;
    for (size_t offset = 0; offset < buffer.size(); offset += 4) {
        byte pageData[4] = { buffer[offset], buffer[offset + 1], buffer[offset + 2], buffer[offset + 3] };
        if (mfrc522.MIFARE_Ultralight_Write(page, pageData, 4) != MFRC522::STATUS_OK) return false;
        page++;
    }
    return true;
}

bool writeNdefTextToMifareClassic(const String& text) {
    MFRC522::MIFARE_Key activeKey;
    byte activeCmd = 0;
    if (!authSector(1, activeKey, activeCmd)) return false;

    uint8_t textLen = text.length();
    uint8_t payloadLen = 1 + 2 + textLen;
    uint8_t ndefRecordLen = 1 + 1 + 1 + 1 + payloadLen;

    std::vector<uint8_t> buffer;
    buffer.reserve(48);
    buffer.push_back(0x03);
    buffer.push_back(ndefRecordLen);
    buffer.push_back(0xD1);
    buffer.push_back(0x01);
    buffer.push_back(payloadLen);
    buffer.push_back(0x54); // 'T'
    buffer.push_back(0x02);
    buffer.push_back('e');
    buffer.push_back('n');

    for (size_t i = 0; i < textLen; i++) buffer.push_back((uint8_t)text[i]);
    buffer.push_back(0xFE);
    while (buffer.size() < 48) buffer.push_back(0x00);

    for (byte b = 0; b < 3; b++) {
        byte blockAddr = 4 + b;
        byte blockData[16];
        memcpy(blockData, buffer.data() + (b * 16), 16);
        if (mfrc522.MIFARE_Write(blockAddr, blockData, 16) != MFRC522::STATUS_OK) {
            return false;
        }
    }
    return true;
}

bool wipeTag(MFRC522::PICC_Type piccType) {
    if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
        byte blankPage[4] = {0, 0, 0, 0};
        for (byte p = 4; p <= 15; p++) mfrc522.MIFARE_Ultralight_Write(p, blankPage, 4);
        return true;
    } else if (piccType == MFRC522::PICC_TYPE_MIFARE_1K || piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
        MFRC522::MIFARE_Key activeKey;
        byte activeCmd = 0;
        if (authSector(1, activeKey, activeCmd)) {
            byte blankBlock[16] = {0};
            mfrc522.MIFARE_Write(4, blankBlock, 16);
            mfrc522.MIFARE_Write(5, blankBlock, 16);
            mfrc522.MIFARE_Write(6, blankBlock, 16);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------
// HTTP ROUTES (ESPAsyncWebServer - ULTRA FAST & NON-BLOCKING)
// ---------------------------------------------------------------------------------
void setupWebRoutes() {
    // Root UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    });

    // API Status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        char vHex[10];
        snprintf(vHex, sizeof(vHex), "0x%02X", rc522Version);

        String json = "{";
        json += "\"online\":true,";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"connected\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
        json += "\"rc522\":\"" + String(vHex) + "\",";
        json += "\"card_present\":" + String(cardPresent ? "true" : "false") + ",";
        json += "\"scan_id\":" + String(scanId) + ",";
        json += "\"uptime\":" + String(millis() / 1000) + ",";
        json += "\"armed\":" + String(currentMode != MODE_IDLE ? "true" : "false") + ",";
        json += "\"armed_type\":\"" + armedType + "\",";
        
        String escArmed = armedPayload;
        escArmed.replace("\"", "\\\"");
        json += "\"armed_payload\":\"" + escArmed + "\",";

        String escAction = lastActionStatus;
        escAction.replace("\"", "\\\"");
        json += "\"last_action_status\":\"" + escAction + "\",";

        json += "\"tag\":{";
        json += "\"uid\":\"" + currentTag.uid + "\",";
        json += "\"type\":\"" + currentTag.type + "\",";
        json += "\"sak\":\"" + currentTag.sak + "\",";
        String escPayload = currentTag.payload;
        escPayload.replace("\"", "\\\"");
        escPayload.replace("\n", " ");
        json += "\"payload\":\"" + escPayload + "\"";
        json += "}";
        json += "}";

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });

    // API Dump Download
    server.on("/api/dump", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", lastDumpJson);
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });

    // Arm Endpoint
    server.on("/api/arm", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("type", true)) {
            String t = request->getParam("type", true)->value();
            String p = request->hasParam("payload", true) ? request->getParam("payload", true)->value() : "";
            armedType = t;
            armedPayload = p;
            lastActionStatus = "Idle";

            if (t == "url") currentMode = MODE_WRITE_URL;
            else if (t == "text") currentMode = MODE_WRITE_TEXT;
            else if (t == "dump" || t == "clone") currentMode = MODE_RESTORE_DUMP;
            else if (t == "wipe") currentMode = MODE_WIPE;
            else currentMode = MODE_IDLE;

            request->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing type\"}");
    });

    // Cancel Endpoint
    server.on("/api/cancel", HTTP_POST, [](AsyncWebServerRequest *request) {
        currentMode = MODE_IDLE;
        armedType = "none";
        armedPayload = "";
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // Captive Portal / Windows Probes
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Microsoft Connect Test");
    });

    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Microsoft NCSI");
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(204, "text/plain", "");
    });
}

// ---------------------------------------------------------------------------------
// ON-DEMAND WI-FI WEB PORTAL CONTROLLER
// ---------------------------------------------------------------------------------
void startWiFiPortal() {
    if (g_wifiActive) {
        Serial.printf("[WiFi] Web portal is already active at: http://%s/\n", WiFi.localIP().toString().c_str());
        return;
    }
    Serial.println("[WiFi] Starting Wi-Fi Web Portal on demand...");
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_START) {
            WiFi.setTxPower(WIFI_POWER_11dBm);
            Serial.printf("[WiFi] STA Radio Started | MAC: %s\n", WiFi.macAddress().c_str());
        } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
            Serial.printf("[WiFi] STA Connected to '%s'!\n", WiFi.SSID().c_str());
            WiFi.setTxPower(WIFI_POWER_11dBm);
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            Serial.printf("[WiFi] STA Ready: http://%s\n", WiFi.localIP().toString().c_str());
        } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            Serial.printf("[WiFi] STA Disconnected (Reason %d)\n", info.wifi_sta_disconnected.reason);
        }
    });

    WiFi.mode(WIFI_STA);
    delay(50);

    wifi_country_t country = {
        .cc = "IN",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 11,
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };
    esp_wifi_set_country(&country);

    WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS);
    WiFi.setTxPower(WIFI_POWER_11dBm);
    WiFi.setAutoReconnect(true);

    Serial.printf("[WiFi] ESP32 MAC Address: %s\n", WiFi.macAddress().c_str());
    Serial.printf("[WiFi] Connecting to '%s' (Static IP: %s) ...", sta_ssid1, staticIP.toString().c_str());
    WiFi.begin(sta_ssid1, sta_pass);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 6000) {
        delay(100);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[WiFi] Retrying '%s' ...", sta_ssid2);
        WiFi.begin(sta_ssid2, sta_pass);
        startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
            delay(100);
            Serial.print(".");
        }
        Serial.println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] SUCCESS! Connected to '%s'\n", WiFi.SSID().c_str());
        Serial.printf("[WiFi] Access IP: http://%s/\n", WiFi.localIP().toString().c_str());
        if (MDNS.begin("nfc")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("[mDNS] Domain: http://nfc.local");
        }
    } else {
        Serial.println("[WiFi] Home Wi-Fi direct connection timed out. Starting fallback AP 'NFC-Studio'...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(ap_ssid, ap_pass, 1);
        WiFi.setTxPower(WIFI_POWER_11dBm);
        Serial.printf("[AP] Backup Hotspot: http://%s/\n", WiFi.softAPIP().toString().c_str());
    }

    server.begin();
    g_wifiActive = true;
    Serial.println("[HTTP] Web Server started on port 80.");
}

void stopWiFiPortal() {
    if (!g_wifiActive) {
        Serial.println("[WiFi] Web Portal is already OFF.");
        return;
    }
    server.end();
    MDNS.end();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    g_wifiActive = false;
    s_espNowReady = false;
    Serial.println("[WiFi] Web Portal stopped. Wi-Fi radio powered OFF (BLE dedicated).");
}

// ---------------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------------
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

    Serial.begin(115200);
    delay(400);

    pinMode(PIN_LED, OUTPUT);
    ledOff(); // LED starts OFF

    Serial.println("\n\n==========================================");
    Serial.println("  ESP32-C3 NFC KEYPAD PRO                 ");
    Serial.println("==========================================");

    // 1. Initialize RC522
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, LOW);
    delay(20);
    digitalWrite(PIN_RST, HIGH);
    delay(30);

    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
    delay(10);
    mfrc522.PCD_Init();
    delay(30);

    rc522Version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    Serial.printf("[RC522] Version: 0x%02X\n", rc522Version);

    // Standard high-sensitivity RF tuning (48dB max gain for reliable reading of cards, fobs and stickers)
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
    mfrc522.PCD_AntennaOn();
    Serial.println("[RC522] Ready (Max 48dB Gain, Antenna ON)");

    // 2. Wi-Fi is OFF by default to preserve 100% 2.4 GHz bandwidth for BLE
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    setupWebRoutes();
    Serial.println("[WiFi] Radio is OFF by default (Preserves RF for BLE). Send 'WIFI_START' to launch portal.");

    // 3. Initialize BLE HID Keyboard (NimBLE 2.5.1 BleHidCombo)
    bleCombo.begin("NFC Deck", "Espressif");
    Serial.println("[BLE] Bluetooth Wireless HID Started as 'NFC Deck'");

    // 4. Initialize Autonomous Hardware Unlock from Flash NVS
    g_prefs.begin("nfc_deck", false);
    g_autoUnlockUid = g_prefs.getString("auto_uid", "27:82:D3:A9");
    g_autoUnlockPin = g_prefs.getString("auto_pin", "1911");
    // Sanitize corrupted NVS value from previous bug (e.g. UID="27" or PIN="82:D3:A9:1911")
    if (g_autoUnlockUid == "27" || g_autoUnlockUid.indexOf(':') == -1 || g_autoUnlockPin.indexOf(':') != -1) {
        g_autoUnlockUid = "27:82:D3:A9";
        g_autoUnlockPin = "1911";
        g_prefs.putString("auto_uid", g_autoUnlockUid);
        g_prefs.putString("auto_pin", g_autoUnlockPin);
    }
    g_autoUnlockEnabled = g_prefs.getBool("auto_en", true);
    g_pcIsLocked = true; // Always default to true: PC boots / powers on into the Windows Lock Screen!
    Serial.printf("[NFC] Autonomous Unlock Ready: UID=%s, PIN=%s, Enabled=%d\n",
                  g_autoUnlockUid.c_str(), g_autoUnlockPin.c_str(), g_autoUnlockEnabled ? 1 : 0);

    // 5. Initialize Wake-on-LAN (WoL) from Flash NVS
    g_wolEnabled = g_prefs.getBool("wol_en", true);
    g_wolMac = g_prefs.getString("wol_mac", "10:FF:E0:4E:5A:20");
    Serial.printf("[WOL] Wake-on-LAN Ready: Target MAC=%s, Enabled=%d\n", g_wolMac.c_str(), g_wolEnabled ? 1 : 0);
}

// ---------------------------------------------------------------------------------
// BLE KEYBOARD SMART LOCK / UNLOCK ENGINE
// ---------------------------------------------------------------------------------
void performBleLock() {
    if (!bleCombo.isConnected()) {
        Serial.println("[BLE] Note: Bluetooth not connected yet. Pair 'NFC Commander Deck' in Windows Bluetooth settings.");
        return;
    }
    Serial.println("[BLE] Locking PC (Win + L)...");
    bleCombo.sendKey(0x0F, KEY_MOD_LMETA); // 0x0F = 'L', KEY_MOD_LMETA = Win key
    delay(50);
    bleCombo.releaseAllKeys();
    g_pcIsLocked = true;
    Serial.println("[BLE] Win+L Sent successfully!");
}

void performBleUnlock(const String& pin) {
    if (!bleCombo.isConnected()) {
        Serial.println("[BLE] Note: Bluetooth not connected yet. Pair 'NFC Commander Deck' in Windows Bluetooth settings.");
        return;
    }
    Serial.printf("[BLE] Unlocking PC with PIN (%s)...\n", pin.c_str());
    bleCombo.releaseAllKeys();

    // 1. Send ESC to dismiss any 'PIN is incorrect' error dialog or prompt
    bleCombo.sendKey(0x29); // HID 0x29 = ESC
    delay(60);
    bleCombo.releaseAllKeys();

    // 2. Send UP ARROW to slide up lock screen curtain (never enters characters into PIN field)
    bleCombo.sendKey(0x52); // HID 0x52 = Up Arrow
    delay(80);
    bleCombo.sendKey(0x52); // HID 0x52 = Up Arrow
    delay(350); // Allow Windows lock screen slide-up animation to focus the PIN box

    // 3. Type PIN digits crisp and clean
    bleCombo.typeString(pin);
    delay(40);
    bleCombo.releaseAllKeys();

    // Note: Do NOT send Enter (0x28)! Windows Hello PIN automatically authenticates on the 4th digit.
    // Leaving Enter out prevents stray Enter keypresses on desktop windows after login.

    g_pcIsLocked = false;
    Serial.println("[BLE] PIN successfully sent over Bluetooth!");
}

// ---------------------------------------------------------------------------------
// WAKE-ON-LAN (WoL) ENGINE
// ---------------------------------------------------------------------------------
bool parseMacAddress(const String& macStr, uint8_t mac[6]) {
    int v[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6 ||
        sscanf(macStr.c_str(), "%x-%x-%x-%x-%x-%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
        return true;
    }
    return false;
}

bool sendWakeOnLan(const String& macStr) {
    uint8_t mac[6];
    if (!parseMacAddress(macStr, mac)) {
        Serial.printf("[WOL] Invalid MAC: %s\n", macStr.c_str());
        return false;
    }

    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(&packet[6 + i * 6], mac, 6);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        Serial.printf("[WOL] Failed to create UDP socket: errno %d\n", errno);
        return false;
    }

    // Explicitly enable SO_BROADCAST so lwIP allows broadcast transmission
    int broadcastEnable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        Serial.printf("[WOL] setsockopt SO_BROADCAST failed: errno %d\n", errno);
        close(sock);
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in destAddrUniversal;
    memset(&destAddrUniversal, 0, sizeof(destAddrUniversal));
    destAddrUniversal.sin_family = AF_INET;
    destAddrUniversal.sin_addr.s_addr = htonl(INADDR_BROADCAST); // 255.255.255.255

    struct sockaddr_in destAddrSubnet;
    memset(&destAddrSubnet, 0, sizeof(destAddrSubnet));
    destAddrSubnet.sin_family = AF_INET;
    destAddrSubnet.sin_addr.s_addr = inet_addr("192.168.1.255"); // Subnet directed broadcast

    bool ok = false;
    for (int rep = 0; rep < 5; rep++) {
        // Universal Port 9 & 7
        destAddrUniversal.sin_port = htons(9);
        int r1 = sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&destAddrUniversal, sizeof(destAddrUniversal));

        destAddrUniversal.sin_port = htons(7);
        int r2 = sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&destAddrUniversal, sizeof(destAddrUniversal));

        // Subnet Port 9 & 7
        destAddrSubnet.sin_port = htons(9);
        int r3 = sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&destAddrSubnet, sizeof(destAddrSubnet));

        destAddrSubnet.sin_port = htons(7);
        int r4 = sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&destAddrSubnet, sizeof(destAddrSubnet));

        if (r1 > 0 || r2 > 0 || r3 > 0 || r4 > 0) ok = true;
        delay(30);
    }

    close(sock);
    return ok;
}

void triggerWakeOnLanSequence() {
    Serial.printf("[WOL] Auto-Unlock card tapped while PC is asleep. Broadcasting WoL for MAC %s...\n", g_wolMac.c_str());

    // Visual feedback: double pulse onboard LED
    for (int i = 0; i < 2; i++) {
        ledOn(); delay(80); ledOff(); delay(80);
    }
    ledOn();

    // 1. Fast Wi-Fi STA connection
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
        delay(30);
        wifi_country_t country = {
            .cc = "IN",
            .schan = 1,
            .nchan = 13,
            .max_tx_power = 15,
            .policy = WIFI_COUNTRY_POLICY_MANUAL
        };
        esp_wifi_set_country(&country);
        WiFi.setTxPower(WIFI_POWER_15dBm);
        WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS);

        WiFi.begin(sta_ssid1, sta_pass);
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 3000) {
            delay(50);
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WOL] Connected to Wi-Fi. Sending Magic Packet to %s...\n", g_wolMac.c_str());
        if (sendWakeOnLan(g_wolMac)) {
            Serial.println("[WOL] Magic packet broadcast successfully!");
        } else {
            Serial.println("[WOL] Magic packet broadcast failed!");
        }
    } else {
        Serial.println("[WOL] Wi-Fi connection timed out.");
    }
    ledOff();

    // 2. Shut off Wi-Fi immediately to restore 100% RF to Bluetooth & RFID!
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    g_wifiActive = false;
    s_espNowReady = false;
    Serial.println("[WOL] Wi-Fi powered down. System ready.");
}

// ---------------------------------------------------------------------------------
// LOOP (NFC SCANNING ENGINE, SERIAL DISPATCHER & WIFI WATCHDOG)
// ---------------------------------------------------------------------------------
unsigned long lastNfcCheck = 0;
unsigned long lastWifiRetry = 0;

bool isCardInField() {
    byte bufferATQA[2];
    byte bufferSize = sizeof(bufferATQA);
    mfrc522.PCD_WriteRegister(mfrc522.TxModeReg, 0x00);
    mfrc522.PCD_WriteRegister(mfrc522.RxModeReg, 0x00);
    mfrc522.PCD_WriteRegister(mfrc522.ModWidthReg, 0x26);
    
    // 1. Try standard RequestA (finds cards in IDLE state)
    MFRC522::StatusCode status = mfrc522.PICC_RequestA(bufferATQA, &bufferSize);
    if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
        return true;
    }
    
    // 2. Fallback to WakeupA (wakes cards that were halted or sleeping)
    bufferSize = sizeof(bufferATQA);
    status = mfrc522.PICC_WakeupA(bufferATQA, &bufferSize);
    return (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION);
}

void loop() {
    unsigned long now = millis();

    // Background Wi-Fi watchdog: retry direct STA connection only if web portal was explicitly started
    if (g_wifiActive && (now - lastWifiRetry > 15000)) {
        lastWifiRetry = now;
        if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
            Serial.printf("[WiFi] Watchdog: Reconnecting (11dBm)...\n");
            WiFi.setTxPower(WIFI_POWER_11dBm);
            WiFi.reconnect();
        }
    }

    // Poll NFC every 20ms for instant sub-millisecond tag detection
    if (now - lastNfcCheck < 20) {
        return;
    }
    lastNfcCheck = now;

    if (!cardPresent) {
        // Look for newly presented card
        if (isCardInField() && mfrc522.PICC_ReadCardSerial()) {
            cardPresent = true;
            consecutiveMisses = 0;

            // Format UID
            String uidStr = "";
            for (byte i = 0; i < mfrc522.uid.size; i++) {
                if (i > 0) uidStr += ":";
                if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
                uidStr += String(mfrc522.uid.uidByte[i], HEX);
            }
            uidStr.toUpperCase();

            MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
            String typeName = mfrc522.PICC_GetTypeName(piccType);

            char sakBuf[8];
            snprintf(sakBuf, sizeof(sakBuf), "%02X", mfrc522.uid.sak);

            currentTag.uid = uidStr;
            currentTag.type = typeName;
            currentTag.sak = String(sakBuf);

            Serial.printf("[NFC] Card Detected! UID: %s | Type: %s\n", uidStr.c_str(), typeName.c_str());
            broadcastEspNowCard(uidStr, typeName, mfrc522.uid.uidByte, mfrc522.uid.size, scanId + 1);

            // -------------------------------------------------------------
            // ZERO-LATENCY HARDWARE SMART LOCK / UNLOCK / WOL TRIGGER
            // Check UID FIRST for instant response (no waiting for sector reads!)
            // -------------------------------------------------------------
            static unsigned long s_lastHardwareTriggerMillis = 0;
            unsigned long nowMillis = millis();

            if (g_autoUnlockEnabled && uidStr == g_autoUnlockUid) {
                if (nowMillis - s_lastHardwareTriggerMillis >= 1500) {
                    s_lastHardwareTriggerMillis = nowMillis;
                    if (bleCombo.isConnected()) {
                        if (g_pcIsLocked) {
                            Serial.printf("[NFC] Auto-Unlock Card (%s) Detected (PC is LOCKED) -> Typing PIN (%s) over BLE...\n",
                                          uidStr.c_str(), g_autoUnlockPin.c_str());
                            performBleUnlock(g_autoUnlockPin);
                        } else {
                            Serial.printf("[NFC] Auto-Unlock Card (%s) Detected (PC is UNLOCKED) -> Locking PC via Win+L...\n",
                                          uidStr.c_str());
                            performBleLock();
                        }
                    } else {
                        Serial.println("[NFC] Auto-Unlock Card detected, but Bluetooth is not connected yet.");
                        // Only wake if PC is locked/asleep - never wake an unlocked PC!
                        if (g_pcIsLocked && g_wolEnabled && g_wolMac.length() >= 12) {
                            triggerWakeOnLanSequence();
                        }
                    }
                } else {
                    Serial.println("[NFC] Auto-Unlock Card cooldown active (ignoring bounce/double-read).");
                }
            }

            if (currentMode != MODE_IDLE) {
                // ARMED WRITE / RESTORE / WIPE MODE
                ledOn();

                bool success = false;
                if (currentMode == MODE_WRITE_URL) {
                    if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) success = writeNdefUrlToNtag(armedPayload);
                    else success = writeNdefUrlToMifareClassic(armedPayload);
                    lastActionStatus = success ? "Successfully wrote URL!" : "Failed to write URL!";
                } else if (currentMode == MODE_WRITE_TEXT) {
                    if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) success = writeNdefTextToNtag(armedPayload);
                    else success = writeNdefTextToMifareClassic(armedPayload);
                    lastActionStatus = success ? "Successfully wrote Text!" : "Failed to write Text!";
                } else if (currentMode == MODE_RESTORE_DUMP) {
                    success = restoreDumpToTag(piccType, armedPayload);
                    lastActionStatus = success ? "Successfully restored dump!" : "Failed to restore dump!";
                } else if (currentMode == MODE_WIPE) {
                    success = wipeTag(piccType);
                    lastActionStatus = success ? "Successfully wiped tag!" : "Failed to wipe tag!";
                }

                if (success) {
                    currentTag.payload = (currentMode == MODE_WIPE) ? "(Wiped / Blank)" : 
                                         (currentMode == MODE_RESTORE_DUMP) ? "(Dump Restored)" : armedPayload;
                }

                // Immediately turn OFF LED when write finishes
                ledOff();
                Serial.printf("[NFC] Payload: %s\n", currentTag.payload.c_str());
                Serial.printf("[NFC] Write Status: %s\n", lastActionStatus.c_str());
                currentMode = MODE_IDLE;
                armedType = "none";
                armedPayload = "";
                scanId++;
            } else {
                // READ MODE: Turn ON LED while card is held on sensor
                ledOn();

                if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
                    readNtag(currentTag.payload, uidStr, typeName, String(sakBuf));
                } else if (piccType == MFRC522::PICC_TYPE_MIFARE_1K || piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
                    readMifareClassic(currentTag.payload, uidStr, typeName, String(sakBuf));
                } else {
                    currentTag.payload = "(Contactless Card / UID Only)";
                }

                scanId++;
                Serial.printf("[NFC] Payload: %s\n", currentTag.payload.c_str());
                broadcastEspNowCard(uidStr, typeName, mfrc522.uid.uidByte, mfrc522.uid.size, scanId);
            }

            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();
        }
    } else {
        // Card was previously detected; check if it is still resting on the sensor
        byte atqa[2];
        byte atqaSz = sizeof(atqa);
        mfrc522.PCD_WriteRegister(mfrc522.TxModeReg, 0x00);
        mfrc522.PCD_WriteRegister(mfrc522.RxModeReg, 0x00);
        mfrc522.PCD_WriteRegister(mfrc522.ModWidthReg, 0x26);
        MFRC522::StatusCode st = mfrc522.PICC_WakeupA(atqa, &atqaSz);

        if (st == MFRC522::STATUS_OK || st == MFRC522::STATUS_COLLISION) {
            consecutiveMisses = 0;
            if (currentMode == MODE_IDLE) {
                ledOn(); // Maintain solid LED while tag is on reader
            }
            mfrc522.PICC_HaltA();
        } else {
            consecutiveMisses++;
            if (consecutiveMisses >= 2) {
                cardPresent = false;
                ledOff(); // Turn OFF LED when card is lifted
                mfrc522.PCD_StopCrypto1();
                Serial.println("[NFC] Card Removed.");
            }
        }
    }

    // Process incoming Serial commands from PC NFC Command Deck
    while (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "PC_STATE:LOCKED") {
            g_pcIsLocked = true;
            Serial.println("[STATE] Synced PC State -> LOCKED");
        } else if (cmd == "PC_STATE:UNLOCKED") {
            g_pcIsLocked = false;
            Serial.println("[STATE] Synced PC State -> UNLOCKED");
        } else if (cmd.startsWith("ARM:url:")) {
            currentMode = MODE_WRITE_URL;
            armedType = "url";
            armedPayload = cmd.substring(8);
            Serial.printf("[CMD] Armed for URL: %s\n", armedPayload.c_str());
        } else if (cmd.startsWith("ARM:text:")) {
            currentMode = MODE_WRITE_TEXT;
            armedType = "text";
            armedPayload = cmd.substring(9);
            Serial.printf("[CMD] Armed for Text: %s\n", armedPayload.c_str());
        } else if (cmd.startsWith("ARM:wipe")) {
            currentMode = MODE_WIPE;
            armedType = "wipe";
            armedPayload = "";
            Serial.println("[CMD] Armed for Wipe");
        } else if (cmd == "DISARM") {
            currentMode = MODE_IDLE;
            armedType = "none";
            armedPayload = "";
            Serial.println("[CMD] Disarmed.");
        } else if (cmd == "PING" || cmd == "STATUS") {
            byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
            Serial.printf("[DIAG] RC522=0x%02X | CARD=%s | BLE=%s | WIFI=%s | UPTIME=%lus\n",
                          v, cardPresent ? "PRESENT" : "NONE",
                          bleCombo.isConnected() ? "CONNECTED" : "ADVERTISING",
                          g_wifiActive ? (WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "AP") : "OFF",
                          millis() / 1000);
        } else if (cmd == "WIFI_START" || cmd == "WIFI_ON") {
            startWiFiPortal();
        } else if (cmd == "WIFI_STOP" || cmd == "WIFI_OFF") {
            stopWiFiPortal();
        } else if (cmd == "WIFI_STATUS") {
            Serial.printf("[WIFI] Status: %s | IP: %s\n",
                          g_wifiActive ? "ACTIVE" : "OFF",
                          g_wifiActive ? (WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "AP") : "NONE");
        } else if (cmd == "BLE_LOCK" || cmd.startsWith("BLE_LOCK")) {
            performBleLock();
        } else if (cmd.startsWith("BLE_UNLOCK:")) {
            String pin = cmd.substring(11);
            pin.trim();
            performBleUnlock(pin);
        } else if (cmd.startsWith("SET_LOCK_STATE:")) {
            int state = cmd.substring(15).toInt();
            g_pcIsLocked = (state != 0);
            Serial.printf("[STATE] Synced PC Lock State: %s\n", g_pcIsLocked ? "LOCKED" : "UNLOCKED");
        } else if (cmd.startsWith("SET_AUTO_UNLOCK:")) {
            String rest = cmd.substring(16);
            int colon = rest.lastIndexOf(':');
            if (colon != -1) {
                g_autoUnlockUid = rest.substring(0, colon);
                g_autoUnlockPin = rest.substring(colon + 1);
                g_autoUnlockUid.trim();
                g_autoUnlockPin.trim();
                g_autoUnlockEnabled = true;
                g_prefs.putString("auto_uid", g_autoUnlockUid);
                g_prefs.putString("auto_pin", g_autoUnlockPin);
                g_prefs.putBool("auto_en", true);
                Serial.printf("[NFC] Saved Auto-Unlock to NVS: UID=%s, PIN=%s\n", g_autoUnlockUid.c_str(), g_autoUnlockPin.c_str());
            }
        } else if (cmd == "CLEAR_AUTO_UNLOCK") {
            g_autoUnlockEnabled = false;
            g_prefs.putBool("auto_en", false);
            Serial.println("[NFC] Auto-Unlock Disabled in NVS.");
        } else if (cmd == "GET_AUTO_UNLOCK") {
            Serial.printf("[NFC] Auto-Unlock Config: UID=%s | PIN=%s | Enabled=%d | LockState=%s\n",
                          g_autoUnlockUid.c_str(), g_autoUnlockPin.c_str(), g_autoUnlockEnabled ? 1 : 0, g_pcIsLocked ? "LOCKED" : "UNLOCKED");
        } else if (cmd == "BLE_RESET" || cmd == "BLE_DISCONNECT") {
            NimBLEDevice::deleteAllBonds();
            NimBLEServer* pServer = NimBLEDevice::getServer();
            if (pServer) {
                std::vector<uint16_t> peers = pServer->getPeerDevices();
                for (auto handle : peers) {
                    pServer->disconnect(handle);
                }
            }
            NimBLEDevice::startAdvertising();
            Serial.println("[BLE] Reset all bonds, disconnected clients, and restarted advertising.");
        } else if (cmd.startsWith("BLE_TYPE:")) {
            String text = cmd.substring(9);
            if (bleCombo.isConnected()) {
                bleCombo.typeString(text);
            }
        } else if (cmd.startsWith("SET_WOL:")) {
            // Format: SET_WOL:<1|0>:<mac> or SET_WOL:<1|0>
            String rest = cmd.substring(8);
            int colon = rest.indexOf(':');
            if (colon != -1) {
                g_wolEnabled = (rest.substring(0, colon).toInt() != 0);
                String mac = rest.substring(colon + 1);
                mac.trim();
                if (mac.length() >= 12) {
                    g_wolMac = mac;
                    g_prefs.putString("wol_mac", g_wolMac);
                }
            } else {
                g_wolEnabled = (rest.toInt() != 0);
            }
            g_prefs.putBool("wol_en", g_wolEnabled);
            Serial.printf("[WOL] Saved Config to NVS: Enabled=%d, Target MAC=%s\n", g_wolEnabled ? 1 : 0, g_wolMac.c_str());
        } else if (cmd == "GET_WOL") {
            Serial.printf("[WOL] Config: Enabled=%d | MAC=%s\n", g_wolEnabled ? 1 : 0, g_wolMac.c_str());
        } else if (cmd == "TEST_WOL" || cmd == "SEND_WOL") {
            Serial.println("[WOL] Manual WoL Test Triggered via Serial.");
            triggerWakeOnLanSequence();
        }
    }
}

