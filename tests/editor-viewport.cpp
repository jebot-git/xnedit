// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual editor handlers and rendered FLTK viewport on private Wayland.
#define main xnedit_application_main
#include "../fltk/main.cpp"
#undef main
#include <cassert>
#include <iostream>
static void settle() {
  for (int i = 0; i < 12; ++i) Fl::wait(.01);
  Fl::flush();
}
static void press(Editor *editor, int key) {
  Fl::e_keysym = key; Fl::e_state = 0;
  const bool enter = key == FL_Enter || key == FL_KP_Enter;
  Fl::e_length = enter ? 1 : 0;
  Fl::e_text = const_cast<char *>(enter ? "\r" : ""); Fl::e_number = FL_KEYDOWN;
  assert(editor->handle(FL_KEYDOWN));
  settle();
}
int main() {
  setenv("FLTK_BACKEND", "wayland", 1);
  dx::init();
  Window window;
  auto page = window.newDocument();
  window.show(); page->first->take_focus(); settle();
  auto editor = page->first;
  auto text = [&](const std::string &value) {
    page->doc.replace(0, page->doc.length(), value);
    page->refresh(value.size()); editor->show_insert_position(); settle();
  };
  const std::string longLine = "é日" + std::string(400, 'x');
  text(longLine);
  assert(editor->scroll_col() > 0);
  press(editor, FL_Enter);
  assert(page->doc.text() == longLine + "\n");
  assert(editor->insert_position() == page->text.length());
  assert(editor->scroll_col() == 0); // newline must expose the start of its line
  std::cout << "PASS Enter restores the left edge after a long UTF-8 line\n";
  text("    " + longLine);
  press(editor, FL_KP_Enter);
  assert(page->doc.text() == "    " + longLine + "\n    ");
  assert(editor->scroll_col() == 0);
  std::cout << "PASS Keypad Enter preserves indentation and reveals the new line\n";
  text(longLine);
  press(editor, FL_Home);
  assert(editor->insert_position() == 0 && editor->scroll_col() == 0);
  int x = 0, y = 0;
  assert(editor->position_to_xy(0, &x, &y));
  assert(x >= editor->x() + editor->linenumber_width());
  std::cout << "PASS Home exposes the complete first character beside the line numbers\n";
  // Replacement indentation belongs to the start of the selection, not its end.
  text("  alpha\n      beta");
  page->text.select(7, page->text.length());
  press(editor, FL_Enter);
  assert(page->doc.text() == "  alpha\n  ");
  std::cout << "PASS Selection replacement takes indentation from its insertion line\n";
  page->autoIndent = false;
  text("  " + longLine);
  press(editor, FL_Enter);
  assert(page->doc.text() == "  " + longLine + "\n" && editor->scroll_col() == 0);
  std::cout << "PASS Disabling auto-indent produces a newline at column zero\n";
  page->autoIndent = true;
  window.command("Split view");
  editor = page->second; editor->take_focus(); settle();
  text(longLine);
  assert(editor->scroll_col() > 0);
  press(editor, FL_Enter);
  assert(editor->scroll_col() == 0);
  std::cout << "PASS Split panes also restore the left edge on newline\n";
  page->doc.readOnly = true;
  auto before = page->doc.text();
  press(editor, FL_KP_Enter);
  assert(page->doc.text() == before);
  window.hide();
  std::cout << "PASS Keypad Enter respects read-only mode\n";
}
