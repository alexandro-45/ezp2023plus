#include "chip-edit-dialog.h"
#include "utilities.h"
#include <glib/gi18n.h>

struct _DialogChipsEdit {
    AdwDialog parent_instance;

    AdwHeaderBar *header_bar;
    GtkEntry *type_selector;
    GtkEntry *manuf_selector;
    GtkEntry *name_selector;
    GtkSpinButton *chip_id_selector;
    GtkSpinButton *flash_size_selector;
    GtkSpinButton *flash_page_size_selector;
    GtkDropDown *voltage_selector;
    GtkDropDown *class_selector;
    GtkDropDown *algorithm_selector;
    GtkSpinButton *delay_selector;
    GtkSpinButton *eeprom_size_selector;
    GtkSpinButton *eeprom_page_size_selector;
    GtkSpinButton *extend_selector;
    GtkButton *prev_btn;
    GtkButton *save_btn;
    GtkButton *next_btn;

    ChipsDataRepository *repo;
    guint current_index;
    guint last_index;
    gboolean has_unsaved_changes;
};

G_DEFINE_FINAL_TYPE(DialogChipsEdit, dialog_chips_edit, ADW_TYPE_DIALOG)

typedef void (*unsaved_alert_cb)(DialogChipsEdit *dce);

static void
setup_algorithm_selector(GtkDropDown *selector, uint8_t clazz, uint8_t algorithm) {
    if (clazz > 4) {
        GtkStringList *l = gtk_string_list_new(NULL);
        gtk_string_list_append(l, gettext("Corrupted data"));
        gtk_drop_down_set_model(selector, G_LIST_MODEL(l));
        return;
    }

    GtkStringList *l = gtk_string_list_new(algorithms_for[clazz]);
    gtk_drop_down_set_model(selector, G_LIST_MODEL(l));
    if (clazz == EEPROM_93) {
        if (algorithm != 0) //look at class_selection_changed_cb. valid algorithm for EEPROM_93 starts with 7
            gtk_drop_down_set_selected(selector, algorithm < 87 ? algorithm - 7 : algorithm - 82);
    } else {
        gtk_drop_down_set_selected(selector, algorithm);
    }
}

static void
set_navigation_buttons_sensitivity(DialogChipsEdit *dce) {
    gtk_widget_set_sensitive(GTK_WIDGET(dce->prev_btn), dce->current_index > 0);
    gtk_widget_set_sensitive(GTK_WIDGET(dce->next_btn), dce->current_index < dce->last_index);
}

static void
update_widget_values(DialogChipsEdit *dce) {
    chips_list list = chips_data_repository_get_chips(dce->repo);
    if (list.length == 0 || dce->current_index >= (guint) list.length) return;
    dce->last_index = list.length - 1;
    ezp_chip_data *data = &list.data[dce->current_index];
    char full_name[48];
    strlcpy(full_name, data->name, 48);
    char *type;
    char *manuf;
    char *name;
    if (parseName(full_name, &type, &manuf, &name)) {
        return;
    }
    gtk_entry_set_text(dce->type_selector, type);
    gtk_entry_set_text(dce->manuf_selector, manuf);
    gtk_entry_set_text(dce->name_selector, name);

    gtk_spin_button_set_value(dce->chip_id_selector, data->chip_id);
    gtk_spin_button_set_value(dce->flash_size_selector, data->flash);
    gtk_spin_button_set_value(dce->flash_page_size_selector, data->flash_page);
    gtk_drop_down_set_selected(dce->voltage_selector, data->voltage);
    gtk_drop_down_set_selected(dce->class_selector, data->clazz);
    setup_algorithm_selector(dce->algorithm_selector, data->clazz, data->algorithm);
    gtk_spin_button_set_value(dce->delay_selector, data->delay);
    gtk_spin_button_set_value(dce->eeprom_size_selector, data->eeprom);
    gtk_spin_button_set_value(dce->eeprom_page_size_selector, data->eeprom_page);
    gtk_spin_button_set_value(dce->extend_selector, data->extend);

    dce->has_unsaved_changes = false;
    adw_dialog_set_title(ADW_DIALOG(dce), gettext("Chips editor"));

    set_navigation_buttons_sensitivity(dce);
}

static void
chips_list_changed_cb(G_GNUC_UNUSED ChipsDataRepository *repo, G_GNUC_UNUSED chips_list *list, gpointer user_data) {
    update_widget_values(EZP_DIALOG_CHIPS_EDIT(user_data));
}

static void
chip_data_from_widgets(DialogChipsEdit *self, ezp_chip_data *data) {
    const char *type = gtk_entry_get_text(self->type_selector);
    const char *manuf = gtk_entry_get_text(self->manuf_selector);
    const char *name = gtk_entry_get_text(self->name_selector);
    snprintf(data->name, 48, "%s,%s,%s", type, manuf, name);

    data->chip_id = gtk_spin_button_get_value_as_int(self->chip_id_selector);
    data->clazz = gtk_drop_down_get_selected(self->class_selector);

    data->algorithm = gtk_drop_down_get_selected(self->algorithm_selector);
    if (data->clazz == EEPROM_93) {
        if (data->algorithm < 5) data->algorithm += 7;
        else data->algorithm += 82;
    }

    data->flash = gtk_spin_button_get_value_as_int(self->flash_size_selector);
    data->flash_page = gtk_spin_button_get_value_as_int(self->flash_page_size_selector);
    data->voltage = gtk_drop_down_get_selected(self->voltage_selector);
    data->delay = gtk_spin_button_get_value_as_int(self->delay_selector);
    data->eeprom = gtk_spin_button_get_value_as_int(self->eeprom_size_selector);
    data->eeprom_page = gtk_spin_button_get_value_as_int(self->eeprom_page_size_selector);
    data->extend = gtk_spin_button_get_value_as_int(self->extend_selector);
}

static void
unsaved_alert_response_cb(G_GNUC_UNUSED AdwAlertDialog *self, gchar *response, gpointer user_data) {
    void **pointers = user_data;
    unsaved_alert_cb discard = pointers[0];
    unsaved_alert_cb save = pointers[1];
    DialogChipsEdit *dce = pointers[2];
    if (!strcmp(response, "discard")) {
        if (discard) discard(dce);
    } else if (!strcmp(response, "save")) {
        if (save) save(dce);
    }
    g_free(user_data);
}

static void
check_unsaved(DialogChipsEdit *dce, unsaved_alert_cb discard_cb, unsaved_alert_cb save_cb) {
    if (dce->has_unsaved_changes) {
        AdwAlertDialog *alert = ADW_ALERT_DIALOG(adw_alert_dialog_new(gettext("Save changes?"),
                                                                      gettext("Current chip has unsaved changes. Changes which are not saved will be permanently lost.")));

        adw_alert_dialog_add_responses(alert,
                                       "cancel", gettext("_Cancel"),
                                       "discard", gettext("_Discard"),
                                       "save", gettext("S_ave"), NULL);

        adw_alert_dialog_set_response_appearance(alert, "cancel", ADW_RESPONSE_DEFAULT);
        adw_alert_dialog_set_response_appearance(alert, "discard", ADW_RESPONSE_DESTRUCTIVE);
        adw_alert_dialog_set_response_appearance(alert, "save", ADW_RESPONSE_SUGGESTED);

        adw_alert_dialog_set_default_response(alert, "cancel");
        adw_alert_dialog_set_close_response(alert, "cancel");

        void **pointers = g_malloc(sizeof(void*) * 3);
        pointers[0] = discard_cb;
        pointers[1] = save_cb;
        pointers[2] = dce;
        g_signal_connect(alert, "response", G_CALLBACK(unsaved_alert_response_cb), pointers);

        adw_dialog_present(ADW_DIALOG(alert), GTK_WIDGET(dce));
    } else {
        if (discard_cb) discard_cb(dce);
    }
}

static gboolean
save_chip(DialogChipsEdit *dce) {
    ezp_chip_data data;
    chip_data_from_widgets(dce, &data);
    chips_data_repository_edit(dce->repo, (int) dce->current_index, &data);
    if (chips_data_repository_save(dce->repo) != 0) {
        AdwAlertDialog *dlg = ADW_ALERT_DIALOG(
                adw_alert_dialog_new(gettext("Error!"), gettext("Changes can't be saved: IO error")));
        adw_alert_dialog_add_response(dlg, "ok", gettext("Ok"));
        adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(dce));
        return false;
    }
    if (dce->has_unsaved_changes) {
        dce->has_unsaved_changes = false;
        adw_dialog_set_title(ADW_DIALOG(dce), gettext("Chips editor"));
    }
    return true;
}

static void
save_btn_click_cb(G_GNUC_UNUSED GtkButton *btn, gpointer user_data) {
    save_chip(EZP_DIALOG_CHIPS_EDIT(user_data));
}

static void
discard_and_go_prev(DialogChipsEdit *dce) {
    if (dce->current_index > 0) {
        dce->current_index--;
    }
    update_widget_values(dce);
}

static void
save_and_go_prev(DialogChipsEdit *dce) {
    if (!save_chip(dce)) return;
    discard_and_go_prev(dce);
}

static void
prev_btn_click_cb(G_GNUC_UNUSED GtkButton *btn, gpointer user_data) {
    DialogChipsEdit *dce = EZP_DIALOG_CHIPS_EDIT(user_data);
    check_unsaved(dce, discard_and_go_prev, save_and_go_prev);
}

static void
discard_and_go_next(DialogChipsEdit *dce) {
    if (dce->current_index < dce->last_index) {
        dce->current_index++;
    }
    update_widget_values(dce);
}

static void
save_and_go_next(DialogChipsEdit *dce) {
    if (!save_chip(dce)) return;
    discard_and_go_next(dce);
}

static void
next_btn_click_cb(G_GNUC_UNUSED GtkButton *btn, gpointer user_data) {
    DialogChipsEdit *dce = EZP_DIALOG_CHIPS_EDIT(user_data);
    check_unsaved(dce, discard_and_go_next, save_and_go_next);
}

/* // Unused code
static void
chip_id_text_changed(GtkEditable *self, G_GNUC_UNUSED gpointer user_data) {
    const char *chip_id_str = gtk_entry_get_text(GTK_ENTRY(self));
    if (strlen(chip_id_str) > 10 ||
        !g_regex_match_simple("^0x[\\da-fA-F]+$", chip_id_str, G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT)) {
        gtk_widget_add_css_class(GTK_WIDGET(self), "error");
    } else {
        gtk_widget_remove_css_class(GTK_WIDGET(self), "error");
    }
}
*/

static void
class_selection_changed_cb(GtkDropDown *self, G_GNUC_UNUSED gpointer *new_value, gpointer user_data) {
    DialogChipsEdit *dce = EZP_DIALOG_CHIPS_EDIT(user_data);
    setup_algorithm_selector(dce->algorithm_selector, gtk_drop_down_get_selected(self), 0);
}

static void
any_entry_changed_cb(G_GNUC_UNUSED GtkEditable *self, gpointer user_data) {
    DialogChipsEdit *dce = EZP_DIALOG_CHIPS_EDIT(user_data);
    if (!dce->has_unsaved_changes) {
        dce->has_unsaved_changes = true;
        adw_dialog_set_title(ADW_DIALOG(user_data), gettext("Chips editor (unsaved)"));
    }
}

static void
any_drop_down_selection_changed_cb(G_GNUC_UNUSED GtkDropDown *self, G_GNUC_UNUSED gpointer *new_value, gpointer user_data) {
    DialogChipsEdit *dce = EZP_DIALOG_CHIPS_EDIT(user_data);
    if (!dce->has_unsaved_changes) {
        dce->has_unsaved_changes = true;
        adw_dialog_set_title(ADW_DIALOG(user_data), gettext("Chips editor (unsaved)"));
    }
}

static void
dialog_chips_editor_realize(G_GNUC_UNUSED GtkWidget *self, gpointer user_data) {
    DialogChipsEdit *dce = EZP_DIALOG_CHIPS_EDIT(user_data);
    ////////////ATTENTION! SHIT CODE BEGIN!//////////////////////
    //remove this fix and other related stuff when libadwaita will be fixed
    gpointer win_handle = gtk_widget_get_first_child(GTK_WIDGET(dce->header_bar));
    gpointer center_box = gtk_widget_get_first_child(GTK_WIDGET(win_handle));
    gpointer gizmo = gtk_center_box_get_end_widget(GTK_CENTER_BOX(center_box));
    gpointer box = gtk_widget_get_first_child(gizmo);
    gpointer controls = gtk_widget_get_first_child(box);
    gpointer cl_btn = gtk_widget_get_first_child(controls);
    gtk_actionable_set_action_name(cl_btn, "window.close");
    /////////////////////////////////////////////////////////////
}

static void
save_and_close(DialogChipsEdit *dce) {
    if (!save_chip(dce)) return;
    adw_dialog_force_close(ADW_DIALOG(dce));
}

static void
chip_edit_dialog_close_attempt(AdwDialog *dialog) {
    DialogChipsEdit *dce = EZP_DIALOG_CHIPS_EDIT(dialog);
    check_unsaved(dce, (unsaved_alert_cb) adw_dialog_force_close, save_and_close);
}

static gboolean
chip_id_selector_output_cb(GtkSpinButton *spin_button, G_GNUC_UNUSED gpointer user_data) {
    guint value = gtk_spin_button_get_value_as_int(spin_button);
    gchar *text = g_strdup_printf("0x%x", value);
    gtk_editable_set_text(GTK_EDITABLE(spin_button), text);
    g_free(text);
    return TRUE;
}

static void
dialog_chips_edit_class_init(DialogChipsEditClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    klass->parent_class.close_attempt = chip_edit_dialog_close_attempt;

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/dev/alexandro45/ezp2023plus/ui/windows/chips-editor/chip-edit-dialog.ui");

    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, header_bar);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, type_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, manuf_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, name_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, chip_id_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, flash_size_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, flash_page_size_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, voltage_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, class_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, algorithm_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, delay_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, eeprom_size_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, eeprom_page_size_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, extend_selector);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, prev_btn);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, save_btn);
    gtk_widget_class_bind_template_child(widget_class, DialogChipsEdit, next_btn);
}

static void
dialog_chips_edit_init(DialogChipsEdit *self) {
    gtk_widget_init_template(GTK_WIDGET (self));

    disable_scroll_for(GTK_WIDGET(self->chip_id_selector));
    disable_scroll_for(GTK_WIDGET(self->flash_size_selector));
    disable_scroll_for(GTK_WIDGET(self->flash_page_size_selector));
    disable_scroll_for(GTK_WIDGET(self->delay_selector));
    disable_scroll_for(GTK_WIDGET(self->eeprom_size_selector));
    disable_scroll_for(GTK_WIDGET(self->eeprom_page_size_selector));
    disable_scroll_for(GTK_WIDGET(self->extend_selector));

    g_signal_connect(self->chip_id_selector, "output", G_CALLBACK(chip_id_selector_output_cb), NULL);

    g_signal_connect_object(self->save_btn, "clicked", G_CALLBACK(save_btn_click_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->prev_btn, "clicked", G_CALLBACK(prev_btn_click_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->next_btn, "clicked", G_CALLBACK(next_btn_click_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->class_selector, "notify::selected-item", G_CALLBACK(class_selection_changed_cb), self,
                            G_CONNECT_DEFAULT);

    g_signal_connect_object(self->type_selector, "changed", G_CALLBACK(any_entry_changed_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->manuf_selector, "changed", G_CALLBACK(any_entry_changed_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->name_selector, "changed", G_CALLBACK(any_entry_changed_cb), self, G_CONNECT_DEFAULT);

    g_signal_connect_object(gtk_spin_button_get_adjustment(self->chip_id_selector), "value-changed", G_CALLBACK(any_entry_changed_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(gtk_spin_button_get_adjustment(self->flash_size_selector), "value-changed", G_CALLBACK(any_entry_changed_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(gtk_spin_button_get_adjustment(self->flash_page_size_selector), "value-changed", G_CALLBACK(any_entry_changed_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(gtk_spin_button_get_adjustment(self->delay_selector), "value-changed", G_CALLBACK(any_entry_changed_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(gtk_spin_button_get_adjustment(self->eeprom_size_selector), "value-changed", G_CALLBACK(any_entry_changed_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(gtk_spin_button_get_adjustment(self->eeprom_page_size_selector), "value-changed", G_CALLBACK(any_entry_changed_cb), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(gtk_spin_button_get_adjustment(self->extend_selector), "value-changed", G_CALLBACK(any_entry_changed_cb), self,
                            G_CONNECT_DEFAULT);

    g_signal_connect_object(self->voltage_selector, "notify::selected-item",
                            G_CALLBACK(any_drop_down_selection_changed_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->class_selector, "notify::selected-item",
                            G_CALLBACK(any_drop_down_selection_changed_cb), self, G_CONNECT_DEFAULT);
    g_signal_connect_object(self->algorithm_selector, "notify::selected-item",
                            G_CALLBACK(any_drop_down_selection_changed_cb), self, G_CONNECT_DEFAULT);

    g_signal_connect_object(self, "realize", G_CALLBACK(dialog_chips_editor_realize), self, G_CONNECT_DEFAULT);
}

DialogChipsEdit *
dialog_chips_edit_new(ChipsDataRepository *repo, guint open_index) {
    DialogChipsEdit *dlg = g_object_new(EZP_TYPE_DIALOG_CHIPS_EDIT, NULL);

    dlg->repo = repo;
    dlg->current_index = open_index;
    g_signal_connect_object(dlg->repo, "chips-list", G_CALLBACK(chips_list_changed_cb), dlg, G_CONNECT_DEFAULT);
    update_widget_values(dlg);

    return dlg;
}
