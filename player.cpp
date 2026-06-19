#include <gtk/gtk.h>
#include <cairo.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <array>
#include <cstdio>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstdlib>
#include <string>

// Доступные аудиодрайверы
enum AudioDriver {
    DRIVER_ALSA,
    DRIVER_PIPEWIRE,
    DRIVER_PULSE
};

// Глобальные флаги запуска
bool flag_swim = false;                   // По умолчанию окно закреплено на месте
AudioDriver flag_driver = DRIVER_ALSA;   // По умолчанию используем amixer (ALSA)

struct App {
    GtkWidget *window;
    GtkWidget *lbl_title;
    GtkWidget *lbl_artist;
    GtkWidget *disc_area;
    GtkWidget *slider_vol;
    GtkWidget *slider_time;
    double angle = 0.0;
    bool spinning = false;
    cairo_surface_t *cover = nullptr;
    
    // Локальная логика времени
    double current_pos = 0.0;
    double total_len = 0.0;
    bool updating_time = false;
    
    // Межпоточный обмен
    std::mutex mtx;
    std::string next_title = "Нет трека";
    std::string next_artist = "";
    std::string next_cover_path = "";
    bool status_changed = false;
    bool cover_changed = false;
    bool track_switched = false;

    // Переменные для перетаскивания окна мышью
    gint drag_start_x = 0;
    gint drag_start_y = 0;
};

// Жесткий выход без зависания glibc при закрытии пайпов
void force_quit(GtkWidget *widget, gpointer data) {
    std::_Exit(0);
}

gboolean on_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
    std::_Exit(0);
    return TRUE;
}

std::string run_cmd(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

void on_btn_clicked(GtkWidget *btn, gpointer data) {
    const char* cmd = (const char*)data;
    std::string full_cmd = "playerctl " + std::string(cmd) + " &";
    system(full_cmd.c_str());
}

// Универсальное чтение громкости по драйверам
int get_volume() {
    std::string out;
    if (flag_driver == DRIVER_ALSA) {
        out = run_cmd("amixer get Master | grep -o '[0-9]*%' | head -1");
        int vol = 50;
        if (!out.empty()) {
            sscanf(out.c_str(), "%d%%", &vol);
        }
        return vol;
    } 
    else if (flag_driver == DRIVER_PIPEWIRE) {
        out = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null");
        size_t pos = out.find("Volume:");
        if (pos != std::string::npos) {
            try {
                double vol = std::stod(out.substr(pos + 7));
                return static_cast<int>(vol * 100.0);
            } catch (...) {}
        }
    } 
    else if (flag_driver == DRIVER_PULSE) {
        out = run_cmd("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null");
        size_t pos = out.find('%');
        if (pos != std::string::npos) {
            size_t start = out.rfind(' ', pos);
            if (start != std::string::npos) {
                try {
                    return std::stoi(out.substr(start + 1, pos - start - 1));
                } catch (...) {}
            }
        }
    }
    return 50;
}

// Универсальная отправка громкости по драйверам
void on_vol_changed(GtkRange *range, gpointer data) {
    int val = (int)gtk_range_get_value(range);
    std::string cmd;

    if (flag_driver == DRIVER_ALSA) {
        cmd = "amixer set Master " + std::to_string(val) + "% > /dev/null";
    } 
    else if (flag_driver == DRIVER_PIPEWIRE) {
        double vol = val / 100.0;
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f", vol);
        cmd = "wpctl set-volume @DEFAULT_AUDIO_SINK@ " + std::string(buf) + " > /dev/null 2>&1";
    } 
    else if (flag_driver == DRIVER_PULSE) {
        cmd = "pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(val) + "% > /dev/null 2>&1";
    }

    if (!cmd.empty()) {
        system(cmd.c_str());
    }
}

// Обработчики мыши для перемещения окна (активны только при --swim)
gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    App *app = static_cast<App*>(data);
    if (event->button == 1) { 
        app->drag_start_x = static_cast<gint>(event->x_root);
        app->drag_start_y = static_cast<gint>(event->y_root);
        return TRUE;
    }
    return FALSE;
}

gboolean on_button_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    App *app = static_cast<App*>(data);
    if (event->state & GDK_BUTTON1_MASK) { 
        gint cur_x, cur_y;
        gtk_window_get_position(GTK_WINDOW(app->window), &cur_x, &cur_y);
        
        gint dx = static_cast<gint>(event->x_root) - app->drag_start_x;
        gint dy = static_cast<gint>(event->y_root) - app->drag_start_y;
        
        gtk_window_move(GTK_WINDOW(app->window), cur_x + dx, cur_y + dy);
        
        app->drag_start_x = static_cast<gint>(event->x_root);
        app->drag_start_y = static_cast<gint>(event->y_root);
        return TRUE;
    }
    return FALSE;
}

void load_cover_from_path(App *app, const std::string& path) {
    if (app->cover) {
        cairo_surface_destroy(app->cover);
        app->cover = nullptr;
    }
    
    std::string real_path = "";
    if (path.rfind("file://", 0) == 0) {
        real_path = path.substr(7);
    } else if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
        std::string clean_url = path;
        clean_url.erase(std::remove(clean_url.begin(), clean_url.end(), '\''), clean_url.end());
        clean_url.erase(std::remove(clean_url.begin(), clean_url.end(), '"'), clean_url.end());
        
        std::string curl_cmd = "curl -s -L \"" + clean_url + "\" -o /tmp/music_widget_cover.jpg";
        if (system(curl_cmd.c_str()) == 0) {
            system("ffmpeg -y -i /tmp/music_widget_cover.jpg /tmp/music_widget_cover.png > /dev/null 2>&1");
            real_path = "/tmp/music_widget_cover.png";
        }
    } else {
        real_path = path;
    }

    if (real_path.empty()) return;

    FILE *f = fopen(real_path.c_str(), "rb");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long f_size = ftell(f);
    fclose(f);
    if (f_size <= 0 || f_size > 12 * 1024 * 1024) return; // Увеличили лимит под жирные картинки

    GError *error = nullptr;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(real_path.c_str(), &error);
    if (error != nullptr) {
        g_error_free(error);
        if (pixbuf) g_object_unref(pixbuf);
        return;
    }

    if (pixbuf) {
        int w = gdk_pixbuf_get_width(pixbuf);
        int h = gdk_pixbuf_get_height(pixbuf);
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
            g_object_unref(pixbuf);
            return;
        }
        cairo_surface_t *temp_surf = gdk_cairo_surface_create_from_pixbuf(pixbuf, 0, nullptr);
        g_object_unref(pixbuf);
        if (temp_surf) {
            if (cairo_surface_status(temp_surf) == CAIRO_STATUS_SUCCESS) {
                app->cover = temp_surf;
            } else {
                cairo_surface_destroy(temp_surf);
            }
        }
    }
}

gboolean draw_bg(GtkWidget *widget, cairo_t *cr, gpointer data) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double r = 18.0;
    cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, 0.04, 0.04, 0.18, 0.96);
    cairo_pattern_add_color_stop_rgba(grad, 0.5, 0.02, 0.07, 0.14, 0.97);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, 0.01, 0.03, 0.09, 0.98);
    cairo_set_source(cr, grad);
    cairo_arc(cr, r, r, r, M_PI, 3*M_PI/2);
    cairo_arc(cr, width-r, r, r, 3*M_PI/2, 2*M_PI);
    cairo_arc(cr, width-r, height-r, r, 0, M_PI/2);
    cairo_arc(cr, r, height-r, r, M_PI/2, M_PI);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
    return FALSE;
}

gboolean draw_disc(GtkWidget *widget, cairo_t *cr, gpointer data) {
    App *app = (App*)data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double size = std::min(width, height);
    double cx = size / 2.0;
    double cy = size / 2.0;
    double r = size / 2.0 - 6.0;

    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_rotate(cr, app->angle);

    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_arc(cr, 4, 4, r, 0, 2*M_PI);
    cairo_fill(cr);

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

    cairo_set_line_width(cr, 0.7);
    for (int i = 0; i < 7; i++) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
        cairo_arc(cr, 0, 0, r * (0.38 + i*0.09), 0, 2*M_PI);
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, 0.06, 0.06, 0.06, 1);
    cairo_arc(cr, 0, 0, r*0.17, 0, 2*M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.64, 0.78, 0.22);
    cairo_arc(cr, 0, 0, r*0.07, 0, 2*M_PI);
    cairo_fill(cr);

    cairo_restore(cr);
    return TRUE;
}

gboolean on_tick(gpointer data) {
    App *app = (App*)data;
    
    if (app->mtx.try_lock()) {
        if (app->status_changed) {
            gtk_label_set_text(GTK_LABEL(app->lbl_title), app->next_title.c_str());
            gtk_label_set_text(GTK_LABEL(app->lbl_artist), app->next_artist.c_str());
            app->status_changed = false;
        }
        if (app->cover_changed) {
            load_cover_from_path(app, app->next_cover_path);
            app->cover_changed = false;
        }
        if (app->track_switched) {
            app->updating_time = true;
            gtk_range_set_range(GTK_RANGE(app->slider_time), 0, app->total_len);
            gtk_range_set_value(GTK_RANGE(app->slider_time), app->current_pos);
            app->updating_time = false;
            app->track_switched = false;
        }
        app->mtx.unlock();
    }

    if (app->spinning) {
        app->angle += 0.015;
        if (app->angle > 2*M_PI) app->angle -= 2*M_PI;
        
        if (app->current_pos < app->total_len) {
            app->current_pos += 0.016; 
            app->updating_time = true;
            gtk_range_set_value(GTK_RANGE(app->slider_time), app->current_pos);
            app->updating_time = false;
        }
        
        gtk_widget_queue_draw(app->disc_area);
    }
    return TRUE;
}

void playerctl_listener(App *app) {
    FILE *pipe = popen("playerctl metadata --format 'TITLE:{{title}}\nARTIST:{{artist}}\nURL:{{mpris:artUrl}}\nSTATUS:{{status}}\nLENGTH:{{mpris:length}}' --follow 2>/dev/null", "r");
    if (!pipe) return;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') line.pop_back();

        app->mtx.lock();
        if (line.rfind("TITLE:", 0) == 0) {
            std::string t = line.substr(6);
            if (t.empty()) t = "Нет трека";
            if (app->next_title != t) {
                app->next_title = t;
                app->status_changed = true;
                app->current_pos = 0.0;
                app->track_switched = true;
            }
        } else if (line.rfind("ARTIST:", 0) == 0) {
            app->next_artist = line.substr(7);
            app->status_changed = true;
        } else if (line.rfind("URL:", 0) == 0) {
            app->next_cover_path = line.substr(4);
            app->cover_changed = true;
        } else if (line.rfind("STATUS:", 0) == 0) {
            std::string stat = line.substr(7);
            bool old_spin = app->spinning;
            app->spinning = (stat == "Playing");
            
            if (app->spinning && !old_spin) {
                try {
                    std::string p_out = run_cmd("playerctl position 2>/dev/null");
                    if (!p_out.empty()) app->current_pos = std::stod(p_out);
                } catch(...) {}
                app->track_switched = true;
            }
        } else if (line.rfind("LENGTH:", 0) == 0) {
            try {
                app->total_len = std::stod(line.substr(7)) / 1000000.0;
                std::string p_out = run_cmd("playerctl position 2>/dev/null");
                if (!p_out.empty()) app->current_pos = std::stod(p_out);
                app->track_switched = true;
            } catch(...) {}
        }
        app->mtx.unlock();
    }
    pclose(pipe);
}

void on_time_changed(GtkRange *range, gpointer data) {
    App *app = (App*)data;
    if (app->updating_time) return;
    double val = gtk_range_get_value(range);
    app->current_pos = val; 
    std::string cmd = "playerctl position " + std::to_string(val) + " &";
    system(cmd.c_str());
}

// Позиционирование строго над нижней i3 панелью
void on_realize(GtkWidget *widget, gpointer data) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle geo;
    gdk_monitor_get_geometry(monitor, &geo);

    int win_w = 360;
    int win_h = 540;
    int x = geo.width - win_w - 15;
    int y = geo.height - win_h - 50; // 50px отступа, чтобы встать чётко над панелью
    gtk_window_move(GTK_WINDOW(widget), std::max(0, x), std::max(0, y));
}

void apply_css(GtkWidget *widget, const std::string& css_data) {
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, css_data.c_str(), -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

int main(int argc, char *argv[]) {
    // Парсим параметры запуска
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h" || arg == "-help" || arg == "--h") {
            std::cout << "======================================================\n";
            std::cout << "   Music Widget — Минималистичный плеер для панелей   \n";
            std::cout << "======================================================\n\n";
            std::cout << "Использование: " << argv[0] << " [ПАРАМЕТРЫ]\n\n";
            std::cout << "Доступные параметры запуска:\n";
            std::cout << "  -h, --help      Показать это справочное сообщение и выйти\n";
            std::cout << "  --swim          Включить режим плавающего окна (перетаскивание мышью)\n";
            std::cout << "  --alsa          Использовать amixer для управления звуком (ALSA/дефолт)\n";
            std::cout << "  --pipewire      Использовать wpctl (для PipeWire/WirePlumber окружений)\n";
            std::cout << "  --pulse         Использовать pactl (для PulseAudio систем)\n\n";
            std::cout << "Пример запуска:\n";
            std::cout << "  " << argv[0] << " --swim --pipewire\n";
            std::cout << "======================================================\n";
            return 0; 
        }
        
        if (arg == "--swim") {
            flag_swim = true;
        } else if (arg == "--alsa") {
            flag_driver = DRIVER_ALSA;
        } else if (arg == "--pipewire") {
            flag_driver = DRIVER_PIPEWIRE;
        } else if (arg == "--pulse") {
            flag_driver = DRIVER_PULSE;
        }
    }

    gtk_init(&argc, &argv);

    App app;
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 360, 540);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);
    gtk_widget_set_app_paintable(app.window, TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(app.window), TRUE);
    
    // Оставляем дефолтный DIALOG, чтобы i3 не трогал размеры
    gtk_window_set_type_hint(GTK_WINDOW(app.window), GDK_WINDOW_TYPE_HINT_DIALOG);

    const char* icon_path = "/storage/emulated/0/Pictures/1769553135868.png";
    if (FILE *f = fopen(icon_path, "r")) {
        fclose(f);
        gtk_window_set_icon_from_file(GTK_WINDOW(app.window), icon_path, nullptr);
    }

    GdkScreen *screen = gtk_widget_get_screen(app.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(app.window, visual);

    g_signal_connect(app.window, "draw", G_CALLBACK(draw_bg), nullptr);
    g_signal_connect(app.window, "realize", G_CALLBACK(on_realize), nullptr);
    
    // Принудительный выход без зависания
    g_signal_connect(app.window, "destroy", G_CALLBACK(force_quit), nullptr);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(on_delete), nullptr);

    // Биндим мышь только если передан флаг --swim
    if (flag_swim) {
        gtk_widget_add_events(app.window, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
        g_signal_connect(app.window, "button-press-event", G_CALLBACK(on_button_press), &app);
        g_signal_connect(app.window, "motion-notify-event", G_CALLBACK(on_button_move), &app);
    }

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), root);

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
    g_signal_connect(btn_x, "clicked", G_CALLBACK(force_quit), nullptr); // Наш жесткий выход
    apply_css(btn_x, "button { color: #888; background: transparent; border: none; font-size: 15px; } button:hover { color: #fff; }");
    gtk_box_pack_end(GTK_BOX(top), btn_x, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);

    app.lbl_artist = gtk_label_new("");
    gtk_widget_set_margin_top(app.lbl_artist, 2);
    gtk_widget_set_margin_start(app.lbl_artist, 18);
    gtk_widget_set_halign(app.lbl_artist, GTK_ALIGN_START);
    apply_css(app.lbl_artist, "label { color: #a4c639; font-size: 10px; }");
    gtk_box_pack_start(GTK_BOX(root), app.lbl_artist, FALSE, FALSE, 0);

    app.disc_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.disc_area, 200, 200);
    gtk_widget_set_halign(app.disc_area, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app.disc_area, 20);
    g_signal_connect(app.disc_area, "draw", G_CALLBACK(draw_disc), &app);
    gtk_box_pack_start(GTK_BOX(root), app.disc_area, FALSE, FALSE, 0);

    GtkWidget *ctrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(ctrl, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ctrl, 20);

    static const char* cmd_prev = "previous";
    static const char* cmd_play = "play-pause";
    static const char* cmd_next = "next";

    GtkWidget *b_prev = gtk_button_new_with_label("⏮");
    GtkWidget *b_play = gtk_button_new_with_label("⏯");
    GtkWidget *b_next = gtk_button_new_with_label("⏭");

    apply_css(b_prev, "button { background: rgba(164,198,57,0.12); border: 1px solid #a4c639; border-radius: 50%; color: #fff; font-size: 20px; min-width: 52px; min-height: 52px; padding: 0; } button:hover { background: rgba(164,198,57,0.3); }");
    apply_css(b_play, "button { background: rgba(164,198,57,0.12); border: 1px solid #a4c639; border-radius: 50%; color: #fff; font-size: 20px; min-width: 52px; min-height: 52px; padding: 0; } button:hover { background: rgba(164,198,57,0.3); }");
    apply_css(b_next, "button { background: rgba(164,198,57,0.12); border: 1px solid #a4c639; border-radius: 50%; color: #fff; font-size: 20px; min-width: 52px; min-height: 52px; padding: 0; } button:hover { background: rgba(164,198,57,0.3); }");

    g_signal_connect(b_prev, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_prev);
    g_signal_connect(b_play, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_play);
    g_signal_connect(b_next, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_next);

    gtk_box_pack_start(GTK_BOX(ctrl), b_prev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl), b_play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl), b_next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), ctrl, FALSE, FALSE, 0);

    // Таймлайн
    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(time_box, 24);
    gtk_widget_set_margin_end(time_box, 24);
    gtk_widget_set_margin_top(time_box, 20);

    GtkWidget *lbl_t_icon = gtk_label_new("⏳");
    gtk_box_pack_start(GTK_BOX(time_box), lbl_t_icon, FALSE, FALSE, 0);

    app.slider_time = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app.slider_time), FALSE);
    g_signal_connect(app.slider_time, "value-changed", G_CALLBACK(on_time_changed), &app);
    apply_css(app.slider_time, "scale trough { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 4px; } scale highlight { background: #39a4c6; border-radius: 4px; } scale slider { background: #fff; border-radius: 50%; min-width: 12px; min-height: 12px; }");
    gtk_box_pack_start(GTK_BOX(time_box), app.slider_time, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), time_box, FALSE, FALSE, 0);

    // Громкость
    GtkWidget *vol = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(vol, 24);
    gtk_widget_set_margin_end(vol, 24);
    gtk_widget_set_margin_top(vol, 14);
    gtk_widget_set_margin_bottom(vol, 20);

    GtkWidget *lbl_vol = gtk_label_new("🔊");
    gtk_box_pack_start(GTK_BOX(vol), lbl_vol, FALSE, FALSE, 0);

    app.slider_vol = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app.slider_vol), FALSE);
    gtk_range_set_value(GTK_RANGE(app.slider_vol), get_volume());
    g_signal_connect(app.slider_vol, "value-changed", G_CALLBACK(on_vol_changed), nullptr);
    apply_css(app.slider_vol, "scale trough { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 4px; } scale highlight { background: #a4c639; border-radius: 4px; } scale slider { background: #fff; border-radius: 50%; min-width: 14px; min-height: 14px; }");
    gtk_box_pack_start(GTK_BOX(vol), app.slider_vol, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), vol, FALSE, FALSE, 0);

    std::string init_status = run_cmd("playerctl status 2>/dev/null");
    app.spinning = (init_status == "Playing");
    if (app.spinning) {
        try {
            std::string len_out = run_cmd("playerctl metadata --format '{{mpris:length}}' 2>/dev/null");
            std::string pos_out = run_cmd("playerctl position 2>/dev/null");
            if (!len_out.empty()) app.total_len = std::stod(len_out) / 1000000.0;
            if (!pos_out.empty()) app.current_pos = std::stod(pos_out);
            app.track_switched = true;
        } catch(...) {}
    }

    std::thread listener(playerctl_listener, &app);
    listener.detach();

    gtk_widget_show_all(app.window);
    g_timeout_add(16, on_tick, &app);

    gtk_main();
    return 0;
}