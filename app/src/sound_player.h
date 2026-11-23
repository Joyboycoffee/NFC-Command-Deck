#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <fstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class SoundPlayer {
private:
    static std::vector<char> packWav(const std::vector<short>& pcm, int sampleRate = 44100) {
        int dataSize = (int)pcm.size() * sizeof(short);
        std::vector<char> wav(44 + dataSize);
        char* p = wav.data();

        memcpy(p, "RIFF", 4); p += 4;
        *(int*)p = 36 + dataSize; p += 4;
        memcpy(p, "WAVEfmt ", 8); p += 8;
        *(int*)p = 16; p += 4;
        *(short*)p = 1; p += 2;
        *(short*)p = 1; p += 2;
        *(int*)p = sampleRate; p += 4;
        *(int*)p = sampleRate * 2; p += 4;
        *(short*)p = 2; p += 2;
        *(short*)p = 16; p += 2;
        memcpy(p, "data", 4); p += 4;
        *(int*)p = dataSize; p += 4;
        memcpy(p, pcm.data(), dataSize);

        return wav;
    }

    static void playMemoryWav(const std::vector<char>& wav) {
        if (wav.empty()) return;
        PlaySoundA((LPCSTR)wav.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }

    static std::vector<char> generateAcousticChime(double f1, double f2, int durMs, double vol, double decayRate) {
        int sampleRate = 44100;
        int totalSamples = (int)(sampleRate * (durMs / 1000.0));
        std::vector<short> pcm(totalSamples);
        int attackSamples = (int)(sampleRate * 0.003);

        for (int i = 0; i < totalSamples; i++) {
            double t = (double)i / sampleRate;
            double attack = (i < attackSamples) ? (0.5 * (1.0 - cos(M_PI * i / attackSamples))) : 1.0;
            double env = attack * exp(-t * decayRate);

            double s = sin(2.0 * M_PI * f1 * t)
                     + 0.38 * sin(2.0 * M_PI * f2 * t)
                     + 0.16 * sin(4.0 * M_PI * f1 * t)
                     + 0.04 * sin(6.0 * M_PI * f1 * t);

            double val = s * (vol * 19000.0) * env;
            if (val > 32767.0) val = 32767.0;
            if (val < -32768.0) val = -32768.0;
            pcm[i] = (short)val;
        }

        return packWav(pcm, sampleRate);
    }

    static std::vector<char> generateTactileTap(int durMs, double vol) {
        int sampleRate = 44100;
        int totalSamples = (int)(sampleRate * (durMs / 1000.0));
        std::vector<short> pcm(totalSamples);

        for (int i = 0; i < totalSamples; i++) {
            double t = (double)i / sampleRate;
            double freq = 75.0 + 245.0 * exp(-t * 220.0);
            double phase = 2.0 * M_PI * freq * t;
            double env = exp(-t * 180.0);

            double val = sin(phase) * (vol * 22000.0) * env;
            if (val > 32767.0) val = 32767.0;
            if (val < -32768.0) val = -32768.0;
            pcm[i] = (short)val;
        }

        return packWav(pcm, sampleRate);
    }

    static std::vector<char> generateMutedBump(int durMs, double vol) {
        int sampleRate = 44100;
        int totalSamples = (int)(sampleRate * (durMs / 1000.0));
        std::vector<short> pcm(totalSamples);
        int half = totalSamples / 2;

        for (int i = 0; i < totalSamples; i++) {
            double t = (double)i / sampleRate;
            double localT = (i < half) ? t : ((double)(i - half) / sampleRate);
            double freq = (i < half) ? 140.0 : 105.0;
            double env = exp(-localT * 45.0);

            double val = sin(2.0 * M_PI * freq * localT) * (vol * 20000.0) * env;
            if (val > 32767.0) val = 32767.0;
            if (val < -32768.0) val = -32768.0;
            pcm[i] = (short)val;
        }

        return packWav(pcm, sampleRate);
    }

public:
    static void playTap() {
        static std::vector<char> tapWav = generateTactileTap(18, 0.30);
        playMemoryWav(tapWav);
    }

    static void playChime() {
        static std::vector<char> chimeWav = generateAcousticChime(880.0, 1318.5, 120, 0.35, 16.0);
        playMemoryWav(chimeWav);
    }

    static void playActionSuccess() {
        static std::vector<char> successWav = generateAcousticChime(1046.5, 1567.98, 120, 0.35, 18.0);
        playMemoryWav(successWav);
    }

    static void playError() {
        static std::vector<char> errorWav = generateMutedBump(130, 0.32);
        playMemoryWav(errorWav);
    }

    // Plays a specific sound effect by name or filename from the sounds/ folder
    static void playSound(const std::string& soundName, const std::string& exeDir) {
        if (soundName == "None (Silent)" || soundName == "none" || soundName == "None") {
            return;
        }

        std::string targetFile = soundName;
        if (targetFile.empty() || targetFile == "Default Glass Chime" || targetFile == "default") {
            targetFile = "03_glass_ping.wav";
        }

        // Try direct file path first, then inside exeDir/sounds/ folder
        std::string targetPath = targetFile;
        DWORD attr = GetFileAttributesA(targetPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            targetPath = exeDir + "\\sounds\\" + targetFile;
            attr = GetFileAttributesA(targetPath.c_str());
        }

        // Diagnostic log file
        std::ofstream dbg(exeDir + "\\sound_debug.log", std::ios::app);
        if (dbg.is_open()) {
            dbg << "playSound: [" << soundName << "] -> [" << targetPath << "] attr: " << attr << "\n";
        }

        if (attr != INVALID_FILE_ATTRIBUTES) {
            static char s_soundPathBuf[MAX_PATH];
            strncpy(s_soundPathBuf, targetPath.c_str(), MAX_PATH - 1);
            s_soundPathBuf[MAX_PATH - 1] = 0;
            BOOL ok = PlaySoundA(s_soundPathBuf, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            if (dbg.is_open()) {
                dbg << "  PlaySoundA result: " << ok << "\n";
                dbg.close();
            }
            return;
        }

        if (dbg.is_open()) {
            dbg << "  File not found! Falling back to playActionSuccess\n";
            dbg.close();
        }
        // Fallback to built-in chime if file loading failed
        playActionSuccess();
    }

    // Scans exeDir\sounds\ and returns all available .wav files + defaults
    static std::vector<std::string> getAvailableSounds(const std::string& exeDir) {
        std::vector<std::string> list;
        list.push_back("Default Glass Chime");
        list.push_back("None (Silent)");

        std::string pattern = exeDir + "\\sounds\\*.wav";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    list.push_back(fd.cFileName);
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        return list;
    }
};
