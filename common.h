#ifndef COMMON_H
#define COMMON_H

#include <gtk/gtk.h>
#include <cairo.h>
#include <string>
#include <mutex>
#include <atomic>
#include <vector>

enum AudioDriver { DRIVER_ALSA, DRIVER_PIPEWIRE, DRIVER_PULSE };

extern bool        flag_swim;
extern AudioDriver flag_driver;
extern std::string g_player_id;

struct App {
    GtkWidget *window      = nullptr;
    GtkWidget *lbl_title   = nullptr;
    GtkWidget *lbl_artist  = nullptr;
    GtkWidget *disc_area   = nullptr;
    GtkWidget *slider_vol  = nullptr;
    GtkWidget *slider_time = nullptr;
    GtkWidget *btn_player  = nullptr;
    GtkWidget *revealer    = nullptr;
    GtkWidget *player_list = nullptr;
    GtkWidget *btn_prev    = nullptr;
    GtkWidget *btn_play    = nullptr;
    GtkWidget *btn_next    = nullptr;

    std::string theme_name       = "dark";
    std::string bg_path          = "";
    std::string audio_driver     = "alsa";
    std::string label_prev_text  = "<";
    std::string label_play_text  = "▶";
    std::string label_next_text  = ">";

    double angle    = 0.0;
    bool   spinning = false;
    cairo_surface_t *cover    = nullptr;
    cairo_surface_t *bg_image = nullptr;

    double current_pos = 0.0;
    double total_len   = 0.0;
    std::atomic<bool> updating_time{false};

    std::mutex  mtx;
    std::string next_title      = "No track";
    std::string next_artist;
    std::string next_cover_path;
    bool status_changed  = false;
    bool cover_changed   = false;
    bool track_switched  = false;
    bool players_changed = false;
    bool spinning_next   = false;
    bool spinning_changed = false;
    std::vector<std::string> next_players;

    gint win_start_x  = 0;
    gint win_start_y  = 0;
    gint drag_start_x = 0;
    gint drag_start_y = 0;
};

void load_config(App *app);
void save_config(App *app);
std::string run_cmd(const std::string& cmd);
std::vector<std::string> get_players();
double get_volume();
void on_vol_changed(GtkRange *range, gpointer data);
std::string resolve_cover_path(const std::string &path);
cairo_surface_t* load_image(const std::string &path);
void reload_cover(App *app, const std::string &path);
void reload_bg(App *app);
void playerctl_listener(App *app);
void spin_sync_thread(App *app);
void playerctl_async(const std::string &subcmd);
void on_btn_clicked(GtkWidget *widget, gpointer data);
void on_time_changed(GtkRange *range, gpointer data);
void build_ui(App *app);
void apply_css(GtkWidget *widget, const char *css);
gboolean draw_bg(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean draw_disc(GtkWidget *widget, cairo_t *cr, gpointer data);
void force_quit(GtkWidget *widget, gpointer data);
void show_settings_dialog(App *app);
gboolean on_tick(gpointer data);
void on_player_toggle(GtkWidget *widget, gpointer data);
void rebuild_player_list(App *app, const std::vector<std::string> &players);

#endif
