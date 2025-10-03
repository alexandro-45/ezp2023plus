#include "chips-editor.h"
#include "list-row.h"
#include "chip-edit-dialog.h"
#include <glib/gi18n.h>
#include <ezp_chips_data_file.h>

struct _WindowChipsEditor {
    AdwWindow parent_instance;

    GtkColumnView *chips_list;
    GtkSearchBar *chips_search_bar;
    GtkSearchEntry *chips_search_entry;
    GtkPopoverMenu *chips_list_context_menu;
    GtkStringFilter *filter;
    GListStore *store;
    ChipsDataRepository *repo;
};

G_DEFINE_FINAL_TYPE (WindowChipsEditor, window_chips_editor, ADW_TYPE_WINDOW)

static void
action_add_chip(GtkWidget *widget, G_GNUC_UNUSED const char *action_name, G_GNUC_UNUSED GVariant *parameter) {
    WindowChipsEditor *wce = EZP_WINDOW_CHIPS_EDITOR(widget);
    ezp_chip_data new = {"FLASH_TYPE,MANUFACTURER,NAME", 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    chips_data_repository_add(wce->repo, &new);
    if (chips_data_repository_save(wce->repo) != 0) {
        AdwAlertDialog *dlg = ADW_ALERT_DIALOG(
                adw_alert_dialog_new(gettext("Error!"), gettext("Changes can't be saved: IO error")));
        adw_alert_dialog_add_response(dlg, "ok", gettext("Ok"));
        adw_dialog_present(ADW_DIALOG(dlg), widget);
        return;
    }
    DialogChipsEdit *dlg = dialog_chips_edit_new(wce->repo, chips_data_repository_get_chips(wce->repo).length - 1);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(wce));
}

static void
delete_chip_alert_response_cb(G_GNUC_UNUSED AdwAlertDialog *self, gchar *response, gpointer user_data) {
    WindowChipsEditor *wce = EZP_WINDOW_CHIPS_EDITOR((gpointer) ((uint64_t *) user_data)[0]);
    if (!strcmp(response, "yes")) {
        chips_data_repository_delete(wce->repo, ((uint64_t *) user_data)[1]);
        chips_data_repository_save(wce->repo);
    }
    g_free(user_data);
}

static void
action_delete_chip(GtkWidget *widget, G_GNUC_UNUSED const char *action_name, G_GNUC_UNUSED GVariant *parameter) {
    WindowChipsEditor *wce = EZP_WINDOW_CHIPS_EDITOR(widget);

    //get selected row model from filtered list
    GListModel *filtered_list = G_LIST_MODEL(gtk_column_view_get_model(wce->chips_list));
    GtkBitset *selection = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(filtered_list));
    guint pos_in_filtered_list = gtk_bitset_get_minimum(selection);
    gtk_bitset_unref(selection);
    gpointer row_model = g_list_model_get_item(filtered_list, pos_in_filtered_list);

    //find selected row model position in full list
    guint pos = 0;
    if (!g_list_store_find(wce->store, row_model, &pos)) return;

    gchar *alert_body = g_strdup_printf(gettext("%s will be deleted. This operation cannot be undone."),
                                        chips_data_repository_get_chips(wce->repo).data[pos].name);
    AdwAlertDialog *alert = ADW_ALERT_DIALOG(adw_alert_dialog_new(gettext("Do you want to delete chip?"), alert_body));
    g_free(alert_body);

    adw_alert_dialog_add_responses(alert,
                                   "no", gettext("_No"),
                                   "yes", gettext("_Yes"), NULL);

    adw_alert_dialog_set_response_appearance(alert, "no", ADW_RESPONSE_DEFAULT);
    adw_alert_dialog_set_response_appearance(alert, "yes", ADW_RESPONSE_DESTRUCTIVE);

    adw_alert_dialog_set_default_response(alert, "no");
    adw_alert_dialog_set_close_response(alert, "no");

    uint64_t *user_data = g_malloc(sizeof(uint64_t) * 2);
    user_data[0] = (uint64_t) wce;
    user_data[1] = pos;
    g_signal_connect(alert, "response", G_CALLBACK(delete_chip_alert_response_cb), user_data);

    adw_dialog_present(ADW_DIALOG(alert), GTK_WIDGET(wce));
}

static void
column_view_right_click_pressed(G_GNUC_UNUSED GtkGestureClick *self, G_GNUC_UNUSED gint n_press, gdouble x, gdouble y,
                                gpointer user_data) {
    WindowChipsEditor *wce = EZP_WINDOW_CHIPS_EDITOR(user_data);
    GtkWidget *child = gtk_widget_pick(GTK_WIDGET(wce->chips_list), x, y, GTK_PICK_DEFAULT);
    GtkWidget *row = gtk_widget_get_ancestor(child, g_type_from_name("GtkColumnViewRowWidget"));
    if (!row) return;

    GtkWidget *label_maybe = gtk_widget_get_first_child(gtk_widget_get_first_child(row));
    if (!GTK_IS_LABEL(label_maybe)) return;

    const char *id = gtk_widget_get_name(label_maybe);
    GListModel *filtered_list = G_LIST_MODEL(gtk_column_view_get_model(wce->chips_list));
    guint pos = 0;
    for (guint i = 0; g_list_model_get_n_items(filtered_list); ++i) {
        ChipsEditorListRow *row_model = EZP_CHIPS_EDITOR_LIST_ROW(g_list_model_get_item(filtered_list, i));
        if (!strcmp(id, chips_editor_list_row_get_id(row_model))) {
            pos = i;
            break;
        }
    }

    gtk_column_view_scroll_to(wce->chips_list, pos, NULL, GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT, NULL);
    GdkRectangle rect = {(int) x, (int) y, 0, 0};
    gtk_popover_set_pointing_to(GTK_POPOVER(wce->chips_list_context_menu), &rect);
    gtk_popover_popup(GTK_POPOVER(wce->chips_list_context_menu));
}

static void
search_text_changed_cb(GtkEditable *editable, gpointer data) {
    printf("search query: %s\n", gtk_editable_get_text(editable));
    WindowChipsEditor *self = EZP_WINDOW_CHIPS_EDITOR(data);
    gtk_string_filter_set_search(self->filter, gtk_editable_get_text(editable));
}

static void
chips_list_activate_cb(G_GNUC_UNUSED GtkColumnView *self, guint position, gpointer user_data) {
    WindowChipsEditor *wce = EZP_WINDOW_CHIPS_EDITOR(user_data);

    //get selected row model from filtered list
    GListModel *filtered_list = G_LIST_MODEL(gtk_column_view_get_model(wce->chips_list));
    gpointer row_model = g_list_model_get_item(filtered_list, position);

    //find selected row model position in full list
    guint real_pos = 0;
    if (!g_list_store_find(wce->store, row_model, &real_pos)) return;

    DialogChipsEdit *dlg = dialog_chips_edit_new(wce->repo, real_pos);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(wce));
}

static void
chips_list_changed_cb(G_GNUC_UNUSED ChipsDataRepository *repo, chips_list *list, gpointer user_data) {
    WindowChipsEditor *self = user_data;

    g_list_store_remove_all(self->store);
    for (size_t i = 0; i < list->length; ++i) {
        ChipsEditorListRow *row = chips_editor_list_row_new(&list->data[i]);
        char id[11];
        snprintf(id, sizeof(id), "%zu", i);
        chips_editor_list_row_set_id(row, id);
        g_list_store_append(self->store, row);
        g_object_unref(row);
    }
}

static void
window_chips_editor_class_init(WindowChipsEditorClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/dev/alexandro45/ezp2023plus/ui/windows/chips-editor/chips-editor.ui");

    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, chips_list);
    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, chips_search_bar);
    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, chips_search_entry);
    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, chips_list_context_menu);

    gtk_widget_class_install_action(widget_class, "win.add-chip", NULL, action_add_chip);
    gtk_widget_class_install_action(widget_class, "win.delete-chip", NULL, action_delete_chip);

    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_n, GDK_CONTROL_MASK, "win.add-chip", NULL);
    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_Delete, GDK_NO_MODIFIER_MASK, "win.delete-chip", NULL);
}

static void
window_chips_editor_init(WindowChipsEditor *self) {
    gtk_widget_init_template(GTK_WIDGET (self));

    self->store = g_list_store_new(EZP_TYPE_CHIPS_EDITOR_LIST_ROW);

    GtkExpression *exp = gtk_property_expression_new(EZP_TYPE_CHIPS_EDITOR_LIST_ROW, NULL, "name");
    self->filter = gtk_string_filter_new(exp);
    gtk_string_filter_set_ignore_case(self->filter, TRUE);
    GtkFilterListModel *filterModel = gtk_filter_list_model_new(G_LIST_MODEL(self->store), GTK_FILTER(self->filter));

    GtkSingleSelection *selection = gtk_single_selection_new(G_LIST_MODEL(filterModel));

    gtk_column_view_set_model(self->chips_list, GTK_SELECTION_MODEL(selection));
    g_signal_connect_object(self->chips_list, "activate", G_CALLBACK(chips_list_activate_cb), self, G_CONNECT_DEFAULT);
    g_object_unref(selection);

    gtk_search_bar_set_key_capture_widget(self->chips_search_bar, GTK_WIDGET (self));
    g_signal_connect (self->chips_search_entry, "changed", G_CALLBACK(search_text_changed_cb), self);

    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(column_view_right_click_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self->chips_list), GTK_EVENT_CONTROLLER(click_gesture));
}

WindowChipsEditor *
window_chips_editor_new(ChipsDataRepository *repo) {
    WindowChipsEditor *win = g_object_new(EZP_TYPE_WINDOW_CHIPS_EDITOR, NULL);

    win->repo = repo;
    g_signal_connect_object(win->repo, "chips-list", G_CALLBACK(chips_list_changed_cb), win, G_CONNECT_DEFAULT);
    chips_list list = chips_data_repository_get_chips(repo);
    chips_list_changed_cb(NULL, &list, win);

    return win;
}