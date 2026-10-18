// SPDX-License-Identifier: GPL-3.0-only
#include "launch/launcher_ui.hpp"
#include "launch/archive.hpp"
#include <gtk/gtk.h>
#include <array>
namespace bmh::launch {
namespace {
class GtkLauncher final : public LauncherUI {
    GtkWidget *window_ = nullptr, *tree_ = nullptr, *progress_ = nullptr, *progress_label_ = nullptr;
    GtkListStore *model_ = nullptr;
    std::array<GtkWidget *, 5> buttons_{};
    bool cancelled_ = false;
    std::string selection_;
    enum { Start = 1, Add, Delete, Rename, End };
    static void selection_changed(GtkTreeSelection *selection, gpointer data) {
        auto &self = *static_cast<GtkLauncher *>(data);
        GtkTreeIter it;
        GtkTreeModel *model;
        const bool selected = gtk_tree_selection_get_selected(selection, &model, &it);
        for (int id : {Start, Delete, Rename})
            gtk_widget_set_sensitive(self.buttons_[size_t(id - 1)], selected);
    }
    GtkWidget *entry(GtkWidget *grid, int row, const char *label, const std::string &initial) {
        auto *title = gtk_label_new(label);
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), title, 0, row, 1, 1);
        auto *input = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(input), initial.c_str());
        gtk_widget_set_hexpand(input, TRUE);
        gtk_grid_attach(GTK_GRID(grid), input, 1, row, 1, 1);
        return input;
    }

  public:
    GtkLauncher() {
        if (!gtk_init_check(nullptr, nullptr))
            throw std::runtime_error("Kein Desktop für den nativen Launcher verfügbar");
        window_ = gtk_dialog_new();
        gtk_window_set_title(GTK_WINDOW(window_), "OpenBMH");
        gtk_window_set_default_size(GTK_WINDOW(window_), 620, 340);
        auto *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
        gtk_container_set_border_width(GTK_CONTAINER(body), 16);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(window_))), body, TRUE, TRUE, 0);
        model_ = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
        tree_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model_));
        auto *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_));
        gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
        g_signal_connect(selection, "changed", G_CALLBACK(selection_changed), this);
        for (int i = 0; i < 2; ++i) {
            auto *renderer = gtk_cell_renderer_text_new();
            auto *column =
                gtk_tree_view_column_new_with_attributes(i ? "Art" : "Name", renderer, "text", i, nullptr);
            gtk_tree_view_column_set_expand(column, i == 0);
            gtk_tree_view_append_column(GTK_TREE_VIEW(tree_), column);
        }
        gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(tree_), 2);
        auto *scroller = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_hexpand(scroller, TRUE);
        gtk_widget_set_vexpand(scroller, TRUE);
        gtk_container_add(GTK_CONTAINER(scroller), tree_);
        gtk_box_pack_start(GTK_BOX(body), scroller, TRUE, TRUE, 0);
        auto *buttons = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_box_pack_start(GTK_BOX(body), buttons, FALSE, FALSE, 0);
        const char *labels[] = {"Start", "Add", "Delete", "Rename", "End"};
        for (int i = 0; i < 5; ++i) {
            auto *button = gtk_button_new_with_label(labels[i]);
            buttons_[size_t(i)] = button;
            gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
            g_object_set_data(G_OBJECT(button), "response-id", GINT_TO_POINTER(i + 1));
            g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton *button, gpointer self) {
                                 auto *window = static_cast<GtkLauncher *>(self)->window_;
                                 gtk_dialog_response(
                                     GTK_DIALOG(window),
                                     GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id")));
                             }),
                             this);
        }
        g_signal_connect(tree_, "row-activated",
                         G_CALLBACK(+[](GtkTreeView *, GtkTreePath *, GtkTreeViewColumn *, gpointer data) {
                             gtk_dialog_response(GTK_DIALOG(static_cast<GtkLauncher *>(data)->window_),
                                                 Start);
                         }),
                         this);
        gtk_widget_show_all(window_);
    }
    ~GtkLauncher() override {
        finish_progress();
        gtk_widget_destroy(window_);
        g_object_unref(model_);
    }
    LauncherChoice next(const std::vector<ModEntry> &mods) override {
        gtk_list_store_clear(model_);
        int selected = -1;
        for (size_t i = 0; i < mods.size(); ++i) {
            GtkTreeIter it;
            gtk_list_store_append(model_, &it);
            const auto &mod = mods[i];
            const auto shown = mod.error.empty() ? mod.name : mod.name + " ⚠";
            gtk_list_store_set(model_, &it, 0, shown.c_str(), 1, mod.type.c_str(), 2, mod.error.c_str(), -1);
            if (mod.name == selection_) {
                auto *path = gtk_tree_path_new_from_indices(int(i), -1);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree_), path, nullptr, FALSE);
                gtk_tree_path_free(path);
            }
        }
        selection_changed(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_)), this);
        const int response = gtk_dialog_run(GTK_DIALOG(window_));
        GtkTreeIter it;
        GtkTreeModel *model;
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_)), &model, &it)) {
            auto *path = gtk_tree_model_get_path(model, &it);
            selected = gtk_tree_path_get_indices(path)[0];
            gtk_tree_path_free(path);
            if (selected >= 0 && size_t(selected) < mods.size())
                selection_ = mods[size_t(selected)].name;
        }
        return {response == Start    ? LauncherAction::start
                : response == Add    ? LauncherAction::add
                : response == Delete ? LauncherAction::remove
                : response == Rename ? LauncherAction::rename
                                     : LauncherAction::end,
                selected};
    }
    std::optional<std::filesystem::path> choose_folder(const std::string &title,
                                                       const std::filesystem::path &initial) override {
        auto *dialog = gtk_file_chooser_dialog_new(
            title.c_str(), GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Abbrechen",
            GTK_RESPONSE_CANCEL, "Auswählen", GTK_RESPONSE_ACCEPT, nullptr);
        if (!initial.empty())
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), path_to_utf8(initial).c_str());
        std::optional<std::filesystem::path> result;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (file) {
                result = path_from_utf8(file);
                g_free(file);
            }
        }
        gtk_widget_destroy(dialog);
        return result;
    }
    std::optional<AddMod> configure_add(AddMod initial) override {
        auto *dialog = gtk_dialog_new_with_buttons("Classic-Mod hinzufügen", GTK_WINDOW(window_),
                                                   GTK_DIALOG_MODAL, "Abbrechen", GTK_RESPONSE_CANCEL,
                                                   "Importieren", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 540, -1);
        auto *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
        gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE, 0);
        auto *name = entry(grid, 0, "Bezeichnung", initial.name);
        auto *year = entry(grid, 1, "Startjahr (nur Anzeige)", std::to_string(initial.start_year));
        auto *migrate = gtk_check_button_new_with_label("Spielstände/Einstellungen übernehmen");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(migrate), initial.migrate);
        gtk_grid_attach(GTK_GRID(grid), migrate, 0, 2, 3, 1);
        auto *saves = entry(grid, 3, "Spielstandsordner", path_to_utf8(initial.saves));
        auto *browse = gtk_button_new_with_label("Auswählen …");
        gtk_grid_attach(GTK_GRID(grid), browse, 2, 3, 1, 1);
        struct Browse {
            GtkLauncher *self;
            GtkWidget *input;
        };
        Browse context{this, saves};
        g_signal_connect(
            browse, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
                auto &c = *static_cast<Browse *>(p);
                if (auto path = c.self->choose_folder("Spielstandsordner wählen",
                                                      path_from_utf8(gtk_entry_get_text(GTK_ENTRY(c.input)))))
                    gtk_entry_set_text(GTK_ENTRY(c.input), path_to_utf8(*path).c_str());
            }),
            &context);
        gtk_widget_show_all(dialog);
        std::optional<AddMod> result;
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            try {
                initial.name = gtk_entry_get_text(GTK_ENTRY(name));
                ModLibrary::validate_name(initial.name);
                std::string value = gtk_entry_get_text(GTK_ENTRY(year));
                size_t end = 0;
                initial.start_year = std::stoi(value, &end);
                if (end != value.size() || initial.start_year < 1900 || initial.start_year > 9998)
                    throw std::runtime_error("Startjahr muss zwischen 1900 und 9998 liegen");
                initial.saves = path_from_utf8(gtk_entry_get_text(GTK_ENTRY(saves)));
                initial.migrate = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(migrate));
                result = initial;
                break;
            } catch (const std::exception &e) {
                message(e.what(), true);
            }
        gtk_widget_destroy(dialog);
        return result;
    }
    std::optional<std::string> rename(const std::string &old) override {
        auto *dialog =
            gtk_dialog_new_with_buttons("Mod umbenennen", GTK_WINDOW(window_), GTK_DIALOG_MODAL, "Abbrechen",
                                        GTK_RESPONSE_CANCEL, "Umbenennen", GTK_RESPONSE_ACCEPT, nullptr);
        auto *input = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(input), old.c_str());
        gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 16);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), input, TRUE, TRUE, 0);
        gtk_widget_show_all(dialog);
        std::optional<std::string> result;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            result = gtk_entry_get_text(GTK_ENTRY(input));
        gtk_widget_destroy(dialog);
        return result;
    }
    bool confirm_delete(const std::string &name) override {
        auto *dialog = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_YES_NO, "Mod „%s“ löschen?", name.c_str());
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog), "Spielstände und persönliche Einstellungen bleiben erhalten.");
        const bool yes = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(dialog);
        return yes;
    }
    void message(const std::string &text, bool error) override {
        auto *dialog = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                                              error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                              "%s", text.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    void progress(const std::string &text, bool cancellable) override {
        if (!progress_) {
            cancelled_ = false;
            progress_ = gtk_dialog_new();
            gtk_window_set_title(GTK_WINDOW(progress_), "OpenBMH");
            gtk_window_set_transient_for(GTK_WINDOW(progress_), GTK_WINDOW(window_));
            gtk_window_set_modal(GTK_WINDOW(progress_), TRUE);
            gtk_container_set_border_width(GTK_CONTAINER(progress_), 20);
            progress_label_ = gtk_label_new(text.c_str());
            gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(progress_))), progress_label_,
                               TRUE, TRUE, 0);
            if (cancellable)
                gtk_dialog_add_button(GTK_DIALOG(progress_), "Abbrechen", GTK_RESPONSE_CANCEL);
            g_signal_connect(progress_, "response", G_CALLBACK(+[](GtkDialog *, int, gpointer data) {
                                 static_cast<GtkLauncher *>(data)->cancelled_ = true;
                             }),
                             this);
            g_signal_connect(progress_, "delete-event",
                             G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                                 static_cast<GtkLauncher *>(data)->cancelled_ = true;
                                 return TRUE;
                             }),
                             this);
            gtk_widget_set_sensitive(window_, FALSE);
            gtk_widget_show_all(progress_);
        }
        gtk_label_set_text(GTK_LABEL(progress_label_), text.c_str());
    }
    bool pump() override {
        while (gtk_events_pending())
            gtk_main_iteration();
        return cancelled_;
    }
    void finish_progress() override {
        if (progress_) {
            gtk_widget_destroy(progress_);
            progress_ = progress_label_ = nullptr;
            gtk_widget_set_sensitive(window_, TRUE);
            gtk_window_present(GTK_WINDOW(window_));
        }
    }
};
} // namespace
std::unique_ptr<LauncherUI> make_launcher_ui() { return std::make_unique<GtkLauncher>(); }
} // namespace bmh::launch
