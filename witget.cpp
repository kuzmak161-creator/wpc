#include "common.h"
#include <cairo.h>
#include <cmath>

void apply_css(GtkWidget *widget, const char *css) {
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, css, -1, nullptr);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(widget),
        GTK_STYLE_PROVIDER(p),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(p);
}

gboolean draw_bg(GtkWidget *widget, cairo_t *cr, gpointer data) {
    App *app = (App*)data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    double r = 18.0;

    cairo_arc(cr, r, r, r, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, w - r, r, r, 3 * M_PI / 2, 2 * M_PI);
    cairo_arc(cr, w - r, h - r, r, 0, M_PI / 2);
    cairo_arc(cr, r, h - r, r, M_PI / 2, M_PI);
    cairo_close_path(cr);
    cairo_clip(cr);

    if (app->bg_image) {
        int sw = cairo_image_surface_get_width(app->bg_image);
        int sh = cairo_image_surface_get_height(app->bg_image);
        double scale = std::max((double)w / sw, (double)h / sh);
        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, app->bg_image, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
        cairo_paint(cr);
    } else {
        cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, 0, h);
        cairo_pattern_add_color_stop_rgba(grad, 0.0, 0.04, 0.04, 0.18, 0.96);
        cairo_pattern_add_color_stop_rgba(grad, 0.5, 0.02, 0.07, 0.14, 0.97);
        cairo_pattern_add_color_stop_rgba(grad, 1.0, 0.01, 0.03, 0.09, 0.98);
        cairo_set_source(cr, grad);
        cairo_paint(cr);
        cairo_pattern_destroy(grad);
    }
    return FALSE;
}

gboolean draw_disc(GtkWidget *widget, cairo_t *cr, gpointer data) {
    App *app = (App*)data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    double size = std::min(w, h);
    double cx = size / 2.0;
    double cy = size / 2.0;
    double r = size / 2.0 - 6.0;

    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_rotate(cr, app->angle);

    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_arc(cr, 4, 4, r, 0, 2 * M_PI);
    cairo_fill(cr);

    if (app->cover && cairo_surface_status(app->cover) == CAIRO_STATUS_SUCCESS) {
        cairo_save(cr);
        cairo_arc(cr, 0, 0, r, 0, 2 * M_PI);
        cairo_clip(cr);
        int sw = cairo_image_surface_get_width(app->cover);
        int sh = cairo_image_surface_get_height(app->cover);
        double scale = (2.0 * r) / std::min(sw, sh);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, app->cover, -sw / 2.0, -sh / 2.0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_pattern_t *grad = cairo_pattern_create_radial(-r * 0.3, -r * 0.3, 0, 0, 0, r);
        cairo_pattern_add_color_stop_rgb(grad, 0.0, 0.15, 0.25, 0.55);
        cairo_pattern_add_color_stop_rgb(grad, 0.6, 0.05, 0.10, 0.28);
        cairo_pattern_add_color_stop_rgb(grad, 1.0, 0.02, 0.04, 0.12);
        cairo_set_source(cr, grad);
        cairo_arc(cr, 0, 0, r, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
    }

    cairo_set_line_width(cr, 0.7);
    for (int i = 0; i < 7; i++) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
        cairo_arc(cr, 0, 0, r * (0.38 + i * 0.09), 0, 2 * M_PI);
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, 0.06, 0.06, 0.06, 1);
    cairo_arc(cr, 0, 0, r * 0.17, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.64, 0.78, 0.22);
    cairo_arc(cr, 0, 0, r * 0.07, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_restore(cr);
    return TRUE;
}

void build_ui(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), root);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(top, 14);
    gtk_widget_set_margin_start(top, 18);
    gtk_widget_set_margin_end(top, 14);

    app->lbl_title = gtk_label_new("No track");
    gtk_widget_set_halign(app->lbl_title, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(app->lbl_title), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(app->lbl_title), 24);
    apply_css(app->lbl_title, "label { color: #fff; font-size: 13px; font-weight: bold; }");
    gtk_box_pack_start(GTK_BOX(top), app->lbl_title, TRUE, TRUE, 0);

    app->btn_player = gtk_button_new_with_label("🎵");
    GtkWidget *btn_set = gtk_button_new_with_label("⚙");
    GtkWidget *btn_x = gtk_button_new_with_label("✕");
    const char *tbtn_css = "button { color: #888; background: transparent; border: none; font-size: 14px; padding: 0 4px; } button:hover { color: #fff; }";
    apply_css(app->btn_player, tbtn_css);
    apply_css(btn_set, tbtn_css);
    apply_css(btn_x, tbtn_css);

    g_signal_connect(btn_x, "clicked", G_CALLBACK(force_quit), nullptr);
    g_signal_connect(btn_set, "clicked", G_CALLBACK(show_settings_dialog), app);
    g_signal_connect(app->btn_player, "clicked", G_CALLBACK(on_player_toggle), app);

    gtk_box_pack_start(GTK_BOX(top), app->btn_player, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_set, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_x, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);

    app->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(app->revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(app->revealer), 200);
    app->player_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(app->player_list, 4);
    apply_css(app->player_list, "box { background: rgba(10,10,30,0.85); border-radius: 6px; padding: 4px; }");
    gtk_container_add(GTK_CONTAINER(app->revealer), app->player_list);
    gtk_box_pack_start(GTK_BOX(root), app->revealer, FALSE, FALSE, 0);

    app->lbl_artist = gtk_label_new("");
    gtk_widget_set_margin_top(app->lbl_artist, 2);
    gtk_widget_set_margin_start(app->lbl_artist, 18);
    gtk_widget_set_halign(app->lbl_artist, GTK_ALIGN_START);
    apply_css(app->lbl_artist, "label { color: #a4c639; font-size: 10px; }");
    gtk_box_pack_start(GTK_BOX(root), app->lbl_artist, FALSE, FALSE, 0);

    app->disc_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->disc_area, 200, 200);
    gtk_widget_set_halign(app->disc_area, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app->disc_area, 20);
    g_signal_connect(app->disc_area, "draw", G_CALLBACK(draw_disc), app);
    gtk_box_pack_start(GTK_BOX(root), app->disc_area, FALSE, FALSE, 0);

    GtkWidget *ctrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(ctrl, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ctrl, 20);

    static const char *cmd_prev = "previous";
    static const char *cmd_play = "play-pause";
    static const char *cmd_next = "next";

    // Простые ASCII-кнопки (без смайликов)
    app->btn_prev = gtk_button_new_with_label("<");
    app->btn_play = gtk_button_new_with_label("▶");
    app->btn_next = gtk_button_new_with_label(">");

    const char *btn_css =
        "button { background: rgba(164,198,57,0.12); border: 1px solid #a4c639;"
        " border-radius: 50%; color: #fff; font-size: 20px;"
        " min-width: 52px; min-height: 52px; padding: 0; }"
        "button:hover { background: rgba(164,198,57,0.3); }";
    apply_css(app->btn_prev, btn_css);
    apply_css(app->btn_play, btn_css);
    apply_css(app->btn_next, btn_css);

    g_signal_connect(app->btn_prev, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_prev);
    g_signal_connect(app->btn_play, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_play);
    g_signal_connect(app->btn_next, "clicked", G_CALLBACK(on_btn_clicked), (gpointer)cmd_next);

    gtk_box_pack_start(GTK_BOX(ctrl), app->btn_prev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl), app->btn_play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl), app->btn_next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), ctrl, FALSE, FALSE, 0);

    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(time_box, 24);
    gtk_widget_set_margin_end(time_box, 24);
    gtk_widget_set_margin_top(time_box, 18);
    gtk_box_pack_start(GTK_BOX(time_box), gtk_label_new("⏳"), FALSE, FALSE, 0);

    app->slider_time = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app->slider_time), FALSE);
    g_signal_connect(app->slider_time, "value-changed", G_CALLBACK(on_time_changed), app);
    apply_css(app->slider_time,
        "scale trough { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 4px; }"
        "scale highlight { background: #39a4c6; border-radius: 4px; }"
        "scale slider { background: #fff; border-radius: 50%; min-width: 12px; min-height: 12px; }");
    gtk_box_pack_start(GTK_BOX(time_box), app->slider_time, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), time_box, FALSE, FALSE, 0);

    GtkWidget *vol_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(vol_box, 24);
    gtk_widget_set_margin_end(vol_box, 24);
    gtk_widget_set_margin_top(vol_box, 10);
    gtk_widget_set_margin_bottom(vol_box, 14);
    gtk_box_pack_start(GTK_BOX(vol_box), gtk_label_new("🔊"), FALSE, FALSE, 0);

    app->slider_vol = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app->slider_vol), FALSE);
    gtk_range_set_value(GTK_RANGE(app->slider_vol), get_volume());
    g_signal_connect(app->slider_vol, "value-changed", G_CALLBACK(on_vol_changed), nullptr);
    apply_css(app->slider_vol,
        "scale trough { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 4px; }"
        "scale highlight { background: #a4c639; border-radius: 4px; }"
        "scale slider { background: #fff; border-radius: 50%; min-width: 14px; min-height: 14px; }");
    gtk_box_pack_start(GTK_BOX(vol_box), app->slider_vol, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), vol_box, FALSE, FALSE, 0);
}



void rebuild_player_list(App *app, const std::vector<std::string> &players) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(app->player_list));
    for (GList *it = children; it; it = it->next)
        gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);

    for (const auto &name : players) {
        GtkWidget *btn = gtk_button_new_with_label(name.c_str());
        apply_css(btn, "button { background: rgba(255,255,255,0.05); border: none; color: #c6c6c6; font-size: 11px; padding: 6px 12px; border-radius: 4px; margin: 1px; } button:hover { background: rgba(164,198,57,0.2); color: #fff; }");
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkWidget *w, gpointer d) {
            App *app = (App*)d;
            g_player_id = gtk_button_get_label(GTK_BUTTON(w));
        }), app);
        gtk_box_pack_start(GTK_BOX(app->player_list), btn, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(app->player_list);
}
