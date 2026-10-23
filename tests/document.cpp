#include "document.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main() {
  Document d;
  d.assign("hello žluťoučký\n", true);
  d.replace(6, 6, "world ");
  assert(d.modified());
  d.undo();
  assert(!d.modified());
  d.redo();
  assert(d.text() == "hello world žluťoučký\n");
  d.assign("foo123 foo45\n", true);
  auto m = d.find("foo([0-9]+)", 0, true, true, false, "num=\\1");
  assert(m && m->replacement == "num=123");
  assert(d.replaceAll("foo([0-9]+)", "num=\\1", true, true) == 2);
  assert(d.text() == "num=123 num=45\n");
  d.undo();
  assert(d.text() == "foo123 foo45\n");
  std::string enc = "UTF-8";
  bool bom = false;
  auto bytes = encodeFile("Příliš\n", "UTF-16LE", true, "CRLF");
  auto utf = utf8Decode(bytes, enc, bom);
  assert(enc == "UTF-16LE" && bom && utf == "Příliš\r\n");
  char pattern[] = "/tmp/xnedit-core-XXXXXX";
  auto dir = mkdtemp(pattern);
  assert(dir);
  auto path = fs::path(dir) / "test.txt";
  d.save(path);
  Document other;
  other.load(path);
  assert(other.text() == d.text());
  atomicWrite(path, "external");
  bool conflict = false;
  try {
    d.save(path);
  } catch (...) {
    conflict = true;
  }
  assert(conflict);
  assert(std::ifstream(path).get() == 'e');
  d.assign("Příliš žluťoučký", true);
  auto unicode = d.find("ŽLUŤOUČKÝ", 0, false, false);
  assert(unicode && unicode->start == int(std::string("Příliš ").size()));
  d.assign("", true);
  assert(d.replaceAll("^", "x", true, true) == 1);
  assert(d.text() == "x");
  d.undo();
  assert(d.text().empty());
  d.assign("a\nb\n", true);
  d.indent(0, 2, false);
  assert(d.text() == "    a\nb\n");
  d.undo();
  assert(d.text() == "a\nb\n");
  d.assign("ž", true);
  bool split = false;
  try {
    d.replace(1, 1, "x");
  } catch (...) {
    split = true;
  }
  assert(split);
  auto fresh = fs::path(dir) / "new.txt";
  Document created;
  created.path = fresh;
  created.replace(0, 0, "new");
  created.save(fresh);
  assert(!created.modified());
  created.eol = "CRLF";
  assert(created.modified());
  created.save(fresh);
  assert(!created.modified());
  auto project = fs::path(dir) / "project";
  fs::create_directory(project);
  atomicWrite(project / ".editorconfig",
              "root = true\n[*.py]\nindent_style = space\nindent_size = "
              "2\ntab_width = 2\nend_of_line = crlf\n");
  atomicWrite(project / "code.py", "print(1)\n");
  Document configured;
  configured.load(project / "code.py");
  assert(configured.tabWidth == 2 && configured.indentWidth == 2 &&
         configured.spaces && configured.eol == "CRLF");
  d.assign("a a", true);
  bool tooLarge = false;
  try {
    d.replaceAll("a", std::string(33 * 1024 * 1024, 'x'), false, true);
  } catch (...) {
    tooLarge = true;
  }
  assert(tooLarge && d.text() == "a a" && !d.modified() && !d.canUndo());
  fs::remove_all(dir);
  std::cout << "PASS upstream buffer undo/redo, regex captures, replacement, "
               "UTF-16/CRLF roundtrip and external-write protection\n";
}
