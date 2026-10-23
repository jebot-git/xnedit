// SPDX-License-Identifier: GPL-2.0-or-later
// Native FLTK frontend for XNEdit's upstream buffer, regex and EditorConfig
// core.
#include "appearance.h"
#include "desktop-menu.h"
#include "document.h"
#include "gpu-window.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_utf8.h>
#include <chrono>
#include <csignal>
#include <fstream>
#include <glib.h>
#include <set>
#include <unordered_set>
class Window;
struct Page;
class Editor : public Fl_Text_Editor {
public:
  Page *page = nullptr;
  Editor(int x, int y, int w, int h) : Fl_Text_Editor(x, y, w, h) {}
  int handle(int) override;
};
struct Page : Fl_Group {
  Window *owner;
  Document doc;
  Fl_Text_Buffer text, style;
  Editor *first, *second;
  Editor *active = nullptr;
  bool syncing = false, split = false, wrap = false, syntax = true,
       autoIndent = true;
  std::vector<Fl_Text_Display::Style_Table_Entry> styles;
  uint64_t revision = 0, styled = ~uint64_t(0);
  fs::path recovery;
  std::string recoveryFingerprint;
  Page(Window *, int, int, int, int);
  ~Page() {
    text.remove_modify_callback(changed, this);
    first->buffer(nullptr);
    second->buffer(nullptr);
  }
  Editor *editor() { return active ? active : first; }
  static void changed(int, int, int, int, const char *, void *);
  void refresh(int cursor = -1);
  void highlight();
  void applyFont();
  void labelChanged();
  void resize(int x, int y, int w, int h) override {
    Fl_Group::resize(x, y, w, h);
    int half = split ? w / 2 : w;
    first->resize(x, y, half, h);
    second->resize(x + half, y, w - half, h);
  }
};
static fs::path configRoot() {
  const char *p = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
  return fs::path(p ? p : std::string(h ? h : "/tmp") + "/.config") /
         "xnedit-xr";
}
static fs::path recoveryRoot() {
  const char *p = getenv("XDG_STATE_HOME"), *h = getenv("HOME");
  return fs::path(p ? p : std::string(h ? h : "/tmp") + "/.local/state") /
         "xnedit-xr/recovery";
}
static void report(const std::string &s) { fl_alert("%s", s.c_str()); }
class Window : public dx::GpuWindow {
  DesktopMenuBar menu{0, 0, 1100, 30};
  Fl_Group toolbar{0, 30, 1100, 40};
  Fl_Tabs tabs{0, 70, 1100, 680};
  Fl_Group search{0, 0, 1100, 76};
  Fl_Input needle{80, 0, 390, 30, "Find:"},
      replacement{80, 35, 390, 30, "Replace:"};
  Fl_Check_Button regex{480, 0, 95, 30, "Regex"},
      sensitive{580, 0, 90, 30, "Case"};
  Fl_Button previous{680, 0, 85, 30, "Previous"}, next{775, 0, 80, 30, "Next"},
      one{480, 35, 100, 30, "Replace"}, all{590, 35, 115, 30, "Replace all"},
      closeSearch{1010, 0, 80, 30, "Close"};
  Fl_Box status{8, 755, 1084, 25};
  std::unique_ptr<DesktopMenu> desktop;
  std::vector<Page *> pages;
  std::vector<std::string> recent;
  uint64_t fontRevision = ~uint64_t(0);
  bool searchOpen = false, closing = false;
  std::chrono::steady_clock::time_point lastRecovery{};
  static void callback(Fl_Widget *widget, void *data) {
    auto self = (Window *)data;
    auto m = dynamic_cast<Fl_Menu_ *>(widget);
    if (m && m->mvalue())
      self->command(m->mvalue()->label());
    else if (widget->label())
      self->command(widget->label());
  }
  static void timer(void *data) {
    auto self = (Window *)data;
    self->poll();
    Fl::repeat_timeout(.2, timer, self);
  }
  static void unfinished(int, void *) {}

public:
  Window() : dx::GpuWindow(1100, 800, "XNEdit XR — FLTK") {
    // All member widgets have now been constructed; explicitly reparent rows.
    end();
    toolbar.end();
    search.end();
    tabs.end();
    add(menu);
    add(toolbar);
    add(tabs);
    add(search);
    add(status);
    search.add(needle);
    search.add(replacement);
    search.add(regex);
    search.add(sensitive);
    search.add(previous);
    search.add(next);
    search.add(one);
    search.add(all);
    search.add(closeSearch);
    for (auto item : std::vector<std::pair<const char *, int>>{
             {"File/New", FL_CTRL + 'n'},
             {"File/Open...", FL_CTRL + 'o'},
             {"File/Open with Encoding...", 0},
             {"File/Recent files...", 0},
             {"File/Save", FL_CTRL + 's'},
             {"File/Save as...", FL_CTRL + FL_SHIFT + 's'},
             {"File/Save all", 0},
             {"File/Reload from disk", 0},
             {"File/Close tab", FL_CTRL + 'w'},
             {"File/Quit", FL_CTRL + 'q'},
             {"Edit/Undo", FL_CTRL + 'z'},
             {"Edit/Redo", FL_CTRL + FL_SHIFT + 'z'},
             {"Edit/Cut", FL_CTRL + 'x'},
             {"Edit/Copy", FL_CTRL + 'c'},
             {"Edit/Paste", FL_CTRL + 'v'},
             {"Edit/Select all", FL_CTRL + 'a'},
             {"Edit/Indent", FL_CTRL + ']'},
             {"Edit/Unindent", FL_CTRL + '['},
             {"Edit/Duplicate line", FL_CTRL + 'd'},
             {"Edit/Delete line", FL_CTRL + FL_SHIFT + 'k'},
             {"Search/Find...", FL_CTRL + 'f'},
             {"Search/Replace...", FL_CTRL + 'h'},
             {"Search/Find next", FL_F + 3},
             {"Search/Find previous", FL_SHIFT + (FL_F + 3)},
             {"Search/Find selection", FL_CTRL + (FL_F + 3)},
             {"Search/Go to line...", FL_CTRL + 'l'},
             {"Search/Matching bracket", FL_CTRL + 'm'},
             {"View/Word wrap", 0},
             {"View/Line numbers", 0},
             {"View/Syntax highlighting", 0},
             {"View/Split view", 0},
             {"View/Next tab", FL_CTRL + FL_Page_Down},
             {"View/Previous tab", FL_CTRL + FL_Page_Up},
             {"Document/Read only", 0},
             {"Document/Auto indent", 0},
             {"Document/Use spaces", 0},
             {"Document/Tab width...", 0},
             {"Document/Encoding/UTF-8", 0},
             {"Document/Encoding/UTF-16LE", 0},
             {"Document/Encoding/UTF-16BE", 0},
             {"Document/Encoding/ISO-8859-1", 0},
             {"Document/Line endings/LF", 0},
             {"Document/Line endings/CRLF", 0},
             {"Document/Line endings/CR", 0},
             {"Document/Byte order mark", 0},
             {"Help/About XNEdit XR", 0},
             {"Help/Port status", 0}})
      menu.add(item.first, item.second, callback, this);
    toolbar.begin();
    for (auto item :
         std::vector<std::pair<const char *, int>>{{"New", 8},
                                                   {"Open...", 88},
                                                   {"Save", 178},
                                                   {"Undo", 258},
                                                   {"Redo", 338},
                                                   {"Find...", 418}}) {
      auto button = new Fl_Button(item.second, 34, 72, 30, item.first);
      button->callback(callback, this);
    }
    toolbar.end();
    search.hide();
    status.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    status.box(FL_FLAT_BOX);
    next.callback([](Fl_Widget *, void *p) { ((Window *)p)->find(false); },
                  this);
    previous.callback([](Fl_Widget *, void *p) { ((Window *)p)->find(true); },
                      this);
    one.callback([](Fl_Widget *, void *p) { ((Window *)p)->replace(false); },
                 this);
    all.callback([](Fl_Widget *, void *p) { ((Window *)p)->replace(true); },
                 this);
    closeSearch.callback(
        [](Fl_Widget *, void *p) { ((Window *)p)->showSearch(false); }, this);
    needle.when(FL_WHEN_ENTER_KEY_ALWAYS);
    needle.callback([](Fl_Widget *, void *p) { ((Window *)p)->find(false); },
                    this);
    tabs.callback(
        [](Fl_Widget *, void *p) {
          auto self = (Window *)p;
          self->updateTitle();
          if (self->current())
            self->current()->editor()->take_focus();
        },
        this);
    callback([](Fl_Widget *, void *p) { ((Window *)p)->quit(); }, this);
    menu.layoutChanged = [this] { resize(x(), y(), w(), h()); };
    desktop = std::make_unique<DesktopMenu>(*this, menu);
    resizable(tabs);
    size_range(640, 400);
    xclass("org.domed.XNEdit");
    loadSettings();
    resize(x(), y(), w(), h());
    Fl::add_timeout(.2, timer, this);
    end();
  }
  ~Window() {
    Fl::remove_timeout(timer, this);
    saveSettings();
    for (auto p : pages) {
      remove(*p);
      tabs.remove(*p);
      delete p;
    }
    pages.clear();
  }
  using dx::GpuWindow::callback;
  int handle(int event) override {
    if ((event == FL_KEYDOWN || event == FL_SHORTCUT) &&
        Fl::event_key() == FL_Escape) {
      if (searchOpen)
        showSearch(false);
      return 1;
    }
    return dx::GpuWindow::handle(event);
  }
  Page *current() { return dynamic_cast<Page *>(tabs.value()); }
  void resize(int x, int y, int w, int h) override {
    dx::GpuWindow::resize(x, y, w, h);
    fl_font(dx::font, dx::size);
    const int row = std::max(30, dx::size + 14), bar = row + 10;
    const int statusHeight = std::max(32, dx::size + 16);
    const int left = std::max(85, int(fl_width("Replace:")) + 16);
    const int cell = std::max(76, int(fl_width("Next")) + 20);
    const int top = menu.universal() ? 0 : row;
    menu.resize(0, 0, w, top);
    toolbar.resize(0, top, w, bar);
    int bx = 8;
    for (int i = 0; i < toolbar.children(); i++) {
      auto button = toolbar.child(i);
      int width = std::max(72, int(fl_width(button->label())) + 24);
      button->resize(bx, top + 4, width, row);
      bx += width + 8;
    }
    int sy = top + bar, searchHeight = 2 * row + 16;
    search.resize(0, sy, w, searchHeight);
    needle.resize(left, sy + 3, std::max(100, w - left - 210), row);
    regex.resize(w - 200, sy + 3, 75, row);
    sensitive.resize(w - 120, sy + 3, 75, row);
    closeSearch.resize(w - 40, sy + 3, 32, row);
    closeSearch.label("×");
    closeSearch.tooltip("Close search (Esc)");
    replacement.resize(left, sy + row + 8,
                       std::max(100, w - left - 4 * cell - 12), row);
    one.resize(w - 4 * cell, sy + row + 8, cell - 4, row);
    one.label("One");
    one.tooltip("Replace selected match");
    all.resize(w - 3 * cell, sy + row + 8, cell - 4, row);
    all.label("All");
    all.tooltip("Replace all matches");
    previous.resize(w - 2 * cell, sy + row + 8, cell - 4, row);
    previous.label("Prev");
    previous.tooltip("Find previous");
    next.resize(w - cell, sy + row + 8, cell - 4, row);
    int ty = sy + (searchOpen ? searchHeight : 0);
    tabs.resize(0, ty, w, std::max(80, h - ty - statusHeight));
    for (auto p : pages)
      p->resize(0, ty + row, w, std::max(40, h - ty - row - statusHeight));
    status.resize(8, h - statusHeight + 4, w - 16, statusHeight - 8);
    redraw();
  }
  Page *newDocument() {
    auto previousGroup = Fl_Group::current();
    tabs.begin();
    auto page = new Page(this, 0, tabs.y() + std::max(30, dx::size + 14), w(),
                         tabs.h() - std::max(30, dx::size + 14));
    tabs.end();
    Fl_Group::current(previousGroup);
    pages.push_back(page);
    tabs.value(page);
    page->show();
    page->applyFont();
    updateTitle();
    return page;
  }
  void open(const fs::path &file, const std::string &encoding = "UTF-8",
            int line = 0, bool readOnly = false) {
    try {
      auto full = fs::absolute(file).lexically_normal();
      for (auto p : pages)
        if (!p->doc.path.empty() && p->doc.path == full) {
          tabs.value(p);
          p->show();
          updateTitle();
          return;
        }
      auto model = std::make_unique<Document>();
      model->encoding = encoding;
      if (fs::exists(full))
        model->load(full);
      else {
        if (!fs::exists(full.parent_path()))
          throw std::runtime_error("Parent directory does not exist");
        model->path = full;
        model->editorConfig();
      }
      auto page = current();
      if (!page || page->doc.modified() || !page->doc.path.empty() ||
          page->doc.length())
        page = newDocument();
      page->doc.encoding = model->encoding;
      page->doc.eol = model->eol;
      page->doc.bom = model->bom;
      page->doc.readOnly = readOnly;
      page->doc.tabWidth = model->tabWidth;
      page->doc.indentWidth = model->indentWidth;
      page->doc.spaces = model->spaces;
      if (fs::exists(full))
        page->doc.load(full);
      else {
        page->doc.path = full;
        page->doc.assign("", true);
      }
      page->refresh();
      page->first->buffer()->tab_distance(page->doc.tabWidth);
      if (line > 0)
        gotoLine(page, line);
      recent.erase(std::remove(recent.begin(), recent.end(), full.string()),
                   recent.end());
      recent.insert(recent.begin(), full.string());
      if (recent.size() > 30)
        recent.resize(30);
      saveSettings();
      page->first->take_focus();
    } catch (const std::exception &e) {
      report(e.what());
    }
  }
  void chooser(bool encoded = false) {
    std::string encoding = "UTF-8";
    if (encoded) {
      auto value = fl_input("Source encoding (UTF-8, UTF-16LE, UTF-16BE, "
                            "ISO-8859-1, WINDOWS-1252)",
                            "UTF-8");
      if (!value)
        return;
      encoding = value;
    }
    Fl_Native_File_Chooser pick;
    pick.title("Open text files");
    pick.type(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
    if (pick.show())
      return;
    for (int i = 0; i < pick.count(); i++)
      open(pick.filename(i), encoding);
  }
  bool save(Page *page, bool as = false) {
    if (!page)
      return true;
    if (page->doc.readOnly && !as) {
      status.copy_label(
          "Read-only document: use Save as to write another file");
      return false;
    }
    try {
      fs::path target = page->doc.path;
      bool choseTarget = as || target.empty();
      if (choseTarget) {
        Fl_Native_File_Chooser pick;
        pick.title("Save text file");
        pick.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
        pick.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
        if (!target.empty())
          pick.preset_file(target.c_str());
        if (pick.show())
          return false;
        target = pick.filename();
      }
      bool changed = !page->doc.path.empty() && fs::exists(page->doc.path) &&
                     page->doc.diskChanged();
      if (changed &&
          fl_choice(
              "The file changed on disk. Replace it with the editor contents?",
              "Cancel", "Replace", nullptr) != 1)
        return false;
      page->doc.save(target, changed || choseTarget);
      if (!page->recovery.empty())
        fs::remove(page->recovery);
      page->labelChanged();
      updateTitle();
      return true;
    } catch (const std::exception &e) {
      report(e.what());
      return false;
    }
  }
  bool allowClose(Page *page) {
    if (!page->doc.modified())
      return true;
    int answer =
        fl_choice("Save changes to %s?", "Cancel", "Save", "Discard",
                  page->doc.path.empty() ? "Untitled"
                                         : page->doc.path.filename().c_str());
    if (answer == 0)
      return false;
    if (answer == 1 && !save(page))
      return false;
    if (!page->recovery.empty())
      fs::remove(page->recovery);
    return true;
  }
  void closeTab() {
    auto p = current();
    if (!p || !allowClose(p))
      return;
    tabs.remove(p);
    pages.erase(std::remove(pages.begin(), pages.end(), p), pages.end());
    Fl::delete_widget(p);
    if (pages.empty())
      newDocument();
    else {
      tabs.value(pages.back());
      pages.back()->show();
    }
    updateTitle();
  }
  void quit() {
    if (closing)
      return;
    for (auto p : pages)
      if (!allowClose(p))
        return;
    closing = true;
    hide();
  }
  void updateTitle() {
    auto p = current();
    if (!p)
      return;
    std::string title =
        (p->doc.path.empty() ? "Untitled" : p->doc.path.filename().string()) +
        (p->doc.modified() ? " *" : "") + " — XNEdit XR";
    copy_label(title.c_str());
    redraw();
  }
  void showSearch(bool value) {
    searchOpen = value;
    if (value)
      search.show();
    else
      search.hide();
    resize(x(), y(), w(), h());
    if (value) {
      needle.take_focus();
      needle.insert_position(0, needle.size());
    } else if (current())
      current()->editor()->take_focus();
  }
  void find(bool reverse) {
    auto p = current();
    if (!p)
      return;
    try {
      if (!*needle.value()) {
        showSearch(true);
        return;
      }
      int a, b;
      int pos = p->editor()->insert_position();
      if (p->text.selection_position(&a, &b))
        pos = reverse ? a : b;
      auto match = p->doc.find(needle.value(), pos, regex.value(),
                               sensitive.value(), reverse);
      if (!match)
        match = p->doc.find(needle.value(), reverse ? p->doc.length() : 0,
                            regex.value(), sensitive.value(), reverse);
      if (!match) {
        status.copy_label("No match");
        return;
      }
      p->text.select(match->start, match->end);
      p->editor()->insert_position(reverse ? match->start : match->end);
      p->editor()->show_insert_position();
      p->editor()->take_focus();
    } catch (const std::exception &e) {
      report(e.what());
    }
  }
  void replace(bool every) {
    auto p = current();
    if (!p || p->doc.readOnly)
      return;
    try {
      if (every) {
        int n = p->doc.replaceAll(needle.value(), replacement.value(),
                                  regex.value(), sensitive.value());
        p->refresh();
        status.copy_label((std::to_string(n) + " replacements").c_str());
      } else {
        int a, b;
        if (p->text.selection_position(&a, &b)) {
          auto match =
              p->doc.find(needle.value(), a, regex.value(), sensitive.value(),
                          false, replacement.value());
          if (match && match->start == a && match->end == b) {
            p->doc.replace(a, b, match->replacement);
            p->refresh(a + match->replacement.size());
          }
        }
        find(false);
      }
    } catch (const std::exception &e) {
      report(e.what());
    }
  }
  void gotoLine(Page *p, int line) {
    line = std::max(line, 1);
    int at = p->text.skip_lines(0, line - 1);
    p->editor()->insert_position(at);
    p->editor()->show_insert_position();
    p->editor()->take_focus();
  }
  void command(const std::string &name) {
    auto p = current();
    auto editor = p ? p->editor() : nullptr;
    try {
      if (name == "New")
        newDocument();
      else if (name == "Open...")
        chooser();
      else if (name == "Open with Encoding...")
        chooser(true);
      else if (name == "Recent files...") {
        if (recent.empty()) {
          status.copy_label("No recent files");
          return;
        }
        std::string values;
        for (auto &s : recent)
          values += s + "\n";
        auto value = fl_input("Open a recent path:", recent.front().c_str());
        if (value)
          open(value);
      } else if (name == "Save")
        save(p);
      else if (name == "Save as...")
        save(p, true);
      else if (name == "Save all") {
        for (auto page : pages)
          if (!save(page))
            break;
      } else if (name == "Reload from disk") {
        if (p && !p->doc.path.empty() && allowClose(p)) {
          p->doc.load(p->doc.path);
          p->refresh();
        }
      } else if (name == "Close tab")
        closeTab();
      else if (name == "Quit")
        quit();
      else if (name == "Undo" && p && !p->doc.readOnly) {
        auto change = p->doc.undo();
        if (change)
          p->refresh(change->pos + change->before.size());
      } else if (name == "Redo" && p && !p->doc.readOnly) {
        auto change = p->doc.redo();
        if (change)
          p->refresh(change->pos + change->after.size());
      } else if (name == "Copy" && editor)
        Fl_Text_Editor::kf_copy(0, editor);
      else if (name == "Cut" && editor && !p->doc.readOnly)
        Fl_Text_Editor::kf_cut(0, editor);
      else if (name == "Paste" && editor && !p->doc.readOnly)
        Fl_Text_Editor::kf_paste(0, editor);
      else if (name == "Select all" && editor)
        Fl_Text_Editor::kf_select_all(0, editor);
      else if (name == "Find..." || name == "Replace...")
        showSearch(true);
      else if (name == "Find next")
        find(false);
      else if (name == "Find previous")
        find(true);
      else if (name == "Find selection" && p) {
        auto text = p->text.selection_text();
        needle.value(text);
        free(text);
        find(false);
      } else if (name == "Go to line..." && p) {
        auto value = fl_input("Line number:", "1");
        if (value)
          gotoLine(p, std::stoi(value));
      } else if (name == "Matching bracket" && p) {
        auto s = p->doc.text();
        int pos = editor->insert_position();
        if (pos >= int(s.size()) ||
            std::string("()[]{}").find(s[pos]) == std::string::npos)
          pos--;
        if (pos < 0)
          return;
        std::string pairs = "()[]{}";
        auto k = pairs.find(s[pos]);
        if (k == std::string::npos)
          return;
        int step = k % 2 ? -1 : 1, level = 1;
        char open = s[pos], close = pairs[k % 2 ? k - 1 : k + 1];
        for (int i = pos + step; i >= 0 && i < int(s.size()); i += step) {
          if (s[i] == open)
            level++;
          else if (s[i] == close && !--level) {
            editor->insert_position(i);
            editor->show_insert_position();
            break;
          }
        }
      } else if ((name == "Indent" || name == "Unindent") && p &&
                 !p->doc.readOnly) {
        int a, b;
        if (!p->text.selection_position(&a, &b))
          a = b = editor->insert_position();
        p->doc.indent(a, b, name == "Unindent");
        p->refresh(a);
      } else if ((name == "Duplicate line" || name == "Delete line") && p &&
                 !p->doc.readOnly) {
        int a = p->text.line_start(editor->insert_position()),
            b = p->text.line_end(a);
        if (b < p->text.length())
          b++;
        if (name == "Delete line")
          p->doc.replace(a, b, "");
        else
          p->doc.replace(b, b, p->doc.text().substr(a, b - a));
        p->refresh(a);
      } else if (name == "Word wrap" && p) {
        p->wrap = !p->wrap;
        for (auto e : {p->first, p->second})
          e->wrap_mode(p->wrap ? Fl_Text_Display::WRAP_AT_BOUNDS
                               : Fl_Text_Display::WRAP_NONE,
                       0);
      } else if (name == "Line numbers" && p) {
        int width = p->first->linenumber_width() ? 0 : dx::size * 4;
        for (auto e : {p->first, p->second})
          e->linenumber_width(width);
      } else if (name == "Syntax highlighting" && p) {
        p->syntax = !p->syntax;
        p->styled = ~uint64_t(0);
        p->highlight();
      } else if (name == "Split view" && p) {
        p->split = !p->split;
        if (p->split)
          p->second->show();
        else {
          p->second->hide();
          p->active = p->first;
        }
        p->resize(p->x(), p->y(), p->w(), p->h());
        redraw();
      } else if ((name == "Next tab" || name == "Previous tab") &&
                 !pages.empty()) {
        auto i = std::find(pages.begin(), pages.end(), p) - pages.begin();
        i = (i + pages.size() + (name == "Next tab" ? 1 : -1)) % pages.size();
        tabs.value(pages[i]);
        pages[i]->show();
        updateTitle();
        pages[i]->first->take_focus();
      } else if (name == "Read only" && p)
        p->doc.readOnly = !p->doc.readOnly;
      else if (name == "Auto indent" && p)
        p->autoIndent = !p->autoIndent;
      else if (name == "Use spaces" && p)
        p->doc.spaces = !p->doc.spaces;
      else if (name == "Tab width..." && p) {
        auto v = fl_input("Tab and indentation width (1–32)",
                          std::to_string(p->doc.tabWidth).c_str());
        if (v) {
          p->doc.tabWidth = p->doc.indentWidth =
              std::clamp(std::stoi(v), 1, 32);
          p->text.tab_distance(p->doc.tabWidth);
        }
      } else if ((name == "UTF-8" || name == "UTF-16LE" || name == "UTF-16BE" ||
                  name == "ISO-8859-1") &&
                 p)
        p->doc.encoding = name;
      else if ((name == "LF" || name == "CRLF" || name == "CR") && p)
        p->doc.eol = name;
      else if (name == "Byte order mark" && p)
        p->doc.bom = !p->doc.bom;
      else if (name == "About XNEdit XR")
        fl_message("XNEdit XR 0.1.0 — native FLTK frontend\nBased on XNEdit "
                   "1.6.3 / upstream 0ffad9c2.\nUpstream text buffer, regular "
                   "expressions and EditorConfig.\nGPL-2.0-or-later. "
                   "https://github.com/jebot-git/xnedit");
      else if (name == "Port status")
        fl_message("Native FLTK: tabs, split views, undo/redo, "
                   "search/replace,\nsyntax highlighting, encoding and line "
                   "endings, EditorConfig,\nrecovery copies, clipboard and "
                   "primary selection.\n\nThe macro interpreter and remote xnc "
                   "service are outside this fork.\n"
                   "Rectangular selection and custom highlight-pattern dialogs "
                   "are not yet exposed.\n"
                   "The original frontend remains available through explicit "
                   "legacy make targets.");
      if (p) {
        p->labelChanged();
        p->applyFont();
      }
      redraw();
    } catch (const std::exception &e) {
      report(e.what());
    }
  }
  void loadSettings() {
    try {
      Json j;
      std::ifstream(configRoot() / "settings.json") >> j;
      recent = j.value("recent", std::vector<std::string>{});
      if (recent.size() > 30)
        recent.resize(30);
      size(std::clamp(j.value("width", 1100), 640, 2400),
           std::clamp(j.value("height", 800), 400, 1600));
    } catch (...) {
    }
  }
  void saveSettings() {
    try {
      fs::create_directories(configRoot());
      atomicWrite(
          configRoot() / "settings.json",
          Json{{"recent", recent}, {"width", w()}, {"height", h()}}.dump(2),
          true);
    } catch (...) {
    }
  }
  void poll() {
    auto p = current();
    if (!p)
      return;
    if (fontRevision != dx::font_revision) {
      fontRevision = dx::font_revision;
      for (auto page : pages)
        page->applyFont();
      resize(x(), y(), w(), h());
    }
    for (auto page : pages)
      if (page->revision != page->styled)
        page->highlight();
    auto e = p->editor();
    int cursor = e->insert_position();
    int line = p->text.count_lines(0, cursor) + 1;
    int col =
        p->text.count_displayed_characters(p->text.line_start(cursor), cursor) +
        1;
    std::string info =
        (p->doc.readOnly ? "Read only · " : "") + std::to_string(line) + ":" +
        std::to_string(col) + " · " + p->doc.encoding +
        (p->doc.bom ? " BOM" : "") + " · " + p->doc.eol + " · " +
        (p->doc.diskChanged() && !p->doc.path.empty() ? "Changed on disk · "
                                                      : "") +
        (p->doc.path.empty() ? "Untitled" : p->doc.path.string());
    status.copy_label(info.c_str());
    dx::enable_menu(menu, "Edit/Undo", p->doc.canUndo() && !p->doc.readOnly);
    dx::enable_menu(menu, "Edit/Redo", p->doc.canRedo() && !p->doc.readOnly);
    dx::enable_menu(menu, "File/Save", !p->doc.readOnly);
    if (std::chrono::steady_clock::now() - lastRecovery >
        std::chrono::seconds(5)) {
      lastRecovery = std::chrono::steady_clock::now();
      recoverSave();
    }
  }
  void recoverSave() {
    try {
      fs::create_directories(recoveryRoot());
      fs::permissions(recoveryRoot(), fs::perms::owner_all);
      for (auto page : pages) {
        if (page->doc.modified()) {
          auto fingerprint = std::to_string(page->revision) + ":" +
                             page->doc.encoding + ":" + page->doc.eol + ":" +
                             std::to_string(page->doc.bom);
          if (fingerprint == page->recoveryFingerprint &&
              !page->recovery.empty() && fs::exists(page->recovery))
            continue;
          if (page->recovery.empty())
            page->recovery =
                recoveryRoot() / (std::to_string(getpid()) + "-" +
                                  std::to_string((uintptr_t)page) + ".json");
          atomicWrite(page->recovery,
                      Json{{"pid", getpid()},
                           {"path", page->doc.path.string()},
                           {"disk_digest", page->doc.originalDigest()},
                           {"text", page->doc.text()},
                           {"encoding", page->doc.encoding},
                           {"eol", page->doc.eol},
                           {"bom", page->doc.bom}}
                          .dump(),
                      true);
          page->recoveryFingerprint = fingerprint;
        } else if (!page->recovery.empty())
          fs::remove(page->recovery);
      }
    } catch (const std::exception &e) {
      status.copy_label(
          (std::string("Recovery copy failed: ") + e.what()).c_str());
    }
  }
  void recover() {
    try {
      if (!fs::exists(recoveryRoot()))
        return;
      for (auto &entry : fs::directory_iterator(recoveryRoot())) {
        if (entry.path().extension() != ".json" || entry.is_symlink() ||
            entry.file_size() > 512 * 1024 * 1024)
          continue;
        Json j;
        std::ifstream(entry.path()) >> j;
        int pid = j.value("pid", 0);
        if (pid > 0 && (kill(pid, 0) == 0 || errno == EPERM))
          continue;
        auto path = j.value("path", std::string());
        int answer = fl_choice("Recover unsaved edits for %s?",
                               "Keep for later", "Recover", "Discard",
                               path.empty() ? "Untitled" : path.c_str());
        if (answer == 2) {
          fs::remove(entry.path());
          continue;
        }
        if (answer == 1) {
          Page *p = nullptr;
          for (auto page : pages)
            if (!page->doc.modified() && page->doc.path == fs::path(path)) {
              p = page;
              break;
            }
          if (!p)
            p = newDocument();
          tabs.value(p);
          p->show();
          p->doc.encoding = j.value("encoding", "UTF-8");
          if (!path.empty() && fs::exists(path))
            p->doc.load(path);
          else {
            p->doc.path = path;
            p->doc.assign("", true);
          }
          p->doc.encoding = j.value("encoding", "UTF-8");
          p->doc.eol = j.value("eol", "LF");
          p->doc.bom = j.value("bom", false);
          if (j.contains("disk_digest"))
            p->doc.restoreDigest(j["disk_digest"]);
          p->doc.replace(0, p->doc.length(), j.value("text", std::string()));
          p->recovery = entry.path();
          p->refresh();
        }
      }
    } catch (const std::exception &e) {
      report(std::string("Recovery: ") + e.what());
    }
  }
};
Page::Page(Window *w, int x, int y, int width, int height)
    : Fl_Group(x, y, width, height, "Untitled"), owner(w) {
  first = new Editor(x, y, width, height);
  second = new Editor(x + width / 2, y, width / 2, height);
  second->hide();
  first->page = second->page = this;
  for (auto e : {first, second}) {
    e->buffer(text);
    e->linenumber_width(dx::size * 4);
    e->linenumber_format("%d");
    e->add_key_binding('z', FL_CTRL, [](int, Fl_Text_Editor *e) {
      ((Editor *)e)->page->owner->command("Undo");
      return 1;
    });
    e->add_key_binding('z', FL_CTRL | FL_SHIFT, [](int, Fl_Text_Editor *e) {
      ((Editor *)e)->page->owner->command("Redo");
      return 1;
    });
  }
  text.canUndo(0);
  text.add_modify_callback(changed, this);
  end();
  resizable(first);
  applyFont();
}
void Page::labelChanged() {
  std::string value =
      (doc.path.empty() ? "Untitled" : doc.path.filename().string()) +
      (doc.modified() ? " *" : "");
  copy_label(value.c_str());
  owner->updateTitle();
}
void Page::changed(int pos, int inserted, int deleted, int, const char *,
                   void *data) {
  auto p = (Page *)data;
  if (p->syncing || (!inserted && !deleted))
    return;
  try {
    char *s = p->text.text_range(pos, pos + inserted);
    std::string value = s;
    free(s);
    p->doc.replace(pos, pos + deleted, value);
    p->revision++;
    p->labelChanged();
  } catch (const std::exception &e) {
    report(e.what());
    p->refresh();
  }
}
void Page::refresh(int cursor) {
  syncing = true;
  auto s = doc.text();
  text.text(s.c_str());
  text.tab_distance(doc.tabWidth);
  syncing = false;
  revision++;
  if (cursor >= 0) {
    editor()->insert_position(std::min(cursor, text.length()));
    editor()->show_insert_position();
  }
  labelChanged();
  highlight();
}
void Page::applyFont() {
  for (auto e : {first, second}) {
    e->textfont(dx::font);
    e->textsize(dx::size);
    e->linenumber_font(dx::font);
    e->linenumber_size(dx::size);
    e->linenumber_fgcolor(FL_FOREGROUND_COLOR);
    e->linenumber_bgcolor(FL_BACKGROUND_COLOR);
    e->color(FL_BACKGROUND2_COLOR);
    e->textcolor(FL_FOREGROUND_COLOR);
    e->cursor_color(FL_FOREGROUND_COLOR);
    e->selection_color(FL_SELECTION_COLOR);
  }
  bool light = false;
  unsigned char r, g, b;
  Fl::get_color(FL_BACKGROUND_COLOR, r, g, b);
  light = (int(r) + g + b) > 384;
  styles = {
      {FL_FOREGROUND_COLOR, dx::font, dx::size, 0, FL_BACKGROUND2_COLOR},
      {fl_rgb_color(light ? 30 : 130, light ? 110 : 190, light ? 65 : 145),
       dx::font, dx::size, 0, FL_BACKGROUND2_COLOR},
      {fl_rgb_color(light ? 140 : 230, light ? 60 : 175, light ? 30 : 105),
       dx::font, dx::size, 0, FL_BACKGROUND2_COLOR},
      {fl_rgb_color(light ? 25 : 120, light ? 65 : 175, light ? 170 : 245),
       dx::font, dx::size, 0, FL_BACKGROUND2_COLOR},
      {fl_rgb_color(light ? 140 : 210, light ? 35 : 150, light ? 120 : 220),
       dx::font, dx::size, 0, FL_BACKGROUND2_COLOR}};
  for (auto e : {first, second})
    e->highlight_data(&style, styles.data(), styles.size(), 'A', nullptr,
                      nullptr);
  styled = ~uint64_t(0);
  highlight();
}
void Page::highlight() {
  auto s = doc.text();
  std::string colors(s.size(), 'A');
  auto ext = doc.path.extension().string();
  bool python = ext == ".py" || ext == ".sh" || ext == ".toml" ||
                ext == ".yaml" || ext == ".yml";
  static const std::unordered_set<std::string> words = {
      "if",        "else",      "elif",     "for",     "while",  "return",
      "break",     "continue",  "class",    "struct",  "public", "private",
      "protected", "static",    "const",    "void",    "int",    "bool",
      "char",      "float",     "double",   "auto",    "def",    "import",
      "from",      "as",        "try",      "catch",   "except", "finally",
      "throw",     "true",      "false",    "True",    "False",  "None",
      "null",      "namespace", "using",    "include", "switch", "case",
      "default",   "match",     "fn",       "let",     "mut",    "pub",
      "impl",      "use",       "in",       "not",     "and",    "or",
      "async",     "await",     "function", "var",     "local",  "end",
      "then",      "do"};
  if (syntax && s.size() < 4 * 1024 * 1024)
    for (size_t i = 0; i < s.size();) {
      size_t start = i;
      char c = s[i];
      if ((python && c == '#') ||
          (!python && c == '/' && i + 1 < s.size() && s[i + 1] == '/')) {
        while (i < s.size() && s[i] != '\n')
          i++;
        std::fill(colors.begin() + start, colors.begin() + i, 'B');
      } else if (!python && c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
        i += 2;
        while (i + 1 < s.size() && (s[i] != '*' || s[i + 1] != '/'))
          i++;
        i = std::min(s.size(), i + 2);
        std::fill(colors.begin() + start, colors.begin() + i, 'B');
      } else if (c == '"' || c == '\'') {
        i++;
        while (i < s.size()) {
          if (s[i] == '\\' && i + 1 < s.size()) {
            i += 2;
            continue;
          }
          if (s[i++] == c)
            break;
        }
        std::fill(colors.begin() + start, colors.begin() + i, 'C');
      } else if (g_ascii_isdigit(c)) {
        while (i < s.size() &&
               (g_ascii_isalnum(s[i]) || s[i] == '.' || s[i] == '_'))
          i++;
        std::fill(colors.begin() + start, colors.begin() + i, 'E');
      } else if (g_ascii_isalpha(c) || c == '_') {
        while (i < s.size() && (g_ascii_isalnum(s[i]) || s[i] == '_'))
          i++;
        if (words.count(s.substr(start, i - start)))
          std::fill(colors.begin() + start, colors.begin() + i, 'D');
      } else
        i++;
    }
  style.text(colors.c_str());
  styled = revision;
  first->redisplay_range(0, text.length());
  second->redisplay_range(0, text.length());
}
int Editor::handle(int event) {
  if (!page)
    return Fl_Text_Editor::handle(event);
  if (event == FL_FOCUS)
    page->active = this;
  if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
    int key = Fl::event_key();
    int mods = Fl::event_state() & (FL_CTRL | FL_SHIFT | FL_ALT | FL_META);
    if ((mods & FL_CTRL) && !(mods & (FL_ALT | FL_META))) {
      const char *action = nullptr;
      if (key == FL_Page_Down)
        action = "Next tab";
      else if (key == FL_Page_Up)
        action = "Previous tab";
      else if (key == 'f')
        action = "Find...";
      else if (key == 'h')
        action = "Replace...";
      else if (key == 'n')
        action = "New";
      else if (key == 'o')
        action = "Open...";
      else if (key == 's')
        action = (mods & FL_SHIFT) ? "Save as..." : "Save";
      else if (key == 'q')
        action = "Quit";
      else if (key == 'w')
        action = "Close tab";
      else if (key == 'l')
        action = "Go to line...";
      else if (key == 'm')
        action = "Matching bracket";
      else if (key == 'd')
        action = "Duplicate line";
      else if (key == ']')
        action = "Indent";
      else if (key == '[')
        action = "Unindent";
      if (action) {
        page->owner->command(action);
        return 1;
      }
    }
    if (key == 'z' && (mods & FL_CTRL)) {
      page->owner->command(mods & FL_SHIFT ? "Redo" : "Undo");
      return 1;
    }
    if (page->doc.readOnly) {
      if (key == FL_BackSpace || key == FL_Delete || key == FL_Enter ||
          key == FL_Tab || ((mods & FL_CTRL) && (key == 'v' || key == 'x')) ||
          (!(mods & (FL_CTRL | FL_ALT | FL_META)) && Fl::event_length() > 0))
        return 1;
    }
    if (!page->doc.readOnly && key == FL_Tab &&
        !(mods & (FL_CTRL | FL_ALT | FL_META))) {
      if (mods & FL_SHIFT) {
        page->owner->command("Unindent");
        return 1;
      }
      if (buffer()->selected()) {
        page->owner->command("Indent");
        return 1;
      }
      if (page->doc.spaces) {
        int pos = insert_position(), col = buffer()->count_displayed_characters(
                                         buffer()->line_start(pos), pos);
        std::string spaces(page->doc.indentWidth - col % page->doc.indentWidth,
                           ' ');
        insert(spaces.c_str());
        return 1;
      }
    }
    if (!page->doc.readOnly && key == FL_Enter && mods == 0 &&
        page->autoIndent) {
      int pos = insert_position(), start = buffer()->line_start(pos);
      char *line = buffer()->text_range(start, pos);
      std::string padding;
      for (char *p = line; *p == ' ' || *p == '\t'; p++)
        padding += *p;
      free(line);
      int a, b;
      if (buffer()->selection_position(&a, &b)) {
        buffer()->remove_selection();
        insert_position(a);
      }
      insert(("\n" + padding).c_str());
      return 1;
    }
  }
  if (event == FL_PASTE && page->doc.readOnly)
    return 1;
  return Fl_Text_Editor::handle(event);
}
int main(int argc, char **argv) try {
  std::vector<std::pair<std::string, int>> files;
  int line = 0;
  bool readOnly = false;
  bool x11 =
      getenv("FLTK_BACKEND") && std::string(getenv("FLTK_BACKEND")) == "x11";
  std::string encoding = "UTF-8";
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--version") {
      puts("XNEdit XR 0.1.0 FLTK / upstream 0ffad9c2");
      return 0;
    }
    if (arg == "--build-options") {
      puts("FLTK=1.4.5 MOTIF=OFF WAYLAND=ON X11="
#if XNEDIT_ENABLE_X11
           "ON"
#else
           "OFF"
#endif
           " UPSTREAM_BUFFER=ON UPSTREAM_REGEX=ON EDITORCONFIG=ON");
      return 0;
    }
    if (arg == "--x11")
      x11 = true;
    else if (arg == "--read-only" || arg == "-read")
      readOnly = true;
    else if ((arg == "-line" || arg == "--line") && i + 1 < argc)
      line = std::stoi(argv[++i]);
    else if (arg == "--encoding" && i + 1 < argc)
      encoding = argv[++i];
    else if (arg.size() > 1 && arg[0] == '+' && g_ascii_isdigit(arg[1]))
      line = std::stoi(arg.substr(1));
    else if (arg == "--" && i + 1 < argc) {
      for (++i; i < argc; i++)
        files.emplace_back(argv[i], line);
      break;
    } else if (arg == "--new-window") {
    } else if (!arg.empty() && arg[0] == '-')
      throw std::runtime_error("Unknown option: " + arg);
    else {
      files.emplace_back(arg, line);
      line = 0;
    }
  }
#if !XNEDIT_ENABLE_X11
  if (x11)
    throw std::runtime_error("X11 support was not built; configure with "
                             "-DXNEDIT_ENABLE_X11=ON to opt in");
#endif
  setenv("FLTK_BACKEND", x11 ? "x11" : "wayland", 1);
  dx::init();
  Window window;
  for (auto &[file, n] : files)
    window.open(file, encoding, n, readOnly);
  if (!window.current())
    window.newDocument();
  dx::style(&window);
  window.show();
  window.recover();
  if (window.current())
    window.current()->first->take_focus();
  return Fl::run();
} catch (const std::exception &e) {
  fprintf(stderr, "xnedit: %s\n", e.what());
  return 1;
}
