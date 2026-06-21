#include "common.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

bool        flag_swim     = false;
AudioDriver flag_driver   = DRIVER_ALSA;
std::string g_player_id   = "";

// ── Конфиг ──
static std::string get_config_path() {
    const char *home = getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.config/pw-menu/config.json";
}

static std::string json_get(const std::string &json, const std::string &key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (json[pos] == '"') {
        size_t start = pos + 1;
        size_t end = json.find('"', start);
        if (end == std::string::npos) return "";
        return json.substr(start, end - start);
    }
    size_t end = json.find_first_of(",}\n", pos);
    std::string val = json.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
    return val;
}

void load_config(App *app) {
    std::ifstream f(get_config_path());
    if (!f.is_open()) return;
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto get = [&](const std::string &k) { return json_get(json, k); };
    if (!get("theme").empty())        app->theme_name   = get("theme");
    if (!get("bg_path").empty())      app->bg_path      = get("bg_path");
    if (!get("audio_driver").empty()) app->audio_driver = get("audio_driver");
    if (!get("label_prev_text").empty()) app->label_prev_text = get("label_prev_text");
    if (!get("label_play_text").empty()) app->label_play_text = get("label_play_text");
    if (!get("label_next_text").empty()) app->label_next_text = get("label_next_text");
}

void save_config(App *app) {
    system(("mkdir -p " + std::string(getenv("HOME")) + "/.config/pw-menu").c_str());
    std::ofstream f(get_config_path());
    f << "{\n"
      << "  \"theme\": \""        << app->theme_name   << "\",\n"
      << "  \"bg_path\": \""      << app->bg_path      << "\",\n"
      << "  \"audio_driver\": \"" << app->audio_driver << "\",\n"
      << "  \"label_prev_text\": \"" << app->label_prev_text << "\",\n"
      << "  \"label_play_text\": \"" << app->label_play_text << "\",\n"
      << "  \"label_next_text\": \"" << app->label_next_text << "\"\n"
      << "}\n";
}

// ── Утилиты ──
std::string run_cmd(const std::string& cmd) {
    char buffer[128];
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        result += buffer;
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

static std::string playerctl_cmd(const std::string &subcmd) {
    std::string p = g_player_id.empty() ? "" : " -p " + g_player_id;
    return "playerctl" + p + " " + subcmd + " 2>/dev/null";
}

void playerctl_async(const std::string &subcmd) {
    system((playerctl_cmd(subcmd) + " &").c_str());
}

std::vector<std::string> split_lines(const std::string &s) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { if (!cur.empty()) lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::vector<std::string> get_players() {
    return split_lines(run_cmd("playerctl -l 2>/dev/null"));
}

// ── Громкость ──
double get_volume() {
    std::string out;
    if (flag_driver == DRIVER_ALSA) {
        out = run_cmd("amixer get Master 2>/dev/null | grep -o '[0-9]*%' | head -1");
        int vol = 50;
        sscanf(out.c_str(), "%d%%", &vol);
        return vol;
    }
    if (flag_driver == DRIVER_PIPEWIRE) {
        out = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null");
        size_t pos = out.find("Volume:");
        if (pos != std::string::npos) {
            try { return std::stod(out.substr(pos + 7)) * 100.0; }
            catch (...) {}
        }
    }
    if (flag_driver == DRIVER_PULSE) {
        out = run_cmd("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null");
        size_t pos = out.find('%');
        if (pos != std::string::npos) {
            size_t start = out.rfind(' ', pos);
            if (start != std::string::npos) {
                try { return std::stoi(out.substr(start + 1, pos - start - 1)); }
                catch (...) {}
            }
        }
    }
    return 50;
}

void on_vol_changed(GtkRange *range, gpointer) {
    int val = (int)gtk_range_get_value(range);
    std::string cmd;
    if (flag_driver == DRIVER_ALSA)
        cmd = "amixer set Master " + std::to_string(val) + "% > /dev/null 2>&1 &";
    else if (flag_driver == DRIVER_PIPEWIRE) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", val / 100.0);
        cmd = "wpctl set-volume @DEFAULT_AUDIO_SINK@ " + std::string(buf) + " > /dev/null 2>&1 &";
    } else
        cmd = "pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(val) + "% > /dev/null 2>&1 &";
    system(cmd.c_str());
}

// ── Обложки ──
static std::string home_dir() {
    const char *h = getenv("HOME");
    return h ? std::string(h) : "/tmp";
}

static std::string covers_dir() {
    return home_dir() + "/.covers";
}

static std::string url_hash(const std::string &url) {
    size_t h = std::hash<std::string>{}(url);
    char buf[32];
    snprintf(buf, sizeof(buf), "%016zx", h);
    return std::string(buf);
}

std::string resolve_cover_path(const std::string &path) {
    if (path.empty()) return "";
    if (path.rfind("file://", 0) == 0) return path.substr(7);
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
        system(("mkdir -p " + covers_dir()).c_str());
        std::string url = path;
        url.erase(std::remove(url.begin(), url.end(), '\''), url.end());
        url.erase(std::remove(url.begin(), url.end(), '"'),  url.end());
        std::string hash    = url_hash(url);
        std::string png_out = covers_dir() + "/" + hash + ".png";
        std::string jpg_out = covers_dir() + "/" + hash + ".jpg";
        struct stat st{};
        if (stat(png_out.c_str(), &st) == 0 && st.st_size > 0) return png_out;
        if (system(("curl -s -L --max-time 10 \"" + url + "\" -o \"" + jpg_out + "\"").c_str()) != 0) return "";
        if (system(("ffmpeg -y -i \"" + jpg_out + "\" \"" + png_out + "\" > /dev/null 2>&1").c_str()) != 0) return "";
        remove(jpg_out.c_str());
        return png_out;
    }
    return path;
}

cairo_surface_t* load_image(const std::string &path) {
    if (path.empty()) return nullptr;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) return nullptr;
    GError *err = nullptr;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file(path.c_str(), &err);
    if (err) { g_error_free(err); return nullptr; }
    if (!pb) return nullptr;
    cairo_surface_t *surf = gdk_cairo_surface_create_from_pixbuf(pb, 0, nullptr);
    g_object_unref(pb);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf) cairo_surface_destroy(surf);
        return nullptr;
    }
    return surf;
}

// БЕЗОПАСНОЕ УНИЧТОЖЕНИЕ ПОВЕРХНОСТЕЙ
void safe_surface_destroy(cairo_surface_t **surf) {
    if (surf && *surf && cairo_surface_status(*surf) == CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(*surf);
        *surf = nullptr;
    }
}

void reload_cover(App *app, const std::string &path) {
    safe_surface_destroy(&app->cover);
    app->cover = load_image(path);
}

void reload_bg(App *app) {
    safe_surface_destroy(&app->bg_image);
    if (!app->bg_path.empty())
        app->bg_image = load_image(app->bg_path);
}

// ── Listener-поток ──
void playerctl_listener(App *app) {
    std::thread([app]() {
        while (true) {
            auto players = get_players();
            std::lock_guard<std::mutex> lock(app->mtx);
            app->next_players = players;
            app->players_changed = true;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }).detach();

    FILE *pipe = popen(
        "playerctl metadata --format "
        "'TITLE:{{title}}\nARTIST:{{artist}}\nURL:{{mpris:artUrl}}"
        "\nSTATUS:{{status}}\nLENGTH:{{mpris:length}}' "
        "--follow 2>/dev/null", "r");
    if (!pipe) return;

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') line.pop_back();

        if (line.rfind("STATUS:", 0) == 0) {
            bool playing = (line.substr(7) == "Playing");
        // Мгновенное обновление статуса
        app->spinning_next = playing;
        app->spinning_changed = true;
        app->spinning_next = playing;
        app->spinning_changed = true;
            double pos = -1.0;
            if (playing) {
                std::string p = run_cmd(playerctl_cmd("position"));
                if (!p.empty()) try { pos = std::stod(p); } catch (...) {}
            }
            {
                std::lock_guard<std::mutex> lock(app->mtx);
                bool was = app->spinning_next;
                app->spinning_next = playing;
                app->spinning_changed = true;
                if (playing && !was && pos >= 0.0) {
                    app->current_pos = pos;
                    app->track_switched = true;
                }
            }
            continue;
        }

        if (line.rfind("URL:", 0) == 0) {
            std::string raw = line.substr(4);
            std::thread([app, raw]() {
                std::string resolved = resolve_cover_path(raw);
                std::lock_guard<std::mutex> lock(app->mtx);
                app->next_cover_path = resolved;
                app->cover_changed = true;
            }).detach();
            continue;
        }

        if (line.rfind("LENGTH:", 0) == 0) {
            double total = 0.0, pos = 0.0;
            try { total = std::stod(line.substr(7)) / 1000000.0; } catch (...) {}
            std::string p = run_cmd(playerctl_cmd("position"));
            if (!p.empty()) try { pos = std::stod(p); } catch (...) {}
            std::lock_guard<std::mutex> lock(app->mtx);
            app->total_len = total;
            app->current_pos = pos;
            app->track_switched = true;
            continue;
        }

        std::lock_guard<std::mutex> lock(app->mtx);
        if (line.rfind("TITLE:", 0) == 0) {
            std::string tt = line.substr(6);
            if (tt.empty()) tt = "No track";
            if (app->next_title != tt) {
                app->next_title = tt;
                app->status_changed = true;
                app->current_pos = 0.0;
                app->track_switched = true;
            }
        } else if (line.rfind("ARTIST:", 0) == 0) {
            app->next_artist = line.substr(7);
            app->status_changed = true;
        }
    }
    pclose(pipe);
}

// ── Кнопки управления ──
void on_btn_clicked(GtkWidget *, gpointer data) {
    const char *cmd = (const char*)data;
    playerctl_async(cmd);
}

void on_time_changed(GtkRange *range, gpointer data) {
    App *app = (App*)data;
    if (app->updating_time.load()) return;
    double val = gtk_range_get_value(range);
    app->current_pos = val;
    playerctl_async("position " + std::to_string(val));
}

// Дополнительный поток для принудительной синхронизации статуса





// Принудительная синхронизация статуса (каждые 50 мс)
void spin_sync_thread(App *app) {
    while (true) {
        std::string status = run_cmd("playerctl status 2>/dev/null");
        bool playing = (status == "Playing");
        {
            std::lock_guard<std::mutex> lock(app->mtx);
            if (app->spinning != playing) {
                app->spinning = playing;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
