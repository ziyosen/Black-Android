// VAProbe — Fase 0 (skema memori 32-bit): ukur reservasi VA kontinu terbesar yang
// bisa didapat proses 32-bit di perangkat user, plus peta kasar /proc/self/maps.
// Sekali jalan, tampilkan hasil di layar + log file. Tidak menyentuh jalur game.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cerrno>
#include <sys/mman.h>
#include <string>
#include <vector>

namespace {

std::string g_report;
std::vector<std::string> g_lines;

void Line(const std::string& s) {
    g_lines.push_back(s);
    __android_log_print(ANDROID_LOG_INFO, "VAPROBE", "%s", s.c_str());
    g_report += s + "\n";
}

struct Size { const char* label; size_t bytes; };

// ukuran turun dari 3,83 GB (span guest terpakai 0xF4000000) — sesuai DESIGN.md Fase 0
const Size kSizes[] = {
    {"0xF4000000 (3.83 GB)", 0xF4000000ull},
    {"3.50 GB", 0xE0000000ull},
    {"3.25 GB", 0xD0000000ull},
    {"3.00 GB", 0xC0000000ull},
    {"2.75 GB", 0xB0000000ull},
    {"2.50 GB", 0xA0000000ull},
    {"2.25 GB", 0x90000000ull},
    {"0x84000000 (2.06 GB)", 0x84000000ull},
    {"2.00 GB", 0x80000000ull},
    {"1.50 GB", 0x60000000ull},
};

void Probe() {
    Line("VAProbe — probe VA kontinu proses 32-bit");
    Line("------------------------------------------");

    const char* biggest_ok = nullptr;
    const char* first_fail = nullptr;

    for (const Size& sz : kSizes) {
        void* p = mmap(nullptr, sz.bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p != MAP_FAILED) {
            char base[16];
            snprintf(base, sizeof base, "%X", (unsigned)(uintptr_t)p);
            Line("OK   " + std::string(sz.label) + "  base=0x" + base);
            if (!biggest_ok) biggest_ok = sz.label;
            munmap(p, sz.bytes);
        } else {
            Line("FAIL " + std::string(sz.label) + "  errno=" + std::to_string(errno));
            if (!first_fail) first_fail = sz.label;
        }
    }

    Line("");
    Line("HASIL: terbesar OK = " + std::string(biggest_ok ? biggest_ok : "tidak ada") +
         (first_fail ? (std::string(" | pertama FAIL = ") + first_fail) : std::string(" | semua OK")));
    Line("");

    // peta kasar /proc/self/maps: blok-mapping terbesar (rentang alamat antar gap)
    FILE* f = fopen("/proc/self/maps", "r");
    if (f) {
        uintptr_t lo = ~uintptr_t(0), hi = 0, prev_end = 0, biggest_gap = 0;
        unsigned long total = 0;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            uintptr_t a, b;
            if (sscanf(line, "%x-%x", &a, &b) == 2) {
                if (a > prev_end && prev_end) biggest_gap = std::max(biggest_gap, a - prev_end);
                lo = std::min(lo, a); hi = std::max(hi, b); total += b - a; prev_end = b;
            }
        }
        fclose(f);
        char buf[256];
        snprintf(buf, sizeof buf, "maps: span 0x%lX..0x%lX, total %lu MB, gap terbesar %lu MB",
                 (unsigned long)lo, (unsigned long)hi, (unsigned long)(total >> 20), (unsigned long)(biggest_gap >> 20));
        Line(buf);
    }
}

// log file di folder eksternal aplikasi
void SaveLog() {
    const char* dir = SDL_GetAndroidExternalStoragePath();
    if (!dir) return;
    std::string path = std::string(dir) + "/vaprobe.log";
    FILE* f = fopen(path.c_str(), "w");
    if (f) { fwrite(g_report.data(), 1, g_report.size(), f); fclose(f); }
    Line("log: " + path);
}

std::string g_status;  // tampilan layar

}  // namespace

int main(int, char**) {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return 1;
    SDL_Window* w = SDL_CreateWindow("VAProbe", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!w) return 1;

    Probe();
    SaveLog();

    // layar ringkas: hasil paling penting dulu (mintaan Muse: terbesar OK + pertama FAIL eksplisit)
    const char* biggest_ok = nullptr;
    const char* first_fail = nullptr;
    for (const Size& sz : kSizes) {
        void* p = mmap(nullptr, sz.bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p != MAP_FAILED) { if (!biggest_ok) biggest_ok = sz.label; munmap(p, sz.bytes); }
        else if (!first_fail) first_fail = sz.label;
    }
    g_status = std::string("VA terbesar OK: ") + (biggest_ok ? biggest_ok : "-") +
               "\nPertama gagal: " + (first_fail ? first_fail : "-") +
               "\n\nDetail + log: vaprobe.log di folder aplikasi";

    SDL_Renderer* r = SDL_CreateRenderer(w, nullptr);
    for (;;) {
        SDL_Event e;
        bool quit = false;
        while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_KEY_DOWN) quit = true;
        if (quit) break;
        SDL_SetRenderDrawColor(r, 11, 11, 15, 255);
        SDL_RenderClear(r);
        SDL_SetRenderDrawColor(r, 249, 115, 22, 255);
        // teks via logcat & message box: renderer primitif tanpa font — tampilkan lewat dialog sekali
        static bool shown = false;
        if (!shown) {
            shown = true;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "VAProbe", g_status.c_str(), w);
        }
        SDL_Delay(50);
        SDL_RenderPresent(r);
    }
    return 0;
}
