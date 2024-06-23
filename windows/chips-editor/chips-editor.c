#include "chips-editor.h"
#include "list-row.h"
#include "chip-edit-dialog.h"
#include <glib/gi18n.h>
#include <ezp_chips_data_file.h>

struct _WindowChipsEditor {
    AdwWindow parent_instance;

    GtkColumnView *chips_list;
    GtkToggleButton *search_button;
    GtkSearchBar *chips_searchbar;
    GtkSearchEntry *chips_searchentry;
    GtkStringFilter *filter;
    GListStore *store;
    ChipsDataRepository *repo;
};

G_DEFINE_FINAL_TYPE (WindowChipsEditor, window_chips_editor, ADW_TYPE_WINDOW)

static void
add_chip(GtkWidget *widget, G_GNUC_UNUSED const char *action_name, G_GNUC_UNUSED GVariant *parameter) {
    WindowChipsEditor *wce = EZP_WINDOW_CHIPS_EDITOR(widget);
    ezp_chip_data new = {"FLASH_TYPE,MANUFACTURER,NAME", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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
window_chips_editor_class_init(WindowChipsEditorClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/dev/alexandro45/ezp2023plus/ui/windows/chips-editor/chips-editor.ui");

    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, chips_list);
    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, search_button);
    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, chips_searchbar);
    gtk_widget_class_bind_template_child(widget_class, WindowChipsEditor, chips_searchentry);

    gtk_widget_class_install_action(widget_class, "win.add-chip", NULL, add_chip);

    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_n, GDK_CONTROL_MASK, "win.add-chip", NULL);
}

static void
search_text_changed_cb(GtkEditable *editable, gpointer data) {
    printf("search query: %s\n", gtk_editable_get_text(editable));
    WindowChipsEditor *self = EZP_WINDOW_CHIPS_EDITOR(data);
    gtk_string_filter_set_search(self->filter, gtk_editable_get_text(editable));
}

static void
chips_list_changed_cb(G_GNUC_UNUSED ChipsDataRepository *repo, chips_list *list, gpointer user_data) {
    WindowChipsEditor *self = user_data;

    g_list_store_remove_all(self->store);
    for (size_t i = 0; i < list->length; ++i) {
        ChipsEditorListRow *row = chips_editor_list_row_new(&list->data[i]);
        g_list_store_append(self->store, row);
        g_object_unref(row);
    }
}

static void
chips_list_activate_cb(G_GNUC_UNUSED GtkColumnView *self, guint position, gpointer user_data) {
    WindowChipsEditor *wce = EZP_WINDOW_CHIPS_EDITOR(user_data);
    DialogChipsEdit *dlg = dialog_chips_edit_new(wce->repo, position);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(wce));
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

    gtk_search_bar_set_key_capture_widget(self->chips_searchbar, GTK_WIDGET (self));
    g_signal_connect (self->chips_searchentry, "changed", G_CALLBACK(search_text_changed_cb), self);
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