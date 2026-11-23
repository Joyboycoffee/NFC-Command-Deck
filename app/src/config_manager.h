#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "action_executor.h"

struct AppConfig {
    std::string connectionMode = "auto"; // "auto", "serial", "wifi"
    std::string comPort = "COM3";
    std::string wifiIp = "192.168.1.100";
    int baudRate = 115200;
    bool soundEnabled = true;
    bool notificationsEnabled = false; // Default OFF so user is not spammed by balloon notifications
    bool minimizeToTray = true;
    bool autoOpenUrls = false; // Default OFF so unmapped URLs don't hijack browser
    bool masterAutomationsEnabled = true; // Global Master Switch for all automations
    bool wolEnabled = false; // Wake-on-LAN PC Power On
    std::string wolMac = "00:11:22:33:44:55"; // Target PC MAC Address
    std::vector<ActionRule> rules;
};

class ConfigManager {
public:
    static std::string escapeJson(const std::string& s) {
        std::ostringstream o;
        for (char c : s) {
            if (c == '"') o << "\\\"";
            else if (c == '\\') o << "\\\\";
            else if (c == '\b') o << "\\b";
            else if (c == '\f') o << "\\f";
            else if (c == '\n') o << "\\n";
            else if (c == '\r') o << "\\r";
            else if (c == '\t') o << "\\t";
            else if ((unsigned char)c < ' ') o << "\\u" << std::hex << (int)c;
            else o << c;
        }
        return o.str();
    }

    static std::string actionTypeToString(ActionType t) {
        switch (t) {
            case ACTION_APP: return "app";
            case ACTION_STEAM: return "steam";
            case ACTION_SPOTIFY: return "spotify";
            case ACTION_MEDIA: return "media";
            case ACTION_COMMAND: return "command";
            case ACTION_URL: return "url";
            case ACTION_HOTKEY: return "hotkey";
            case ACTION_SYSTEM: return "system";
            default: return "app";
        }
    }

    static ActionType stringToActionType(const std::string& s) {
        if (s == "steam") return ACTION_STEAM;
        if (s == "spotify") return ACTION_SPOTIFY;
        if (s == "media") return ACTION_MEDIA;
        if (s == "command") return ACTION_COMMAND;
        if (s == "url") return ACTION_URL;
        if (s == "hotkey") return ACTION_HOTKEY;
        if (s == "system") return ACTION_SYSTEM;
        return ACTION_APP;
    }

    static bool save(const std::string& filepath, const AppConfig& cfg) {
        std::ofstream f(filepath.c_str());
        if (!f.is_open()) return false;

        f << "{\n";
        f << "  \"connectionMode\": \"" << escapeJson(cfg.connectionMode) << "\",\n";
        f << "  \"comPort\": \"" << escapeJson(cfg.comPort) << "\",\n";
        f << "  \"wifiIp\": \"" << escapeJson(cfg.wifiIp) << "\",\n";
        f << "  \"baudRate\": " << cfg.baudRate << ",\n";
        f << "  \"soundEnabled\": " << (cfg.soundEnabled ? "true" : "false") << ",\n";
        f << "  \"notificationsEnabled\": " << (cfg.notificationsEnabled ? "true" : "false") << ",\n";
        f << "  \"minimizeToTray\": " << (cfg.minimizeToTray ? "true" : "false") << ",\n";
        f << "  \"autoOpenUrls\": " << (cfg.autoOpenUrls ? "true" : "false") << ",\n";
        f << "  \"masterAutomationsEnabled\": " << (cfg.masterAutomationsEnabled ? "true" : "false") << ",\n";
        f << "  \"wolEnabled\": " << (cfg.wolEnabled ? "true" : "false") << ",\n";
        f << "  \"wolMac\": \"" << escapeJson(cfg.wolMac) << "\",\n";
        f << "  \"rules\": [\n";

        for (size_t i = 0; i < cfg.rules.size(); i++) {
            const ActionRule& r = cfg.rules[i];
            f << "    {\n";
            f << "      \"id\": \"" << escapeJson(r.id) << "\",\n";
            f << "      \"name\": \"" << escapeJson(r.name) << "\",\n";
            f << "      \"tagUid\": \"" << escapeJson(r.tagUid) << "\",\n";
            f << "      \"tagPayload\": \"" << escapeJson(r.tagPayload) << "\",\n";
            f << "      \"matchMode\": \"" << escapeJson(r.matchMode.empty() ? "uid" : r.matchMode) << "\",\n";
            f << "      \"type\": \"" << actionTypeToString(r.type) << "\",\n";
            f << "      \"target\": \"" << escapeJson(r.target) << "\",\n";
            f << "      \"args\": \"" << escapeJson(r.args) << "\",\n";
            f << "      \"silent\": " << (r.silent ? "true" : "false") << ",\n";
            f << "      \"enabled\": " << (r.enabled ? "true" : "false") << ",\n";
            f << "      \"triggerCount\": " << r.triggerCount << ",\n";
            f << "      \"lastTriggerTime\": \"" << escapeJson(r.lastTriggerTime) << "\",\n";
            f << "      \"sound\": \"" << escapeJson(r.sound.empty() ? "Default Glass Chime" : r.sound) << "\"\n";
            f << "    }" << (i + 1 < cfg.rules.size() ? "," : "") << "\n";
        }

        f << "  ]\n";
        f << "}\n";
        f.close();
        return true;
    }

    static bool load(const std::string& filepath, AppConfig& outCfg) {
        std::ifstream f(filepath.c_str());
        if (!f.is_open()) {
            // Populate defaults
            createDefaultConfig(outCfg);
            save(filepath, outCfg);
            return true;
        }

        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        outCfg.connectionMode = extractString(content, "connectionMode", "auto");
        outCfg.comPort = extractString(content, "comPort", "COM3");
        outCfg.wifiIp = extractString(content, "wifiIp", "192.168.1.100");
        outCfg.baudRate = (int)extractNumber(content, "baudRate", 115200);
        outCfg.soundEnabled = extractBool(content, "soundEnabled", true);
        outCfg.notificationsEnabled = extractBool(content, "notificationsEnabled", false);
        outCfg.minimizeToTray = extractBool(content, "minimizeToTray", true);
        outCfg.autoOpenUrls = extractBool(content, "autoOpenUrls", false);
        outCfg.masterAutomationsEnabled = extractBool(content, "masterAutomationsEnabled", true);
        outCfg.wolEnabled = extractBool(content, "wolEnabled", false);
        outCfg.wolMac = extractString(content, "wolMac", "00:11:22:33:44:55");

        outCfg.rules.clear();
        size_t rulesPos = content.find("\"rules\"");
        if (rulesPos != std::string::npos) {
            size_t startBracket = content.find('[', rulesPos);
            size_t endBracket = content.rfind(']');
            if (startBracket != std::string::npos && endBracket != std::string::npos && endBracket > startBracket) {
                std::string rulesArrayStr = content.substr(startBracket + 1, endBracket - startBracket - 1);
                size_t objStart = 0;
                while ((objStart = rulesArrayStr.find('{', objStart)) != std::string::npos) {
                    size_t objEnd = rulesArrayStr.find('}', objStart);
                    if (objEnd == std::string::npos) break;

                    std::string objStr = rulesArrayStr.substr(objStart, objEnd - objStart + 1);
                    ActionRule r;
                    r.id = extractString(objStr, "id", "rule_" + to_str(outCfg.rules.size() + 1));
                    r.name = extractString(objStr, "name", "Unnamed Action");
                    r.tagUid = extractString(objStr, "tagUid", "");
                    r.tagPayload = extractString(objStr, "tagPayload", "");
                    r.matchMode = extractString(objStr, "matchMode", r.tagUid.empty() && !r.tagPayload.empty() ? "payload" : "uid");
                    r.type = stringToActionType(extractString(objStr, "type", "app"));
                    r.target = extractString(objStr, "target", "");
                    r.args = extractString(objStr, "args", "");
                    r.silent = extractBool(objStr, "silent", true);
                    r.enabled = extractBool(objStr, "enabled", true);
                    r.triggerCount = (int)extractNumber(objStr, "triggerCount", 0);
                    r.lastTriggerTime = extractString(objStr, "lastTriggerTime", "Never");
                    r.sound = extractString(objStr, "sound", "Default Glass Chime");

                    if (!r.name.empty()) {
                        outCfg.rules.push_back(r);
                    }
                    objStart = objEnd + 1;
                }
            }
        }

        return true;
    }

private:
    static void createDefaultConfig(AppConfig& cfg) {
        cfg.connectionMode = "auto";
        cfg.comPort = "COM3";
        cfg.wifiIp = "192.168.1.100";
        cfg.baudRate = 115200;
        cfg.soundEnabled = true;
        cfg.notificationsEnabled = false;
        cfg.minimizeToTray = true;
        cfg.autoOpenUrls = false;
        cfg.wolEnabled = false;
        cfg.wolMac = "00:11:22:33:44:55";
        cfg.rules.clear();
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
        
        std::string raw = json.substr(q1 + 1, q2 - q1 - 1);
        std::string res = "";
        for (size_t i = 0; i < raw.length(); i++) {
            if (raw[i] == '\\' && i + 1 < raw.length()) {
                if (raw[i+1] == '"') { res += '"'; i++; continue; }
                if (raw[i+1] == '\\') { res += '\\'; i++; continue; }
                if (raw[i+1] == 'n') { res += '\n'; i++; continue; }
            }
            res += raw[i];
        }
        return res;
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

    static bool extractBool(const std::string& json, const std::string& key, bool defaultVal) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return defaultVal;
        size_t colon = json.find(':', pos + pattern.length());
        if (colon == std::string::npos) return defaultVal;
        if (json.find("true", colon) < json.find("false", colon) && json.find("true", colon) != std::string::npos) {
            return true;
        }
        if (json.find("false", colon) != std::string::npos) {
            return false;
        }
        return defaultVal;
    }
};
