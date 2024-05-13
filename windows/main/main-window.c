#include "main-window.h"
#include <glib/gi18n.h>
#include "ezp_chips_data_file.h"
#include "chips_data_repository.h"
#include "utilities.h"
#include "gtk_string_list_extension.h"
#include <ezp_prog.h>
#include <ezp_errors.h>

struct _WindowMain {
    AdwApplicationWindow parent_instance;

    GtkWidget *color_scheme_button;
    GtkDrawingArea *hex_widget;
    GtkScrollbar *scroll_bar;
    GtkButton *test_button;
    GtkButton *erase_button;
    GtkButton *read_button;
    GtkButton *write_button;
    GtkProgressBar *progress_bar;
    GtkButton *status_icon;
    GtkDropDown *flash_type_selector;
    GtkDropDown *flash_manufacturer_selector;
    GtkDropDown *flash_name_selector;
    GtkDropDown *speed_selector;
    GtkDropDown *flash_size_selector;
    GtkSpinButton *delay_selector;
    GtkSearchEntry *chip_search_entry;

    uint8_t *hex_buffer;
    long hex_buffer_size;
    long viewer_offset;
    int lines_on_screen_count;
    double scroll;
    uint32_t hex_cursor;
    gboolean nibble;

    GTree *models_for_manufacturer_selector;
    GTree *models_for_name_selector;
    gboolean dropdowns_setup_completed;
    char selected_chip_type[48];
    char selected_chip_manuf[48];
    char selected_chip_name[48];

    ChipsDataRepository *repo;
    ezp_programmer *programmer;
};

G_DEFINE_FINAL_TYPE(WindowMain, window_main, ADW_TYPE_APPLICATION_WINDOW)

static GQuark domain_gquark;

static char *
get_color_scheme_icon_name(G_GNUC_UNUSED gpointer user_data, gboolean dark) {
    return g_strdup(dark ? "light-mode-symbolic" : "dark-mode-symbolic");
}

static void
color_scheme_button_clicked_cb(G_GNUC_UNUSED WindowMain *self) {
    AdwStyleManager *manager = adw_style_manager_get_default();

    if (adw_style_manager_get_dark(manager))
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
    else
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_FORCE_DARK);
}

static void
notify_system_supports_color_schemes_cb(WindowMain *self) {
    AdwStyleManager *manager = adw_style_manager_get_default();
    gboolean supports = adw_style_manager_get_system_supports_color_schemes(manager);

    gtk_widget_set_visible(self->color_scheme_button, !supports);

    if (supports)
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_DEFAULT);
}

#define BYTES_PER_LINE 16
#define GAP_POSITION ((BYTES_PER_LINE / 2) - 1)
#define FONT_SIZE 16
#define HEX_SPACING (char_width * 2.5)
//#define TEXT_SPACING (FONT_SIZE * 0.7)
#define GAP (char_width * 2)
#define HEX_BLOCK_X_OFFSET 105
//#define TEXT_BLOCK_X_OFFSET (HEX_BLOCK_X_OFFSET + 430)

static void
hex_widget_draw_function(GtkDrawingArea *area, cairo_t *cr, G_GNUC_UNUSED int width, int height, gpointer data) {
    GdkRGBA color;
    GdkRGBA cursor_color_focused = {
            .red = 0,
            .green = 0,
            .blue = 1,
            .alpha = 1
    };
    GdkRGBA cursor_color_unfocused = {
            .red = 0.5f,
            .green = 0.5f,
            .blue = 0.8f,
            .alpha = 1
    };
    GdkRGBA nibble_color_focused = {
            .red = 0,
            .green = 0,
            .blue = 1 * 0.5f,
            .alpha = 1
    };
    GdkRGBA nibble_color_unfocused = {
            .red = 0.5f * 0.5f,
            .green = 0.5f * 0.5f,
            .blue = 0.8f * 0.5f,
            .alpha = 1
    };
    WindowMain *wm = EZP_WINDOW_MAIN(data);

    gtk_widget_get_color(GTK_WIDGET(area), &color);
    gdk_cairo_set_source_rgba(cr, &color);
    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, FONT_SIZE);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, "F", &extents);
    double char_width = extents.x_advance;

    char buffer[10];
    wm->lines_on_screen_count = height / FONT_SIZE;
    long i = wm->viewer_offset * BYTES_PER_LINE;
    for (int y = 0; y < wm->lines_on_screen_count; ++y) {

        //line numbers
        sprintf(buffer, "%08lX", i & 0xffffffff);
        cairo_move_to(cr, 0, y * FONT_SIZE + FONT_SIZE);
        cairo_show_text(cr, buffer);

        for (int x = 0; x < BYTES_PER_LINE; ++x) {
            //cursor
            if (i == wm->hex_cursor) {
                //nibble
                gdk_cairo_set_source_rgba(cr, gtk_widget_has_focus(&area->widget) ? &nibble_color_focused
                                                                                  : &nibble_color_unfocused);
                cairo_rectangle(cr, (wm->nibble ? char_width : 0) + HEX_BLOCK_X_OFFSET + x * HEX_SPACING +
                                    (x > GAP_POSITION ? GAP : 0), y * FONT_SIZE + FONT_SIZE + 2, char_width,
                                -FONT_SIZE);
                cairo_fill(cr);

                //frame
                cairo_set_line_width(cr, gtk_widget_has_focus(&area->widget) ? 2 : 1);
                gdk_cairo_set_source_rgba(cr, gtk_widget_has_focus(&area->widget) ? &cursor_color_focused
                                                                                  : &cursor_color_unfocused);
                cairo_rectangle(cr, HEX_BLOCK_X_OFFSET + x * HEX_SPACING + (x > GAP_POSITION ? GAP : 0),
                                y * FONT_SIZE + FONT_SIZE + 2, char_width * 2, -FONT_SIZE);
                cairo_stroke(cr);

                gdk_cairo_set_source_rgba(cr, &color);
            }

            //hex
            sprintf(buffer, "%02X", wm->hex_buffer[i] & 0xff);
            cairo_move_to(cr, HEX_BLOCK_X_OFFSET + x * HEX_SPACING + (x > GAP_POSITION ? GAP : 0),
                          y * FONT_SIZE + FONT_SIZE);
            cairo_show_text(cr, buffer);

            i++;
            if (i == wm->hex_buffer_size) break;
        }
        if (i == wm->hex_buffer_size) break;
    }
}

static gboolean
hex_widget_scroll_cb(GtkEventControllerScroll *self, G_GNUC_UNUSED gdouble dx, gdouble dy, gpointer user_data) {
    WindowMain *wm = EZP_WINDOW_MAIN(user_data);

    wm->scroll += dy;
    if (gtk_event_controller_scroll_get_unit(self) == GDK_SCROLL_UNIT_SURFACE) {
        if (fabs(wm->scroll) >= FONT_SIZE) {
            long prev_viewer_offset = wm->viewer_offset;

            while (fabs(wm->scroll) >= FONT_SIZE) {
                wm->viewer_offset += wm->scroll > 0 ? 1 : -1;
                if (wm->scroll > 0) wm->scroll -= FONT_SIZE;
                else if (wm->scroll < 0) wm->scroll += FONT_SIZE;
            }

            if (wm->viewer_offset < 0) wm->viewer_offset = 0;
            long upperBound =
                    wm->hex_buffer_size / BYTES_PER_LINE - (wm->hex_buffer_size % BYTES_PER_LINE == 0 ? 1 : 0);
            if (wm->viewer_offset > upperBound) wm->viewer_offset = upperBound;

            if (wm->viewer_offset != prev_viewer_offset) {
                gtk_widget_queue_draw(GTK_WIDGET(wm->hex_widget));
                gtk_adjustment_set_value(gtk_scrollbar_get_adjustment(wm->scroll_bar), (uint32_t) wm->viewer_offset);
            }
        }
    } else {
        long prev_viewer_offset = wm->viewer_offset;

        wm->viewer_offset += wm->scroll > 0 ? 1 : -1;
        wm->scroll = 0;

        if (wm->viewer_offset < 0) wm->viewer_offset = 0;
        long upperBound = wm->hex_buffer_size / BYTES_PER_LINE - (wm->hex_buffer_size % BYTES_PER_LINE == 0 ? 1 : 0);
        if (wm->viewer_offset > upperBound) wm->viewer_offset = upperBound;

        if (wm->viewer_offset != prev_viewer_offset) {
            gtk_widget_queue_draw(GTK_WIDGET(wm->hex_widget));
            gtk_adjustment_set_value(gtk_scrollbar_get_adjustment(wm->scroll_bar), (uint32_t) wm->viewer_offset);
        }
    }
    return TRUE;
}

static void
hex_widget_scroll_begin_cb(G_GNUC_UNUSED GtkEventControllerScroll *self, gpointer user_data) {
    EZP_WINDOW_MAIN(user_data)->scroll = 0;
}

static void
scroll_bar_value_changed_cb(GtkAdjustment *adjustment, gpointer user_data) {
    WindowMain *wm = EZP_WINDOW_MAIN(user_data);

    long prev_viewer_offset = wm->viewer_offset;
    wm->viewer_offset = (uint32_t) gtk_adjustment_get_value(adjustment);

    if (prev_viewer_offset != wm->viewer_offset) {
        EZP_WINDOW_MAIN(user_data)->scroll = 0;
        gtk_widget_queue_draw(GTK_WIDGET(wm->hex_widget));
    }
}

static void
window_main_set_buttons_sensitive(WindowMain *self, gboolean sensitive) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->test_button), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->erase_button), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->read_button), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->write_button), sensitive);
}

static void
window_main_set_selectors_sensitive(WindowMain *self, gboolean sensitive) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->flash_type_selector), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->flash_manufacturer_selector), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->flash_name_selector), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->speed_selector), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->flash_size_selector), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(self->delay_selector), sensitive);
}

static void
chip_test_task_func(GTask *task, gpointer source_object, G_GNUC_UNUSED gpointer task_data,
                    G_GNUC_UNUSED GCancellable *cancellable) {
    WindowMain *wm = EZP_WINDOW_MAIN(source_object);
    ezp_flash type;
    uint32_t chip_id;
    int res = ezp_test_flash(wm->programmer, &type, &chip_id);
    char *message;
    switch (res) {
        case EZP_OK:
            switch (type) {
                case SPI_FLASH:
                    message = g_strdup_printf(gettext("Flash type is SPI_FLASH, Chip ID: 0x%x"), chip_id);
                    break;
                case EEPROM_24:
                    message = g_strdup_printf(gettext("Flash type is EEPROM_24"));
                    break;
                case EEPROM_93:
                    message = g_strdup_printf(gettext("Flash type is EEPROM_93"));
                    break;
                case EEPROM_25:
                    message = g_strdup_printf(gettext("Flash type is EEPROM_25"));
                    break;
                case EEPROM_95:
                    message = g_strdup_printf(gettext("Flash type is EEPROM_95"));
                    break;
            }
            break;
        case EZP_FLASH_NOT_DETECTED:
            message = g_strdup_printf("Flash not detected");
            break;
        case EZP_INVALID_DATA_FROM_PROGRAMMER:
            message = g_strdup_printf(gettext("Invalid data from programmer"));
            break;
        case EZP_LIBUSB_ERROR:
            message = g_strdup_printf(gettext("libusb error"));
            break;
        default:
            message = g_strdup_printf(gettext("Unknown error: %d"), res);
            break;
    }
    g_task_return_pointer(task, message, NULL);
    g_object_unref(task);
}

static void
chip_test_task_result_cb(GObject *source_object, GAsyncResult *res, G_GNUC_UNUSED gpointer user_data) {
    GError *error = NULL;
    char *message = g_task_propagate_pointer(G_TASK(res), &error);

    AdwDialog *dlg = adw_alert_dialog_new(gettext("Test result"), message ? message : error->message);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "OK", gettext("OK"));
    adw_dialog_present(dlg, GTK_WIDGET(source_object));
    window_main_set_buttons_sensitive(EZP_WINDOW_MAIN(source_object), true);

    if (message) free(message);
    if (error) g_error_free(error);
}

static void chip_test_task_start(WindowMain *self) {
    window_main_set_buttons_sensitive(self, false);
    GTask *task = g_task_new(self, NULL, chip_test_task_result_cb, NULL);
    g_task_set_task_data(task, NULL, NULL);
    g_task_run_in_thread(task, chip_test_task_func);
}

// READ
static void
chip_read_progress_cb_gtk_thread(gpointer user_data) {
    uint64_t *dat = user_data;
    WindowMain *wm = (void *) dat[0];
    gtk_progress_bar_set_fraction(wm->progress_bar, *(double *) &dat[1]);
    free(user_data);
}

static void
chip_read_progress_cb(uint32_t current, uint32_t max, void *user_data) {
    uint64_t *dat = malloc(sizeof(uint64_t) * 2);
    dat[0] = (uint64_t) user_data;
    *(double *) &dat[1] = (double) current / (double) max;

    g_idle_add_once(chip_read_progress_cb_gtk_thread, dat);
}

static void
chip_read_task_func(GTask *task, gpointer source_object, G_GNUC_UNUSED gpointer task_data,
                    G_GNUC_UNUSED GCancellable *cancellable) {
    WindowMain *wm = EZP_WINDOW_MAIN(source_object);

    gboolean chip_found = false;
    ezp_chip_data chip_data;
    char sprintf_buf[48];
    //warning about snprintf is ok. just ignore it
    snprintf(sprintf_buf, 48, "%s,%s,%s", wm->selected_chip_type, wm->selected_chip_manuf, wm->selected_chip_name);
    chips_list chips = chips_data_repository_get_chips(wm->repo);
    for (int i = 0; i < chips.length; ++i) {
        if (!strcmp(chips.data[i].name, sprintf_buf)) {
            chip_data = chips.data[i];
            chip_found = true;
            break;
        }
    }
    if (!chip_found) {
        printf("Data not found!\n");
        g_task_return_new_error(task, domain_gquark, (45 << 16) | 45, "CHIP_NOT_FOUND");
        g_object_unref(task);
        return;
    }

    ezp_speed speed = gtk_drop_down_get_selected(wm->speed_selector);

    printf("reading with chip: %s; speed: %d\n", chip_data.name, speed);
    uint8_t *data;
    int ret = ezp_read_flash(wm->programmer, &data, &chip_data, speed, chip_read_progress_cb, wm);
    uint64_t *dat;
    switch (ret) {
        case EZP_OK:
            dat = g_malloc(sizeof(uint64_t) * 2);
            dat[0] = (uint64_t) data;
            dat[1] = chip_data.flash;
            printf("OK\n");
            g_task_return_pointer(task, dat, NULL);
            break;
        case EZP_FLASH_SIZE_OR_PAGE_INVALID:
            printf("EZP_FLASH_SIZE_OR_PAGE_INVALID\n");
            g_task_return_new_error(task, domain_gquark, (45 << 16) | ret, "EZP_FLASH_SIZE_OR_PAGE_INVALID");
            g_object_unref(task);
            break;
        case EZP_LIBUSB_ERROR:
            printf("EZP_LIBUSB_ERROR\n");
            g_task_return_new_error(task, domain_gquark, (45 << 16) | ret, "EZP_LIBUSB_ERROR");
            g_object_unref(task);
            break;
        default:
            printf("Unknown error: %d\n", ret);
            g_task_return_new_error(task, domain_gquark, (45 << 16) | ret, "UNKNOWN_ERROR");
            g_object_unref(task);
            break;
    }
    g_object_unref(task);
}

static void
chip_read_task_result_cb(GObject *source_object, GAsyncResult *res, G_GNUC_UNUSED gpointer user_data) {
    WindowMain *wm = EZP_WINDOW_MAIN(source_object);

    GError *error = NULL;
    uint64_t *dat = g_task_propagate_pointer(G_TASK(res), &error);

    if (!error) {
        if (wm->hex_buffer) free(wm->hex_buffer);
        wm->hex_buffer = (uint8_t *) dat[0];
        wm->hex_buffer_size = (uint32_t) dat[1];
        gtk_widget_queue_draw(&wm->hex_widget->widget);
        wm->viewer_offset = 0;
        wm->hex_cursor = 0;
        wm->nibble = false;
        GtkAdjustment *scroll_adj = gtk_scrollbar_get_adjustment(wm->scroll_bar);
        gtk_adjustment_set_upper(scroll_adj, (uint32_t)(wm->hex_buffer_size / BYTES_PER_LINE) +
                                             (wm->hex_buffer_size % BYTES_PER_LINE == 0 ? 0 : 1));
        gtk_adjustment_set_value(scroll_adj, (uint32_t) wm->viewer_offset);
    }

    AdwDialog *dlg = adw_alert_dialog_new(error ? gettext("Error") : gettext("Success"), error ? error->message : "OK");
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "OK", gettext("OK"));
    adw_dialog_present(dlg, GTK_WIDGET(source_object));

    gtk_widget_set_visible(GTK_WIDGET(EZP_WINDOW_MAIN(source_object)->progress_bar), false);
    window_main_set_buttons_sensitive(EZP_WINDOW_MAIN(source_object), true);
    window_main_set_selectors_sensitive(EZP_WINDOW_MAIN(source_object), true);

    if (dat) free(dat);
    if (error) g_error_free(error);
}

static void chip_read_task_start(WindowMain *self) {
    window_main_set_buttons_sensitive(self, false);
    window_main_set_selectors_sensitive(self, false);

    gtk_progress_bar_set_fraction(self->progress_bar, 0);
    gtk_progress_bar_set_text(self->progress_bar, gettext("Reading..."));
    gtk_widget_set_visible(GTK_WIDGET(self->progress_bar), true);

    GTask *task = g_task_new(self, NULL, chip_read_task_result_cb, NULL);
    g_task_set_task_data(task, NULL, NULL);
    g_task_run_in_thread(task, chip_read_task_func);
}
// READ

static void
window_main_button_clicked_cb(GtkButton *self, gpointer user_data) {
    const char *name = gtk_widget_get_name(GTK_WIDGET(self));

    if (!strcmp(name, "test_button")) {
        printf("Test button clicked!\n");
        chip_test_task_start(user_data);
    } else if (!strcmp(name, "erase_button")) {
        printf("Erase button clicked!\n");
    } else if (!strcmp(name, "read_button")) {
        printf("Read button clicked!\n");
        chip_read_task_start(user_data);
    } else if (!strcmp(name, "write_button")) {
        printf("Write button clicked!\n");
    } else {
        g_warning("Unknown button clicked!");
    }
}

static void
status_indicator_ok(gpointer user_data) {
    printf("status_indicator_ok\n");
    WindowMain *wm = EZP_WINDOW_MAIN(user_data);
    if (!wm->programmer) {
        wm->programmer = ezp_find_programmer();
        if (wm->programmer) {
            gtk_button_set_icon_name(wm->status_icon, "status-ok");
            window_main_set_buttons_sensitive(wm, true);
        } else {
            g_warning("status_indicator_ok called, but ezp_find_programmer returned NULL");
        }
    }
}

static void
status_indicator_error(gpointer user_data) {
    printf("status_indicator_error\n");
    WindowMain *wm = EZP_WINDOW_MAIN(user_data);
    if (wm->programmer) {
        ezp_free_programmer(wm->programmer);
        wm->programmer = NULL;
        gtk_button_set_icon_name(EZP_WINDOW_MAIN(user_data)->status_icon, "status-error");
        window_main_set_buttons_sensitive(wm, false);
    }
}

static void
status_access_denied(gpointer user_data) {
    printf("status_access_denied\n");
    status_indicator_error(user_data);
    AdwDialog *dlg = adw_alert_dialog_new(gettext("No access to programmer"), gettext("Did you install udev rules?"));
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "OK", gettext("OK"));
    adw_dialog_present(dlg, GTK_WIDGET(user_data));
}

static void
programmer_status_callback(ezp_status status, void *user_data) {
    if (status == EZP_READY) g_idle_add_once(status_indicator_ok, user_data);
    else if (status == EZP_CONNECTED) g_idle_add_once(status_access_denied, user_data);
    else g_idle_add_once(status_indicator_error, user_data);
}

static void
programmer_status_task_func(GTask *task, G_GNUC_UNUSED gpointer source_object, gpointer task_data,
                            G_GNUC_UNUSED GCancellable *cancellable) {
    ezp_listen_programmer_status(programmer_status_callback, task_data);
    g_task_return_pointer(task, NULL, NULL);
}

static void
programmer_status_task_result_cb(G_GNUC_UNUSED GObject *source_object, G_GNUC_UNUSED GAsyncResult *res,
                                 G_GNUC_UNUSED gpointer user_data) {
    printf("Programmer status task has been stopped\n");
}

static void programmer_status_task_start(WindowMain *self) {
    GTask *task = g_task_new(NULL, NULL, programmer_status_task_result_cb, NULL);
    g_task_set_task_data(task, self, NULL);
    g_task_run_in_thread(task, programmer_status_task_func);
    g_object_unref(task);
}

static gint
compare_strings(gconstpointer a, gconstpointer b) {
    if (a == NULL && b == NULL) return 0;
    if (a == NULL || b == NULL) return -1;
    return strcmp(a, b);
}

static void
append_string_to_string_list(gpointer data, gpointer user_data) {
    gtk_string_list_append(GTK_STRING_LIST(user_data), data);
}

static gint
string_key_compare(gconstpointer a, gconstpointer b, G_GNUC_UNUSED gpointer user_data) {
    if (!a || !b) g_error("a or b is NULL");
    return strcmp(a, b);
}

static void
destroy_string(gpointer data) {
    free(data);
}

static void
destroy_strings_g_list(gpointer data) {
    g_list_free_full(data, destroy_string);
}

static void
chip_selected(WindowMain *wm, GtkStringObject *selected_type, GtkStringObject *selected_manuf,
              GtkStringObject *selected_name) {
    if (!selected_type || !selected_manuf || !selected_name) {
        wm->selected_chip_type[0] = '\0';
        wm->selected_chip_manuf[0] = '\0';
        wm->selected_chip_name[0] = '\0';
        return;
    }
    strlcpy(wm->selected_chip_type, gtk_string_object_get_string(selected_type), 48);
    strlcpy(wm->selected_chip_manuf, gtk_string_object_get_string(selected_manuf), 48);
    strlcpy(wm->selected_chip_name, gtk_string_object_get_string(selected_name), 48);

    printf("Flash selected: %s,%s,%s\n", wm->selected_chip_type, wm->selected_chip_manuf, wm->selected_chip_name);

    char sprintf_buf[48];
    //warning about snprintf is ok. just ignore it
    snprintf(sprintf_buf, 48, "%s,%s,%s", wm->selected_chip_type, wm->selected_chip_manuf, wm->selected_chip_name);
    ezp_chip_data *chip_data = chips_data_repository_find_chip(wm->repo, sprintf_buf);
    if (!chip_data) g_error("chip_data not found");

    wm->hex_buffer_size = chip_data->flash;
    wm->hex_buffer = g_realloc(wm->hex_buffer, wm->hex_buffer_size);
    memset(wm->hex_buffer, 0xff, wm->hex_buffer_size);

    gtk_widget_queue_draw(&wm->hex_widget->widget);
    wm->viewer_offset = 0;
    wm->hex_cursor = 0;
    wm->nibble = false;
    GtkAdjustment *scroll_adj = gtk_scrollbar_get_adjustment(wm->scroll_bar);
    gtk_adjustment_set_upper(scroll_adj, (uint32_t)(wm->hex_buffer_size / BYTES_PER_LINE) +
                                         (wm->hex_buffer_size % BYTES_PER_LINE == 0 ? 0 : 1));
    gtk_adjustment_set_value(scroll_adj, (uint32_t) wm->viewer_offset);
}

static void
chips_list_changed_cb(G_GNUC_UNUSED ChipsDataRepository *repo, chips_list *data, gpointer user_data) {
    ezp_chip_data *chips = data->data;
    WindowMain *wm = EZP_WINDOW_MAIN(user_data);
    wm->dropdowns_setup_completed = false;

    // STEP 1 - compute dropdown items
    // at first, we need to get list of types
    // then list of manufacturers for each type
    // then list of names for each manufacturer
    //
    // A GtkStringList is needed to dringList andisplay data in a GtkDropDown, but it
    // has not any "find_*" methods which we need to insert a new items
    // without duplicates. So, at first we need to use a GList, then
    // copy items to GtkStringList and delete(free) GList.
    GList *types = NULL;
    GTree *manufs = g_tree_new_full(string_key_compare, NULL, destroy_string,
                                    destroy_strings_g_list); //key is "type". stores lists with manufacturers for each type
    GTree *names = g_tree_new_full(string_key_compare, NULL, destroy_string,
                                   destroy_strings_g_list); //key is "type,manuf" stores lists with names for each manufacturer

    for (int i = 0; i < data->length; ++i) {
        char nm_cp[48];
        strlcpy(nm_cp, chips[i].name, 48);

        char *type;
        char *manuf;
        char *name;
        // replaces all comas in nm_cp by \0
        if (parseName(nm_cp, &type, &manuf, &name)) g_error("parseName");

        if (g_list_find_custom(types, type, compare_strings) == NULL) { // avoiding duplicates
            types = g_list_append(types, strdup(type));
        }

        gpointer ptr = g_tree_lookup(manufs, type);
        if (!ptr) {
            g_tree_insert(manufs, strdup(type), g_list_append(NULL, strdup(manuf)));
        } else {
            if (g_list_find_custom(ptr, manuf, compare_strings) == NULL) { // avoiding duplicates
                ptr = g_list_append(ptr, strdup(manuf));
            }
        }

        *(manuf - 1) = ','; //set comma between type and manuf instead of \0. now type is "type,manuf"

        ptr = g_tree_lookup(names, type);
        if (!ptr) {
            g_tree_insert(names, strdup(type), g_list_append(NULL, strdup(name)));
        } else {
            ptr = g_list_append(ptr, strdup(name));
        }
    }

    // copy manufacturers and free lists
    if (wm->models_for_manufacturer_selector != NULL) g_tree_destroy(wm->models_for_manufacturer_selector);
    wm->models_for_manufacturer_selector = g_tree_new_full(string_key_compare, NULL, destroy_string, g_object_unref);
    GTreeNode *node = g_tree_node_first(manufs);
    while (node) {
        GList *list = g_tree_node_value(node);
        GtkStringList *string_list = gtk_string_list_new(NULL);
        g_list_foreach(list, append_string_to_string_list, string_list);

        g_tree_insert(wm->models_for_manufacturer_selector, strdup(g_tree_node_key(node)), string_list);

        node = g_tree_node_next(node);
    }
    g_tree_destroy(manufs);

    // copy names and free lists
    if (wm->models_for_name_selector != NULL) g_tree_destroy(wm->models_for_name_selector);
    wm->models_for_name_selector = g_tree_new_full(string_key_compare, NULL, destroy_string, g_object_unref);
    node = g_tree_node_first(names);
    while (node) {
        GList *list = g_tree_node_value(node);
        GtkStringList *string_list = gtk_string_list_new(NULL);
        g_list_foreach(list, append_string_to_string_list, string_list);

        g_tree_insert(wm->models_for_name_selector, strdup(g_tree_node_key(node)), string_list);

        node = g_tree_node_next(node);
    }
    g_tree_destroy(names);

    // copy types and free temp list
    GtkStringList *types_model = gtk_string_list_new(NULL);
    g_list_foreach(types, append_string_to_string_list, types_model);
    destroy_strings_g_list(types);

    // STEP 2 - try to select previously selected items
    // find positions of previously selected items
    // set models for dropdowns
    // set selected items by its position

    // find position of previously selected type item
    int t_pos = gtk_string_list_index_of(types_model, wm->selected_chip_type);

    // find model for dropdown and position of previously selected manuf item
    GtkStringList *manufs_model = g_tree_lookup(wm->models_for_manufacturer_selector, wm->selected_chip_type);
    int m_pos = manufs_model ? gtk_string_list_index_of(manufs_model, wm->selected_chip_manuf) : -1;

    // find model for dropdown and position of previously selected name item
    char key[48];
    strlcpy(key, wm->selected_chip_type, 48);
    strlcat(key, ",", 48);
    strlcat(key, wm->selected_chip_manuf, 48);

    GtkStringList *names_model = g_tree_lookup(wm->models_for_name_selector, key);
    int n_pos = names_model ? gtk_string_list_index_of(names_model, wm->selected_chip_name) : -1;

    //set models
    gtk_drop_down_set_model(wm->flash_type_selector, G_LIST_MODEL(types_model));
    if (manufs_model) gtk_drop_down_set_model(wm->flash_manufacturer_selector, G_LIST_MODEL(manufs_model));
    if (names_model) gtk_drop_down_set_model(wm->flash_name_selector, G_LIST_MODEL(names_model));

    //set positions
    if (t_pos >= 0) gtk_drop_down_set_selected(wm->flash_type_selector, t_pos);
    if (m_pos >= 0) gtk_drop_down_set_selected(wm->flash_manufacturer_selector, m_pos);
    if (n_pos >= 0) gtk_drop_down_set_selected(wm->flash_name_selector, n_pos);
    wm->dropdowns_setup_completed = true;
    GtkStringObject *selected_type = gtk_drop_down_get_selected_item(wm->flash_type_selector);
    GtkStringObject *selected_manuf = gtk_drop_down_get_selected_item(wm->flash_manufacturer_selector);
    GtkStringObject *selected_name = gtk_drop_down_get_selected_item(wm->flash_name_selector);
    chip_selected(wm, selected_type, selected_manuf, selected_name);
}

static void
dropdown_selected_item_changed_cb(GtkDropDown *self, G_GNUC_UNUSED gpointer *new_value, gpointer user_data) {
    const char *name = gtk_widget_get_name(GTK_WIDGET(self));
    WindowMain *wm = EZP_WINDOW_MAIN(user_data);

    if (!strcmp(name, "flash_type_selector")) {
        GtkStringObject *selected_type = gtk_drop_down_get_selected_item(self);
        gtk_drop_down_set_model(wm->flash_manufacturer_selector,
                                g_tree_lookup(wm->models_for_manufacturer_selector,
                                              gtk_string_object_get_string(selected_type)));
    } else if (!strcmp(name, "flash_manufacturer_selector")) {
        GtkStringObject *selected_type = gtk_drop_down_get_selected_item(wm->flash_type_selector);
        GtkStringObject *selected_manuf = gtk_drop_down_get_selected_item(self);
        char key[48];
        strlcpy(key, gtk_string_object_get_string(selected_type), 48);
        strlcat(key, ",", 48);
        strlcat(key, gtk_string_object_get_string(selected_manuf), 48);

        gtk_drop_down_set_model(wm->flash_name_selector, g_tree_lookup(wm->models_for_name_selector, key));
    } else if (!strcmp(name, "flash_name_selector")) {
        GtkStringObject *selected_type = gtk_drop_down_get_selected_item(wm->flash_type_selector);
        GtkStringObject *selected_manuf = gtk_drop_down_get_selected_item(wm->flash_manufacturer_selector);
        GtkStringObject *selected_name = gtk_drop_down_get_selected_item(self);

        // dropdown_selected_item_changed_cb is called multiple times during filling dropdowns with data, so we should
        // use dropdowns_setup_completed flag to call chips_selected only once
        if (wm->dropdowns_setup_completed) chip_selected(wm, selected_type, selected_manuf, selected_name);
    } else {
        g_warning("Unknown selector!");
    }
}

static gboolean
hex_key_press_cb(G_GNUC_UNUSED GtkEventControllerKey *controller, guint keyval, G_GNUC_UNUSED guint keycode,
                 GdkModifierType state, gpointer user_data) {
    if (state != GDK_NO_MODIFIER_MASK) return GDK_EVENT_PROPAGATE;

    WindowMain *wm = EZP_WINDOW_MAIN(user_data);
    int input = -1;

    switch (keyval) {
        case GDK_KEY_Up:
            if (wm->hex_cursor < BYTES_PER_LINE) {
                wm->hex_cursor = 0;
            } else {
                wm->hex_cursor -= BYTES_PER_LINE;
            }
            wm->nibble = false;
            gtk_widget_queue_draw(&wm->hex_widget->widget);
            return GDK_EVENT_STOP;
        case GDK_KEY_Down:
            if (wm->hex_cursor + BYTES_PER_LINE >= wm->hex_buffer_size - 1) {
                wm->hex_cursor = wm->hex_buffer_size - 1;
            } else {
                wm->hex_cursor += BYTES_PER_LINE;
            }
            wm->nibble = false;
            gtk_widget_queue_draw(&wm->hex_widget->widget);
            return GDK_EVENT_STOP;
        case GDK_KEY_Left:
            wm->nibble = !wm->nibble;
            if (wm->hex_cursor && wm->nibble) wm->hex_cursor--;
            gtk_widget_queue_draw(&wm->hex_widget->widget);
            return GDK_EVENT_STOP;
        case GDK_KEY_Right:
            wm->nibble = !wm->nibble;
            if (wm->hex_cursor < wm->hex_buffer_size - 1 && !wm->nibble) wm->hex_cursor++;
            gtk_widget_queue_draw(&wm->hex_widget->widget);
            return GDK_EVENT_STOP;
        case GDK_KEY_0:
        case GDK_KEY_KP_0:
            input = 0;
            break;
        case GDK_KEY_1:
        case GDK_KEY_KP_1:
            input = 1;
            break;
        case GDK_KEY_2:
        case GDK_KEY_KP_2:
            input = 2;
            break;
        case GDK_KEY_3:
        case GDK_KEY_KP_3:
            input = 3;
            break;
        case GDK_KEY_4:
        case GDK_KEY_KP_4:
            input = 4;
            break;
        case GDK_KEY_5:
        case GDK_KEY_KP_5:
            input = 5;
            break;
        case GDK_KEY_6:
        case GDK_KEY_KP_6:
            input = 6;
            break;
        case GDK_KEY_7:
        case GDK_KEY_KP_7:
            input = 7;
            break;
        case GDK_KEY_8:
        case GDK_KEY_KP_8:
            input = 8;
            break;
        case GDK_KEY_9:
        case GDK_KEY_KP_9:
            input = 9;
            break;
        case GDK_KEY_A:
        case GDK_KEY_a:
            input = 0xa;
            break;
        case GDK_KEY_B:
        case GDK_KEY_b:
            input = 0xb;
            break;
        case GDK_KEY_C:
        case GDK_KEY_c:
            input = 0xc;
            break;
        case GDK_KEY_D:
        case GDK_KEY_d:
            input = 0xd;
            break;
        case GDK_KEY_E:
        case GDK_KEY_e:
            input = 0xe;
            break;
        case GDK_KEY_F:
        case GDK_KEY_f:
            input = 0xf;
            break;
        default:
            return GDK_EVENT_PROPAGATE;
    }

    if (input >= 0) {
        wm->hex_buffer[wm->hex_cursor] &= wm->nibble ? 0xf0 : 0x0f;
        wm->hex_buffer[wm->hex_cursor] |= wm->nibble ? (input & 0xf) : (input << 4) & 0xf0;

        wm->nibble = !wm->nibble;
        if (wm->hex_cursor < wm->hex_buffer_size - 1 && !wm->nibble) wm->hex_cursor++;
        gtk_widget_queue_draw(&wm->hex_widget->widget);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void
hex_pressed_cb(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    WindowMain *wm = EZP_WINDOW_MAIN(user_data);
    gtk_widget_grab_focus(&wm->hex_widget->widget);
    printf("pressed\n");
}

static void
window_main_finalize(GObject *gobject) {
    WindowMain *wm = EZP_WINDOW_MAIN(gobject);
    g_free(wm->hex_buffer);
    if (wm->programmer) ezp_free_programmer(wm->programmer);
    if (wm->models_for_manufacturer_selector != NULL) g_tree_destroy(wm->models_for_manufacturer_selector);
    if (wm->models_for_name_selector != NULL) g_tree_destroy(wm->models_for_name_selector);
    G_OBJECT_CLASS(window_main_parent_class)->finalize(gobject);
}

static void
window_main_class_init(WindowMainClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = window_main_finalize;

    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_q, GDK_CONTROL_MASK, "window.close", NULL);

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/dev/alexandro45/ezp2023plus/ui/windows/main/main-window.ui");
    gtk_widget_class_bind_template_child(widget_class, WindowMain, color_scheme_button);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, hex_widget);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, scroll_bar);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, test_button);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, erase_button);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, read_button);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, write_button);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, progress_bar);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, status_icon);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, flash_type_selector);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, flash_manufacturer_selector);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, flash_name_selector);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, speed_selector);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, flash_size_selector);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, delay_selector);
    gtk_widget_class_bind_template_child(widget_class, WindowMain, chip_search_entry);

    gtk_widget_class_bind_template_callback(widget_class, get_color_scheme_icon_name);
    gtk_widget_class_bind_template_callback(widget_class, color_scheme_button_clicked_cb);
}

static void
window_main_init(WindowMain *self) {
    domain_gquark = g_quark_from_static_string("WindowMain");
    AdwStyleManager *manager = adw_style_manager_get_default();

    gtk_widget_init_template(GTK_WIDGET(self));

    g_signal_connect_object(manager,
                            "notify::system-supports-color-schemes",
                            G_CALLBACK(notify_system_supports_color_schemes_cb),
                            self,
                            G_CONNECT_SWAPPED);

    notify_system_supports_color_schemes_cb(self);

    self->hex_buffer_size = 1;
    self->hex_buffer = g_malloc(self->hex_buffer_size);
    memset(self->hex_buffer, 0xff, self->hex_buffer_size);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->hex_widget), hex_widget_draw_function, self, NULL);

    GtkEventController *ev_ctl = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect_object(ev_ctl, "scroll", G_CALLBACK(hex_widget_scroll_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(ev_ctl, "scroll-begin", G_CALLBACK(hex_widget_scroll_begin_cb), self, G_CONNECT_DEFAULT);
    gtk_widget_add_controller(GTK_WIDGET(self->hex_widget), GTK_EVENT_CONTROLLER(ev_ctl));

    GtkAdjustment *scroll_adj = gtk_scrollbar_get_adjustment(self->scroll_bar);
    gtk_adjustment_set_upper(scroll_adj, (uint32_t)(self->hex_buffer_size / BYTES_PER_LINE) +
                                         (self->hex_buffer_size % BYTES_PER_LINE == 0 ? 0 : 1));
    g_signal_connect_object(scroll_adj, "value-changed", G_CALLBACK(scroll_bar_value_changed_cb), self,
                            G_CONNECT_DEFAULT);

    ev_ctl = gtk_event_controller_key_new();
    g_signal_connect(ev_ctl, "key-pressed", G_CALLBACK(hex_key_press_cb), self);
    gtk_widget_add_controller(GTK_WIDGET(self->hex_widget), GTK_EVENT_CONTROLLER(ev_ctl));

    ev_ctl = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
    g_signal_connect(ev_ctl, "pressed", G_CALLBACK(hex_pressed_cb), self);
    gtk_widget_add_controller(GTK_WIDGET(self->hex_widget), GTK_EVENT_CONTROLLER(ev_ctl));

    ev_ctl = gtk_event_controller_focus_new();
    g_signal_connect_swapped (ev_ctl, "enter", G_CALLBACK(gtk_widget_queue_draw), GTK_WIDGET(self->hex_widget));
    g_signal_connect_swapped (ev_ctl, "leave", G_CALLBACK(gtk_widget_queue_draw), GTK_WIDGET(self->hex_widget));
    gtk_widget_add_controller(GTK_WIDGET(self->hex_widget), GTK_EVENT_CONTROLLER(ev_ctl));


    g_signal_connect_object(self->test_button, "clicked", G_CALLBACK(window_main_button_clicked_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(self->erase_button, "clicked", G_CALLBACK(window_main_button_clicked_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(self->read_button, "clicked", G_CALLBACK(window_main_button_clicked_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(self->write_button, "clicked", G_CALLBACK(window_main_button_clicked_cb), self,
                            G_CONNECT_DEFAULT);

    g_signal_connect_object(self->flash_type_selector, "notify::selected-item",
                            G_CALLBACK(dropdown_selected_item_changed_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->flash_manufacturer_selector, "notify::selected-item",
                            G_CALLBACK(dropdown_selected_item_changed_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->flash_name_selector, "notify::selected-item",
                            G_CALLBACK(dropdown_selected_item_changed_cb), self, G_CONNECT_DEFAULT);
    self->programmer = ezp_find_programmer();
    if (!self->programmer) {
        window_main_set_buttons_sensitive(self, false);
    }
    gtk_button_set_icon_name(self->status_icon, self->programmer ? "status-ok" : "status-error");
    programmer_status_task_start(self);
}

WindowMain *
window_main_new(GtkApplication *application, ChipsDataRepository *repo) {
    WindowMain *wm = g_object_new(EZP_TYPE_WINDOW_MAIN, "application", application, NULL);

    wm->repo = repo;
    g_signal_connect_object(wm->repo, "chips-list", G_CALLBACK(chips_list_changed_cb), wm, G_CONNECT_DEFAULT);
    chips_list list = chips_data_repository_get_chips(repo);
    chips_list_changed_cb(NULL, &list, wm);

    return wm;
}