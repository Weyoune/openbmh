// SPDX-License-Identifier: GPL-3.0-only
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include "launch/launcher_ui.hpp"
#include "launch/archive.hpp"
#include <functional>
namespace bmh::launch {
namespace {
std::wstring wide(const std::string &s) { return path_from_utf8(s).wstring(); }
std::string text(HWND window) {
    int n = GetWindowTextLengthW(window);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(window, s.data(), n + 1);
    s.resize(size_t(n));
    return path_to_utf8(std::filesystem::path(s));
}
struct Form {
    HWND window = nullptr;
    bool done = false;
    int response = 0;
    bool prevent_close = false;
    std::function<void(int)> command;
    static LRESULT CALLBACK procedure(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto *form = reinterpret_cast<Form *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            form = static_cast<Form *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(form));
            form->window = h;
        }
        if (form) {
            if (msg == WM_COMMAND && form->command) {
                try {
                    form->command(LOWORD(w));
                } catch (const std::exception &e) {
                    MessageBoxW(h, wide(e.what()).c_str(), L"OpenBMH", MB_OK | MB_ICONERROR);
                }
                return 0;
            }
            if (msg == WM_NOTIFY && reinterpret_cast<NMHDR *>(l)->code == LVN_ITEMCHANGED) {
                const bool selected =
                    ListView_GetNextItem(reinterpret_cast<NMHDR *>(l)->hwndFrom, -1, LVNI_SELECTED) >= 0;
                for (int id : {1, 3, 4})
                    EnableWindow(GetDlgItem(h, id), selected);
                return 0;
            }
            if (msg == WM_CLOSE) {
                form->response = 0;
                form->done = true;
                if (!form->prevent_close)
                    ShowWindow(h, SW_HIDE);
                return 0;
            }
        }
        return DefWindowProcW(h, msg, w, l);
    }
    Form(const wchar_t *title, int width, int height, HWND owner = nullptr) {
        static const bool registered = [] {
            WNDCLASSW c{};
            c.lpfnWndProc = procedure;
            c.hInstance = GetModuleHandleW(nullptr);
            c.lpszClassName = L"OpenBMHLauncher";
            c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            return RegisterClassW(&c) != 0;
        }();
        if (!registered)
            throw std::runtime_error("Cannot register native launcher window");
        window =
            CreateWindowExW(WS_EX_DLGMODALFRAME, L"OpenBMHLauncher", title,
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                            CW_USEDEFAULT, width, height, owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!window)
            throw std::runtime_error("Cannot create native launcher window");
        command = [this](int id) {
            // Edit controls also emit WM_COMMAND while the user types.
            if (id != IDOK && id != IDCANCEL)
                return;
            response = id;
            done = true;
        };
    }
    ~Form() {
        if (window)
            DestroyWindow(window);
    }
    HWND control(const wchar_t *cls, const std::wstring &label, int x, int y, int w, int h, int id,
                 DWORD style = 0) {
        auto window =
            CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, label.c_str(),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, w, h, this->window,
                            reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return window;
    }
    int run() {
        done = false;
        response = 0;
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        MSG msg;
        while (!done && GetMessageW(&msg, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(window, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        return response;
    }
};
class WinLauncher final : public LauncherUI {
    Form main_{L"OpenBMH", 660, 370};
    HWND list_ = nullptr;
    std::unique_ptr<Form> progress_;
    HWND progress_label_ = nullptr;
    bool com_ = false;

  public:
    WinLauncher() {
        const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        com_ = SUCCEEDED(result);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE)
            throw std::runtime_error("Cannot initialize native folder dialog");
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        list_ = main_.control(WC_LISTVIEWW, L"", 16, 16, 480, 290, 100,
                              LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        for (int i = 0; i < 2; ++i) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.cx = i ? 110 : 340;
            c.pszText = const_cast<wchar_t *>(i ? L"Art" : L"Name");
            ListView_InsertColumn(list_, i, &c);
        }
        const wchar_t *labels[] = {L"Start", L"Add", L"Delete", L"Rename", L"End"};
        for (int i = 0; i < 5; ++i)
            main_.control(L"BUTTON", labels[i], 514, 16 + i * 48, 110, 34, i + 1, BS_PUSHBUTTON);
        main_.command = [this](int id) {
            if (id < 1 || id > 5)
                return;
            const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
            if (id != 2 && id != 5 && selected < 0)
                return;
            main_.response = id;
            main_.done = true;
        };
    }
    ~WinLauncher() override {
        if (com_)
            CoUninitialize();
    }
    LauncherChoice next(const std::vector<ModEntry> &mods) override {
        const int old = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        ListView_DeleteAllItems(list_);
        for (size_t i = 0; i < mods.size(); ++i) {
            auto name = wide(mods[i].name + (mods[i].error.empty() ? "" : " [!]"));
            LVITEMW row{};
            row.mask = LVIF_TEXT;
            row.iItem = int(i);
            row.pszText = name.data();
            ListView_InsertItem(list_, &row);
            auto type = wide(mods[i].type);
            ListView_SetItemText(list_, int(i), 1, type.data());
        }
        if (old >= 0 && size_t(old) < mods.size())
            ListView_SetItemState(list_, old, LVIS_SELECTED, LVIS_SELECTED);
        const bool selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED) >= 0;
        for (int id : {1, 3, 4})
            EnableWindow(GetDlgItem(main_.window, id), selected);
        const int response = main_.run();
        return {response == 1   ? LauncherAction::start
                : response == 2 ? LauncherAction::add
                : response == 3 ? LauncherAction::remove
                : response == 4 ? LauncherAction::rename
                                : LauncherAction::end,
                ListView_GetNextItem(list_, -1, LVNI_SELECTED)};
    }
    std::optional<std::filesystem::path> choose_folder(const std::string &title,
                                                       const std::filesystem::path &initial) override {
        IFileOpenDialog *dialog = nullptr;
        if (FAILED(
                CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            throw std::runtime_error("Cannot create folder dialog");
        struct Release {
            IFileOpenDialog *p;
            ~Release() { p->Release(); }
        } release{dialog};
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(wide(title).c_str());
        if (!initial.empty()) {
            IShellItem *folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        const auto status = dialog->Show(main_.window);
        if (status == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return {};
        if (FAILED(status))
            throw std::runtime_error("Folder dialog failed");
        IShellItem *item = nullptr;
        if (FAILED(dialog->GetResult(&item)))
            throw std::runtime_error("No folder selected");
        PWSTR name = nullptr;
        const auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
        item->Release();
        if (FAILED(result))
            throw std::runtime_error("Invalid folder selection");
        std::filesystem::path path(name);
        CoTaskMemFree(name);
        return path;
    }
    std::optional<AddMod> configure_add(AddMod initial) override {
        Form form(L"Classic-Mod hinzufügen", 650, 295, main_.window);
        form.control(L"STATIC", L"Bezeichnung", 16, 20, 160, 22, 100);
        auto name = form.control(L"EDIT", wide(initial.name), 182, 16, 425, 26, 101, ES_AUTOHSCROLL);
        form.control(L"STATIC", L"Startjahr (nur Anzeige)", 16, 62, 164, 22, 102);
        auto year =
            form.control(L"EDIT", wide(std::to_string(initial.start_year)), 182, 58, 110, 26, 103, ES_NUMBER);
        auto migrate = form.control(L"BUTTON", L"Spielstände/Einstellungen übernehmen", 16, 100, 550, 26, 104,
                                    BS_AUTOCHECKBOX);
        SendMessageW(migrate, BM_SETCHECK, initial.migrate ? BST_CHECKED : BST_UNCHECKED, 0);
        form.control(L"STATIC", L"Spielstandsordner", 16, 146, 160, 22, 105);
        auto saves = form.control(L"EDIT", initial.saves.wstring(), 182, 140, 310, 26, 106, ES_AUTOHSCROLL);
        form.control(L"BUTTON", L"Auswählen …", 502, 140, 106, 26, 107);
        form.control(L"BUTTON", L"Importieren", 365, 204, 116, 32, 1);
        form.control(L"BUTTON", L"Abbrechen", 493, 204, 116, 32, 2);
        form.command = [&](int id) {
            if (id == 107) {
                if (auto path = choose_folder("Spielstandsordner wählen", path_from_utf8(text(saves))))
                    SetWindowTextW(saves, path->c_str());
            } else if (id == 1 || id == 2) {
                form.response = id;
                form.done = true;
            }
        };
        EnableWindow(main_.window, FALSE);
        std::optional<AddMod> out;
        while (form.run() == 1)
            try {
                initial.name = text(name);
                ModLibrary::validate_name(initial.name);
                auto value = text(year);
                size_t end = 0;
                initial.start_year = std::stoi(value, &end);
                if (end != value.size() || initial.start_year < 1900 || initial.start_year > 9998)
                    throw std::runtime_error("Startjahr muss zwischen 1900 und 9998 liegen");
                initial.saves = path_from_utf8(text(saves));
                initial.migrate = SendMessageW(migrate, BM_GETCHECK, 0, 0) == BST_CHECKED;
                out = initial;
                break;
            } catch (const std::exception &e) {
                message(e.what(), true);
            }
        EnableWindow(main_.window, TRUE);
        return out;
    }
    std::optional<std::string> rename(const std::string &old) override {
        Form form(L"Mod umbenennen", 480, 165, main_.window);
        auto input = form.control(L"EDIT", wide(old), 16, 16, 430, 26, 100, ES_AUTOHSCROLL);
        form.control(L"BUTTON", L"Umbenennen", 190, 65, 120, 32, 1);
        form.control(L"BUTTON", L"Abbrechen", 322, 65, 124, 32, 2);
        EnableWindow(main_.window, FALSE);
        const auto response = form.run();
        EnableWindow(main_.window, TRUE);
        return response == 1 ? std::optional<std::string>(text(input)) : std::nullopt;
    }
    bool confirm_delete(const std::string &name) override {
        return MessageBoxW(main_.window,
                           wide("Mod „" + name +
                                "“ löschen?\nSpielstände und persönliche Einstellungen bleiben erhalten.")
                               .c_str(),
                           L"OpenBMH", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    }
    void message(const std::string &value, bool error) override {
        MessageBoxW(main_.window, wide(value).c_str(), L"OpenBMH",
                    MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }
    void progress(const std::string &value, bool cancellable) override {
        if (!progress_) {
            EnableWindow(main_.window, FALSE);
            progress_ = std::make_unique<Form>(L"OpenBMH", 600, 150, main_.window);
            progress_->prevent_close = true;
            progress_label_ = progress_->control(L"STATIC", L"", 16, 18, 550, 40, 100);
            if (cancellable)
                progress_->control(L"BUTTON", L"Abbrechen", 440, 65, 115, 28, 1);
            ShowWindow(progress_->window, SW_SHOW);
        }
        SetWindowTextW(progress_label_, wide(value).c_str());
    }
    bool pump() override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return progress_ && progress_->done;
    }
    void finish_progress() override {
        if (progress_) {
            progress_.reset();
            EnableWindow(main_.window, TRUE);
            SetForegroundWindow(main_.window);
        }
    }
};
} // namespace
std::unique_ptr<LauncherUI> make_launcher_ui() { return std::make_unique<WinLauncher>(); }
} // namespace bmh::launch
