#include "tones.h"

#include <math.h>

#include "app/storage.h"
#include "player.h"

namespace tones {
namespace {

constexpr int kRate = 24000;  // plenty for tones; the player resamples to 48 kHz

// Adds a note: sine partials with a short attack and exponential decay
void note(std::vector<float> &buf, float start, float freq, float decay, float gain,
          bool bell) {
    // Bell partials are inharmonic, which is what makes a bell sound like one
    static const float kBell[][2] = {{1.0f, 1.0f}, {2.76f, 0.40f}, {5.40f, 0.20f}, {8.93f, 0.08f}};
    static const float kSoft[][2] = {{1.0f, 1.0f}, {2.0f, 0.25f}, {3.0f, 0.06f}, {0.0f, 0.0f}};
    const auto &partials = bell ? kBell : kSoft;
    const float attack = bell ? 0.004f : 0.06f;

    const size_t first = start * kRate;
    const size_t length = decay * 6 * kRate;
    for (size_t i = 0; i < length && first + i < buf.size(); i++) {
        const float t = float(i) / kRate;
        const float env = min(1.0f, t / attack) * expf(-t / decay);
        float v = 0;
        for (const auto &p : partials) {
            if (p[0] > 0) v += p[1] * sinf(2.0f * PI * freq * p[0] * t);
        }
        buf[first + i] += gain * env * v;
    }
}

void chime(std::vector<float> &b) {
    const float melody[] = {1318.5f, 1046.5f, 784.0f, 1046.5f};  // E6 C6 G5 C6
    for (int i = 0; i < 4; i++) note(b, 0.38f * i, melody[i], 0.55f, 0.5f, true);
}

void beeps(std::vector<float> &b) {
    for (int i = 0; i < 4; i++) {
        const size_t first = 0.16f * i * kRate;
        const size_t length = 0.09f * kRate;
        for (size_t n = 0; n < length; n++) {
            const float t = float(n) / kRate;
            const float edge = min(1.0f, min(t, 0.09f - t) / 0.002f);  // no clicks
            const float w = 2.0f * PI * 2048.0f * t;
            b[first + n] = 0.6f * edge * (sinf(w) + 0.3f * sinf(3 * w));
        }
    }
}

void sunrise(std::vector<float> &b) {
    const float arpeggio[] = {523.3f, 659.3f, 784.0f, 1046.5f, 1318.5f};  // C5 E5 G5 C6 E6
    for (int i = 0; i < 5; i++) note(b, 0.3f * i, arpeggio[i], 0.7f, 0.35f, false);
}

struct Builtin {
    const char *id;
    const char *name;
    const char *path;  // versioned: change the synth, bump the name (and player.cpp's chime)
    float seconds;
    void (*synth)(std::vector<float> &);  // adds to a silent buffer that long
};

const Builtin kBuiltins[] = {
    {"tone:chime", "Chime", "/sounds/chime-1.wav", 3.4f, chime},
    {"tone:beeps", "Beeps", "/sounds/beeps-1.wav", 1.2f, beeps},
    {"tone:sunrise", "Sunrise", "/sounds/sunrise-1.wav", 3.2f, sunrise},
};

constexpr size_t kWavHeader = 44;

size_t sampleCount(const Builtin &b) { return b.seconds * kRate; }

bool put(File &f, const void *data, size_t n) {
    return f.write(static_cast<const uint8_t *>(data), n) == n;
}
bool putU32(File &f, uint32_t v) { return put(f, &v, 4); }
bool putU16(File &f, uint16_t v) { return put(f, &v, 2); }

// 16-bit mono PCM WAV, normalised so every tone peaks at the same level. The
// chime is what an alarm falls back on: a file cut short (the flash full) goes,
// so the next start writes it again.
bool writeWav(const char *path, const std::vector<float> &samples) {
    float peak = 0.001f;
    for (float s : samples) peak = max(peak, fabsf(s));
    const float scale = 0.8f * 32767 / peak;

    File f = storage::internal().open(path, "w");
    if (!f) return false;
    const uint32_t dataBytes = samples.size() * 2;
    bool ok = put(f, "RIFF", 4) && putU32(f, kWavHeader - 8 + dataBytes) && put(f, "WAVEfmt ", 8) &&
              putU32(f, 16) && putU16(f, 1) &&  // PCM
              putU16(f, 1) &&                   // mono
              putU32(f, kRate) && putU32(f, kRate * 2) && putU16(f, 2) && putU16(f, 16) &&
              put(f, "data", 4) && putU32(f, dataBytes);

    int16_t chunk[256];
    size_t n = 0;
    for (size_t i = 0; ok && i < samples.size(); i++) {
        chunk[n++] = int16_t(samples[i] * scale);
        if (n == 256) {
            ok = put(f, chunk, sizeof(chunk));
            n = 0;
        }
    }
    ok = ok && put(f, chunk, n * 2);
    f.close();
    if (!ok) storage::internal().remove(path);
    return ok;
}

// Exactly as long as it should be: one cut short (a reset while writing) or
// from a different synth isn't
bool complete(const Builtin &b) {
    fs::FS &fs = storage::internal();
    if (!fs.exists(b.path)) return false;
    File f = fs.open(b.path, "r");
    return f && f.size() == kWavHeader + sampleCount(b) * 2;
}

// Older versions of the tones (another number in the name) aren't needed
void removeOldVersions() {
    std::vector<String> old;
    {
        if (!storage::internal().exists("/sounds")) return;  // the first start
        File dir = storage::internal().open("/sounds");
        if (!dir || !dir.isDirectory()) return;
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const String path = String("/sounds/") + f.name();
            bool current = false;
            for (const Builtin &b : kBuiltins) current |= path == b.path;
            if (!current && !f.isDirectory()) old.push_back(path);
        }
    }
    for (const String &path : old) {
        Serial.printf("tones: removing %s\n", path.c_str());
        storage::internal().remove(path);
    }
}

bool isAudioFile(const String &name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".m4a") ||
           lower.endsWith(".aac") || lower.endsWith(".flac") || lower.endsWith(".ogg");
}

}  // namespace

void begin() {
    removeOldVersions();
    for (const Builtin &b : kBuiltins) {
        if (complete(b)) continue;
        std::vector<float> samples(sampleCount(b), 0.0f);
        b.synth(samples);
        const bool ok = writeWav(b.path, samples);
        Serial.printf("tones: %s %s\n", ok ? "wrote" : "couldn't write", b.path);
    }
}

std::vector<Sound> list() {
    std::vector<Sound> out;
    for (const Builtin &b : kBuiltins) out.push_back({b.id, b.name});
    if (!storage::checkForCard()) return out;

    File dir = storage::card().open("/sounds");
    if (!dir || !dir.isDirectory()) return out;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const String name = f.name();
        if (f.isDirectory() || !isAudioFile(name)) continue;
        out.push_back({"sd:/sounds/" + name, name.substring(0, name.lastIndexOf('.'))});
    }
    return out;
}

bool play(const String &id) {
    if (id.startsWith("tone:")) {
        for (const Builtin &b : kBuiltins) {
            if (id == b.id) {
                player::playFile(b.path, true);
                return true;
            }
        }
        return false;
    }
    if (id.startsWith("sd:")) {
        if (!storage::checkForCard()) return false;
        player::playFile(id.c_str() + 3, true, true);
        return true;
    }
    if (id.startsWith("radio:")) {
        player::playUrl(id.c_str() + 6, "");
        return true;
    }
    return false;
}

}  // namespace tones
