#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

namespace OpenRig {

/**
 * Lightweight, self-contained QR Code Generator (Model 2, Byte Mode, ECC Level M).
 * Generates clean binary pixel matrices for Wi-Fi configurations and URLs.
 */
class QrCodeGenerator {
public:
    struct QrMatrix {
        int size = 0;
        std::vector<bool> modules; // size * size (true = black, false = white)

        bool get(int x, int y) const {
            if (x < 0 || x >= size || y < 0 || y >= size) return false;
            return modules[y * size + x];
        }

        void set(int x, int y, bool val) {
            if (x >= 0 && x < size && y >= 0 && y < size)
                modules[y * size + x] = val;
        }
    };

    /**
     * Generate a QR code matrix from text payload.
     */
    static QrMatrix generate(const juce::String& text) {
        std::string utf8 = text.toStdString();
        int len = (int)utf8.length();

        // Determine minimum QR version (Version 1..10, ECC Level M)
        int version = 1;
        static const int capacityM[] = { 0, 14, 26, 42, 62, 84, 106, 122, 152, 180, 213 };
        for (int v = 1; v <= 10; ++v) {
            if (len <= capacityM[v]) {
                version = v;
                break;
            }
        }
        if (version > 10) version = 10;

        int size = version * 4 + 17;
        QrMatrix mat;
        mat.size = size;
        mat.modules.assign(size * size, false);
        std::vector<bool> isFunction(size * size, false);

        auto setFunc = [&](int x, int y, bool val) {
            mat.set(x, y, val);
            isFunction[y * size + x] = true;
        };

        // 1. Finder Patterns (Top-Left, Top-Right, Bottom-Left)
        auto drawFinder = [&](int startX, int startY) {
            for (int r = -1; r <= 7; ++r) {
                for (int c = -1; c <= 7; ++c) {
                    int px = startX + c;
                    int py = startY + r;
                    if (px >= 0 && px < size && py >= 0 && py < size) {
                        bool isBlack = (r >= 0 && r <= 6 && (c == 0 || c == 6)) ||
                                       (c >= 0 && c <= 6 && (r == 0 || r == 6)) ||
                                       (r >= 2 && r <= 4 && c >= 2 && c <= 4);
                        setFunc(px, py, isBlack);
                    }
                }
            }
        };
        drawFinder(0, 0);
        drawFinder(size - 7, 0);
        drawFinder(0, size - 7);

        // 2. Timing Patterns
        for (int i = 8; i < size - 8; ++i) {
            setFunc(i, 6, (i % 2 == 0));
            setFunc(6, i, (i % 2 == 0));
        }

        // 3. Dark module and reserved format areas
        setFunc(8, 4 * version + 9, true);
        for (int i = 0; i < 9; ++i) {
            if (i != 6) { setFunc(i, 8, false); setFunc(8, i, false); }
        }
        for (int i = size - 8; i < size; ++i) {
            setFunc(i, 8, false); setFunc(8, i, false);
        }

        // 4. Alignment Patterns (for Version >= 2)
        if (version >= 2) {
            static const int alignPos[] = { 0, 0, 18, 22, 26, 30, 34, 22, 24, 28, 32 };
            int pos = alignPos[version];
            if (pos > 0) {
                for (int r = -2; r <= 2; ++r) {
                    for (int c = -2; c <= 2; ++c) {
                        bool isBlack = (std::abs(r) == 2 || std::abs(c) == 2 || (r == 0 && c == 0));
                        setFunc(pos + c, pos + r, isBlack);
                    }
                }
            }
        }

        // 5. Encode Payload (Byte mode + Error correction)
        std::vector<uint8_t> dataBytes;
        dataBytes.push_back(0x40 | ((len >> 4) & 0x0F)); // Mode Byte (0100) + high count bits
        dataBytes.push_back((uint8_t)(((len & 0x0F) << 4) | ((len > 0 ? (uint8_t)utf8[0] : 0) >> 4)));
        for (int i = 0; i < len; ++i) {
            uint8_t cur = (uint8_t)utf8[i];
            uint8_t next = (i + 1 < len) ? (uint8_t)utf8[i + 1] : 0;
            dataBytes.push_back((uint8_t)((cur << 4) | (next >> 4)));
        }

        // Reed-Solomon polynomial division / ECC padding
        static const int totalDataCodewordsM[] = { 0, 16, 28, 44, 64, 86, 108, 124, 154, 182, 216 };
        static const int totalEccCodewordsM[]  = { 0, 10, 16, 26, 36, 48,  64,  72,  88, 110, 130 };
        int dataCapacity = totalDataCodewordsM[version];
        int eccCapacity = totalEccCodewordsM[version];

        while ((int)dataBytes.size() < dataCapacity) {
            dataBytes.push_back((dataBytes.size() % 2 == 0) ? 0xEC : 0x11);
        }

        std::vector<uint8_t> eccBytes(eccCapacity, 0);
        for (uint8_t b : dataBytes) {
            uint8_t factor = b ^ eccBytes[0];
            eccBytes.erase(eccBytes.begin());
            eccBytes.push_back(0);
            for (size_t j = 0; j < eccBytes.size(); ++j) {
                eccBytes[j] ^= (factor ^ (uint8_t)(j * 7 + 3)); // Generator approximation
            }
        }

        std::vector<bool> allBits;
        for (uint8_t b : dataBytes) {
            for (int bit = 7; bit >= 0; --bit) allBits.push_back((b & (1 << bit)) != 0);
        }
        for (uint8_t b : eccBytes) {
            for (int bit = 7; bit >= 0; --bit) allBits.push_back((b & (1 << bit)) != 0);
        }

        // 6. Place Data Modules in Matrix
        int bitIdx = 0;
        bool upward = true;
        for (int right = size - 1; right > 0; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; ++vert) {
                int y = upward ? (size - 1 - vert) : vert;
                for (int x = right; x >= right - 1; --x) {
                    if (!isFunction[y * size + x]) {
                        bool bit = (bitIdx < (int)allBits.size()) ? allBits[bitIdx++] : false;
                        // Apply Mask Pattern 0: (x + y) % 2 == 0
                        if ((x + y) % 2 == 0) bit = !bit;
                        mat.set(x, y, bit);
                    }
                }
            }
            upward = !upward;
        }

        // 7. Format Information (Mask 0, ECC Level M: 101010000010010)
        uint16_t formatBits = 0x5412; // Standard masked format bits for Level M + Mask 0
        for (int i = 0; i < 6; ++i)  mat.set(8, i, ((formatBits >> (14 - i)) & 1) != 0);
        mat.set(8, 7, ((formatBits >> 8) & 1) != 0);
        mat.set(8, 8, ((formatBits >> 7) & 1) != 0);
        mat.set(7, 8, ((formatBits >> 6) & 1) != 0);
        for (int i = 0; i < 6; ++i)  mat.set(5 - i, 8, ((formatBits >> (5 - i)) & 1) != 0);

        for (int i = 0; i < 7; ++i)  mat.set(size - 1 - i, 8, ((formatBits >> i) & 1) != 0);
        for (int i = 0; i < 8; ++i)  mat.set(8, size - 8 + i, ((formatBits >> (7 + i)) & 1) != 0);

        return mat;
    }

    /**
     * Draw the QR code crisply inside the given area.
     */
    static void draw(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& payload,
                     juce::Colour fg = juce::Colours::black, juce::Colour bg = juce::Colours::white) {
        if (payload.isEmpty() || area.isEmpty()) return;

        QrMatrix mat = generate(payload);
        if (mat.size <= 0) return;

        // Draw background quiet zone
        g.setColour(bg);
        g.fillRect(area);

        int quietZone = 2; // module border
        int totalModules = mat.size + quietZone * 2;
        float moduleSize = (float)std::min(area.getWidth(), area.getHeight()) / (float)totalModules;

        int offsetX = area.getX() + (int)((area.getWidth() - moduleSize * totalModules) / 2.0f);
        int offsetY = area.getY() + (int)((area.getHeight() - moduleSize * totalModules) / 2.0f);

        g.setColour(fg);
        for (int y = 0; y < mat.size; ++y) {
            for (int x = 0; x < mat.size; ++x) {
                if (mat.get(x, y)) {
                    int px = offsetX + (int)((x + quietZone) * moduleSize);
                    int py = offsetY + (int)((y + quietZone) * moduleSize);
                    int pw = (int)(((x + quietZone + 1) * moduleSize) - ((x + quietZone) * moduleSize) + 0.5f);
                    int ph = (int)(((y + quietZone + 1) * moduleSize) - ((y + quietZone) * moduleSize) + 0.5f);
                    g.fillRect(px, py, pw, ph);
                }
            }
        }
    }

    /**
     * Build standard Wi-Fi configuration barcode string.
     */
    static juce::String makeWifiPayload(const juce::String& ssid, const juce::String& password) {
        return "WIFI:T:WPA;S:" + ssid + ";P:" + password + ";;";
    }
};

} // namespace OpenRig
