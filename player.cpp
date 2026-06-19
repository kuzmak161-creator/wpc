#include <gtk/gtk.h>
#include <cairo.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <array>
#include <cstdio>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <functional>
#include <sys/stat.h>

// ──────────────────────────────────────────────
//  Аудиодрайверы
// ──────────────────────────────────────────────
enum AudioDriver { DRIVER_ALSA, DRIVER_PIPEWIRE, DRIVER_PULSE };

bool        flag_swim   = false;
AudioDriver flag_driver = DRIVER_ALSA;

// ──────────────────────────────────────────────
//  Структура приложения
// ──────────────────────────────────────────────
struct App {
    GtkWidget *window     = nullptr;
    GtkWidget *lbl_title  = nullptr;
    GtkWidget *lbl_artist = nullptr;
    GtkWidget *disc_area  = nullptr;
    GtkWidget *slider_vol  = nullptr;
    GtkWidget *slider_time = nullptr;

    double angle    = 0.0;
    bool   spinning = false;

    cairo_surface_t *cover = nullptr;

    // Локальный трекинг позиции
    double current_pos = 0.0;
    double total_len   = 0.0;
    std::atomic<bool> updating_time{false};

    // Данные из listener-потока (защищены mtx)
    std::mutex  mtx;
    std::string next_title      = "Нет трека";
    std::string next_artist;
    std::string next_cover_path;
    bool status_changed = false;
    bool cover_changed  = false;
    bool track_switched = false;

    // Перетаскивание окна (--swim)
    // Запоминаем позицию окна и курсора в момент нажатия —
    // чтобы считать абсолютное смещение, без накопления ошибки.
    gint win_start_x  = 0;
    gint win_start_y  = 0;
    gint drag_start_x = 0;
    gint drag_start_y = 0;
};

// ──────────────────────────────────────────────
//  Утилиты
// ──────────────────────────────────────────────

static std::string home_dir() {
    const char *h = getenv("HOME");
    return h ? std::string(h) : "/tmp";
}

static void mkdir_p(const std::string &path) {
    system(("mkdir -p \"" + path + "\"").c_str());
}

static std::string run_cmd(const std::string &cmd) {
    std::array<char, 256> buf;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buf.data(), buf.size(), pipe.get()))
        result += buf.data();
    if (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}

// Жёсткий выход — избегаем зависания на закрытии popen-пайпов
static void force_quit(GtkWidget *, gpointer) { std::_Exit(0); }

static gboolean on_delete(GtkWidget *, GdkEvent *, gpointer) {
    std::_Exit(0);
    return TRUE;
}

// ──────────────────────────────────────────────
//  Громкость
// ──────────────────────────────────────────────
static int get_volume() {
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
            try { return static_cast<int>(std::stod(out.substr(pos + 7)) * 100.0); }
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

static void on_vol_changed(GtkRange *range, gpointer) {
    int val = static_cast<int>(gtk_range_get_value(range));
    std::string cmd;
    if (flag_driver == DRIVER_ALSA)
        cmd = "amixer set Master " + std::to_string(val) + "% > /dev/null 2>&1";
    else if (flag_driver == DRIVER_PIPEWIRE) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", val / 100.0);
        cmd = "wpctl set-volume @DEFAULT_AUDIO_SINK@ " + std::string(buf) + " > /dev/null 2>&1";
    } else
        cmd = "pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(val) + "% > /dev/null 2>&1";
    system(cmd.c_str());
}

// ──────────────────────────────────────────────
//  Обложки
// ──────────────────────────────────────────────

static std::string url_hash(const std::string &url) {
    size_t h = std::hash<std::string>{}(url);
    char buf[32];
    snprintf(buf, sizeof(buf), "%016zx", h);
    return std::string(buf);
}

static std::string covers_dir() {
    return home_dir() + "/.covers";
}

// Скачивает/конвертирует обложку, возвращает путь к PNG.
// Вызывается из listener-потока — вне GTK и вне mutex.
static std::string resolve_cover_path(const std::string &path) {
    if (path.empty()) return "";

    if (path.rfind("file://", 0) == 0)
        return path.substr(7);

    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
        mkdir_p(covers_dir());

        std::string url = path;
        url.erase(std::remove(url.begin(), url.end(), '\''), url.end());
        url.erase(std::remove(url.begin(), url.end(), '"'),  url.end());

        std::string hash    = url_hash(url);
        std::string jpg_out = covers_dir() + "/" + hash + ".jpg";
        std::string png_out = covers_dir() + "/" + hash + ".png";

        // Уже скачано — отдаём сразу
        struct stat st{};
        if (stat(png_out.c_str(), &st) == 0 && st.st_size > 0)
            return png_out;

        if (system(("curl -s -L --max-time 10 \"" + url + "\" -o \"" + jpg_out + "\"").c_str()) != 0)
            return "";
        if (system(("ffmpeg -y -i \"" + jpg_out + "\" \"" + png_out + "\" > /dev/null 2>&1").c_str()) != 0)
            return "";

        remove(jpg_out.c_str());
        return png_out;
    }

    return path;
}

static void load_cover(App *app, const std::string &path) {
    if (app->cover) {
        cairo_surface_destroy(app->cover);
        app->cover = nullptr;
    }
    if (path.empty()) return;

    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || st.st_size <= 0 || st.st_size > 12 * 1024 * 1024)
        return;

    GError    *error  = nullptr;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path.c_str(), &error);
    if (error) { g_error_free(error); return; }
    if (!pixbuf) return;

    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        g_object_unref(pixbuf);
        return;
    }

    cairo_surface_t *surf = gdk_cairo_surface_create_from_pixbuf(pixbuf, 0, nullptr);
    g_object_unref(pixbuf);
    if (!surf) return;

    if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS)
        app->cover = surf;
    else
        cairo_surface_destroy(surf);
}

// ──────────────────────────────────────────────
//  Перетаскивание окна (--swim)
//  Фикс: позиция окна и курсора запоминаются один раз
//  при нажатии, смещение считается абсолютно —
//  gtk_window_get_position не вызывается на каждый move.
// ──────────────────────────────────────────────
static gboolean on_button_press(GtkWidget *, GdkEventButton *event, gpointer data) {
    App *app = static_cast<App*>(data);
    if (event->button == 1) {
        gtk_window_get_position(GTK_WINDOW(app->window),
                                &app->win_start_x, &app->win_start_y);
        app->drag_start_x = static_cast<gint>(event->x_root);
        app->drag_start_y = static_cast<gint>(event->y_root);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_button_move(GtkWidget *, GdkEventMotion *event, gpointer data) {
    App *app = static_cast<App*>(data);
    if (event->state & GDK_BUTTON1_MASK) {
        gint dx = static_cast<gint>(event->x_root) - app->drag_start_x;
        gint dy = static_cast<gint>(event->y_root) - app->drag_start_y;
        gtk_window_move(GTK_WINDOW(app->window),
                        app->win_start_x + dx,
                        app->win_start_y + dy);
        return TRUE;
    }
    return FALSE;
}

// ──────────────────────────────────────────────
//  Отрисовка
// ──────────────────────────────────────────────
static gboolean draw_bg(GtkWidget *widget, cairo_t *cr, gpointer) {
    int    w = gtk_widget_get_allocated_width(widget);
    int    h = gtk_widget_get_allocated_height(widget);
    double r = 18.0;

    cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, 0.04, 0.04, 0.18, 0.96);
    cairo_pattern_add_color_stop_rgba(grad, 0.5, 0.02, 0.07, 0.14, 0.97);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, 0.01, 0.03, 0.09, 0.98);
    cairo_set_source(cr, grad);

    cairo_arc(cr, r,   r,   r, M_PI,     3*M_PI/2);
    cairo_arc(cr, w-r, r,   r, 3*M_PI/2, 2*M_PI);
    cairo_arc(cr, w-r, h-r, r, 0,        M_PI/2);
    cairo_arc(cr, r,   h-r, r, M_PI/2,   M_PI);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
    return FALSE;
}

static gboolean draw_disc(GtkWidget *widget, cairo_t *cr, gpointer data) {
    App   *app   = static_cast<App*>(data);
    int    width  = gtk_widget_get_allocated_width(widget);
    int    height = gtk_widget_get_allocated_height(widget);
    double size   = std::min(width, height);
    double cx     = size / 2.0;
    double cy     = size / 2.0;
    double r      = size / 2.0 - 6.0;

    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_rotate(cr, app->angle);

    // Тень
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_arc(cr, 4, 4, r, 0, 2*M_PI);
    cairo_fill(cr);

    // Обложка или градиент
    if (app->cover) {
        cairo_save(cr);
        cairo_arc(cr, 0, 0, r, 0, 2*M_PI);
        cairo_clip(cr);
        int sw = cairo_image_surface_get_width(app->cover);
        int sh = cairo_image_surface_get_height(app->cover);
        double scale = (2.0 * r) / std::min(sw, sh);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, app->cover, -sw/2.0, -sh/2.0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_pattern_t *grad = cairo_pattern_create_radial(-r*0.3, -r*0.3, 0, 0, 0, r);
        cairo_pattern_add_color_stop_rgb(grad, 0.0, 0.15, 0.25, 0.55);
        cairo_pattern_add_color_stop_rgb(grad, 0.6, 0.05, 0.10, 0.28);
        cairo_pattern_add_color_stop_rgb(grad, 1.0, 0.02, 0.04, 0.12);
        cairo_set_source(cr, grad);
        cairo_arc(cr, 0, 0, r, 0, 2*M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
    }

    // Дорожки пластинки
    cairo_set_line_width(cr, 0.7);
    for (int i = 0; i < 7; i++) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
        cairo_arc(cr, 0, 0, r * (0.38 + i * 0.09), 0, 2*M_PI);
        cairo_stroke(cr);
    }

    // Центральное отверстие
    cairo_set_source_rgba(cr, 0.06, 0.06, 0.06, 1);
    cairo_arc(cr, 0, 0, r * 0.17, 0, 2*M_PI);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.64, 0.78, 0.22);
    cairo_arc(cr, 0, 0, r * 0.07, 0, 2*M_PI);
    cairo_fill(cr);

    cairo_restore(cr);
    return TRUE;
}

// ──────────────────────────────────────────────
//  Тик UI (~60 fps)
// ──────────────────────────────────────────────
static gboolean on_tick(gpointer data) {
    App *app = static_cast<App*>(data);

    if (app->mtx.try_lock()) {
        if (app->status_changed) {
            gtk_label_set_text(GTK_LABEL(app->lbl_title),  app->next_title.c_str());
            gtk_label_set_text(GTK_LABEL(app->lbl_artist), app->next_artist.c_str());
            app->status_changed = false;
        }
        if (app->cover_changed) {
            load_cover(app, app->next_cover_path);
            app->cover_changed = false;
        }
        if (app->track_switched) {
            app->updating_time.store(true);
            gtk_range_set_range(GTK_RANGE(app->slider_time), 0, app->total_len);
            gtk_range_set_value(GTK_RANGE(app->slider_time), app->current_pos);
            app->updating_time.store(false);
            app->track_switched = false;
        }
        app->mtx.unlock();
    }

    if (app->spinning) {
        app->angle += 0.015;
        if (app->angle > 2*M_PI) app->angle -= 2*M_PI;

        if (app->current_pos < app->total_len) {
            app->current_pos += 0.016;
            app->updating_time.store(true);
            gtk_range_set_value(GTK_RANGE(app->slider_time), app->current_pos);
            app->updating_time.store(false);
        }
        gtk_widget_queue_draw(app->disc_area);
    }

    return TRUE;
}

// ──────────────────────────────────────────────
//  Listener-поток (playerctl --follow)
//  Тяжёлые операции (run_cmd, resolve_cover_path)
//  выполняются ВНЕ mutex, чтобы не блокировать UI.
// ──────────────────────────────────────────────
static void playerctl_listener(App *app) {
    FILE *pipe = popen(
        "playerctl metadata --format "
        "'TITLE:{{title}}\nARTIST:{{artist}}\nURL:{{mpris:artUrl}}"
        "\nSTATUS:{{status}}\nLENGTH:{{mpris:length}}' "
        "--follow 2>/dev/null",
        "r"
    );
    if (!pipe) return;

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') line.pop_back();

        if (line.rfind("URL:", 0) == 0) {
            std::string resolved = resolve_cover_path(line.substr(4)); // вне lock
            std::lock_guard<std::mutex> lock(app->mtx);
            app->next_cover_path = resolved;
            app->cover_changed   = true;
            continue;
        }

        if (line.rfind("STATUS:", 0) == 0) {
            bool playing = (line.substr(7) == "Playing");
            double pos = -1.0;
            if (playing) { // вне lock
                std::string p = run_cmd("playerctl position 2>/dev/null");
                if (!p.empty()) try { pos = std::stod(p); } catch (...) {}
            }
            std::lock_guard<std::mutex> lock(app->mtx);
            bool was = app->spinning;
            app->spinning = playing;
            if (playing && !was && pos >= 0.0) {
                app->current_pos    = pos;
                app->track_switched = true;
            }
            continue;
        }

        if (line.rfind("LENGTH:", 0) == 0) {
            double total = 0.0, pos = 0.0;
            try { total = std::stod(line.substr(7)) / 1000000.0; } catch (...) {}
            std::string p = run_cmd("playerctl position 2>/dev/null"); // вне lock
            if (!p.empty()) try { pos = std::stod(p); } catch (...) {}
            std::lock_guard<std::mutex> lock(app->mtx);
            app->total_len      = total;
            app->current_pos    = pos;
            app->track_switched = true;
            continue;
        }

        // TITLE / ARTIST — быстрые, сразу под lock
        std::lock_guard<std::mutex> lock(app->mtx);
        if (line.rfind("TITLE:", 0) == 0) {
            std::string t = line.substr(6);
            if (t.empty()) t = "Нет трека";
            if (app->next_title != t) {
                app->next_title     = t;
                app->status_changed = true;
                app->current_pos    = 0.0;
                app->track_switched = true;
            }
        } else if (line.rfind("ARTIST:", 0) == 0) {
            app->next_artist    = line.substr(7);
            app->status_changed = true;
        }
    }

    pclose(pipe);
}

// ──────────────────────────────────────────────
//  Перемотка
// ──────────────────────────────────────────────
static void on_time_changed(GtkRange *range, gpointer data) {
    App *app = static_cast<App*>(data);
    if (app->updating_time.load()) return;
    double val = gtk_range_get_value(range);
    app->current_pos = val;
    system(("playerctl position " + std::to_string(val) + " &").c_str());
}

// ──────────────────────────────────────────────
//  Кнопки управления плеером
// ──────────────────────────────────────────────
static void on_btn_clicked(GtkWidget *, gpointer data) {
    system(("playerctl " + std::string(static_cast<const char*>(data)) + " &").c_str());
}

// ──────────────────────────────────────────────
//  Позиционирование над панелью i3
// ──────────────────────────────────────────────
static void on_realize(GtkWidget *widget, gpointer) {
    GdkDisplay  *display = gdk_display_get_default();
    GdkMonitor  *monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle geo;
    gdk_monitor_get_geometry(monitor, &geo);
    const int win_w = 360, win_h = 540;
    gtk_window_move(GTK_WINDOW(widget),
                    std::max(0, geo.width  - win_w - 15),
                    std::max(0, geo.height - win_h - 50));
}

// ──────────────────────────────────────────────
//  CSS-хелпер
// ──────────────────────────────────────────────
static void apply_css(GtkWidget *widget, const char *css) {
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, css, -1, nullptr);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(widget),
        GTK_STYLE_PROVIDER(p),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(p);
}

// ──────────────────────────────────────────────
//  main
// ──────────────────────────────────────────────
int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout <<
                "======================================================\n"
                " Music Widget — Минималистичный плеер для панелей\n"
                "======================================================\n\n"
                "Использование: " << argv[0] << " [ПАРАМЕТРЫ]\n\n"
                "  -h, --help    Показать справку и выйти\n"
                "  --swim        Плавающее окно (перетаскивание мышью)\n"
                "  --alsa        Управление звуком через amixer (дефолт)\n"
                "  --pipewire    Управление звуком через wpctl\n"
                "  --pulse       Управление звуком через pactl\n\n"
                "Пример: " << argv[0] << " --swim --pipewire\n"
                "======================================================\n";
            return 0;
        }
        if      (arg == "--swim")     flag_swim   = true;
        else if (arg == "--alsa")     flag_driver = DRIVER_ALSA;
        else if (arg == "--pipewire") flag_driver = DRIVER_PIPEWIRE;
        else if (arg == "--pulse")    flag_driver = DRIVER_PULSE;
    }

    gtk_init(&argc, &argv);

    App app;

    // ── Окно ──
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 360, 540);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);
    gtk_widget_set_app_paintable(app.window, TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(app.window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(app.window), GDK_WINDOW_TYPE_HINT_DIALOG);

    GdkScreen *screen = gtk_widget_get_screen(app.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(app.window, visual);

    g_signal_connect(app.window, "draw",         G_CALLBACK(draw_bg),    nullptr);
    g_signal_connect(app.window, "realize",      G_CALLBACK(on_realize), nullptr);
    g_signal_connect(app.window, "destroy",      G_CALLBACK(force_quit), nullptr);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(on_delete),  nullptr);

    if (flag_swim) {
        gtk_widget_add_events(app.window, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
        g_signal_connect(app.window, "button-press-event",  G_CALLBACK(on_button_press), &app);
        g_signal_connect(app.window, "motion-notify-event", G_CALLBACK(on_button_move),  &app);
    }

    // ── Лейаут ──
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), root);

    // Заголовок + кнопка закрытия
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(top, 14);
    gtk_widget_set_margin_start(top, 18);
    gtk_widget_set_margin_end(top, 14);

    app.lbl_title = gtk_label_new("Нет трека");
    gtk_widget_set_halign(app.lbl_title, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(app.lbl_title), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(app.lbl_title), 26);
    apply_css(app.lbl_title, "label { color: #ffffff; font-size: 13px; font-weight: bold; }");
    gtk_box_pack_start(GTK_BOX(top), app.lbl_title, TRUE, TRUE, 0);

    GtkWidget *btn_x = gtk_button_new_with_label("✕");
    gtk_button_set_relief(GTK_BUTTON(btn_x), GTK_RELIEF_NONE);
    g_signal_connect(btn_x, "clicked", G_CALLBACK(force_quit), nullptr);
    apply_css(btn_x,
        "button { color: #888; background: transparent; border: none; font-size: 15px; }"
        "button:hover { color: #fff; }");
    gtk_box_pack_end(GTK_BOX(top), btn_x, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);

    // Артист
    app.lbl_artist = gtk_label_new("");
    gtk_widget_set_margin_top(app.lbl_artist, 2);
    gtk_widget_set_margin_start(app.lbl_artist, 18);
    gtk_widget_set_halign(app.lbl_artist, GTK_ALIGN_START);
    apply_css(app.lbl_artist, "label { color: #a4c639; font-size: 10px; }");
    gtk_box_pack_start(GTK_BOX(root), app.lbl_artist, FALSE, FALSE, 0);

    // Диск
    app.disc_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.disc_area, 200, 200);
    gtk_widget_set_halign(app.disc_area, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app.disc_area, 20);
    g_signal_connect(app.disc_area, "draw", G_CALLBACK(draw_disc), &app);
    gtk_box_pack_start(GTK_BOX(root), app.disc_area, FALSE, FALSE, 0);

    // Кнопки управления
    GtkWidget *ctrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(ctrl, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ctrl, 20);

    static const char *cmd_prev = "previous";
    static const char *cmd_play = "play-pause";
    static const char *cmd_next = "next";

    GtkWidget *b_prev = gtk_button_new_with_label("⏮");
    GtkWidget *b_play = gtk_button_new_with_label("⏯");
    GtkWidget *b_next = gtk_button_new_with_label("⏭");

    const char *btn_css =
        "button { background: rgba(164,198,57,0.12); border: 1px solid #a4c639;"
        " border-radius: 50%; color: #fff; font-size: 20px;"
        " min-width: 52px; min-height: 52px; padding: 0; }"
        "button:hover { background: rgba(164,198,57,0.3); }";
    apply_css(b_prev, btn_css);
    apply_css(b_play, btn_css);
    apply_css(b_next, btn_css);

    g_signal_connect(b_prev, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_prev);
    g_signal_connect(b_play, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_play);
    g_signal_connect(b_next, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_next);

    gtk_box_pack_start(GTK_BOX(ctrl), b_prev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl), b_play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl), b_next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), ctrl,   FALSE, FALSE, 0);

    // Таймлайн
    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(time_box, 24);
    gtk_widget_set_margin_end(time_box, 24);
    gtk_widget_set_margin_top(time_box, 20);
    gtk_box_pack_start(GTK_BOX(time_box), gtk_label_new("⏳"), FALSE, FALSE, 0);

    app.slider_time = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app.slider_time), FALSE);
    g_signal_connect(app.slider_time, "value-changed", G_CALLBACK(on_time_changed), &app);
    apply_css(app.slider_time,
        "scale trough { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 4px; }"
        "scale highlight { background: #39a4c6; border-radius: 4px; }"
        "scale slider { background: #fff; border-radius: 50%; min-width: 12px; min-height: 12px; }");
    gtk_box_pack_start(GTK_BOX(time_box), app.slider_time, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), time_box, FALSE, FALSE, 0);

    // Громкость
    GtkWidget *vol_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(vol_box, 24);
    gtk_widget_set_margin_end(vol_box, 24);
    gtk_widget_set_margin_top(vol_box, 14);
    gtk_widget_set_margin_bottom(vol_box, 20);
    gtk_box_pack_start(GTK_BOX(vol_box), gtk_label_new("🔊"), FALSE, FALSE, 0);

    app.slider_vol = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app.slider_vol), FALSE);
    gtk_range_set_value(GTK_RANGE(app.slider_vol), get_volume());
    g_signal_connect(app.slider_vol, "value-changed", G_CALLBACK(on_vol_changed), nullptr);
    apply_css(app.slider_vol,
        "scale trough { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 4px; }"
        "scale highlight { background: #a4c639; border-radius: 4px; }"
        "scale slider { background: #fff; border-radius: 50%; min-width: 14px; min-height: 14px; }");
    gtk_box_pack_start(GTK_BOX(vol_box), app.slider_vol, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), vol_box, FALSE, FALSE, 0);

    // ── Инициализация состояния ──
    app.spinning = (run_cmd("playerctl status 2>/dev/null") == "Playing");
    if (app.spinning) {
        try {
            std::string len = run_cmd("playerctl metadata --format '{{mpris:length}}' 2>/dev/null");
            std::string pos = run_cmd("playerctl position 2>/dev/null");
            if (!len.empty()) app.total_len   = std::stod(len) / 1000000.0;
            if (!pos.empty()) app.current_pos = std::stod(pos);
            app.track_switched = true;
        } catch (...) {}
    }

    // ── Listener-поток ──
    std::thread(playerctl_listener, &app).detach();

    gtk_widget_show_all(app.window);
    g_timeout_add(16, on_tick, &app);
    gtk_main();

    return 0;
}
