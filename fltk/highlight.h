// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace highlight {
enum class Language { Automatic, Plain, Cpp, Rust, Python, JavaScript, Go, Java,
  CSharp, Shell, Lua, Sql, Json, Yaml, Toml, Ini, Html, Xml, Css, Markdown, Make,
  CMake, Diff, Gemini, Gopher, GopherPlus, Nex, Spartan, Scroll };
enum Style : char { Normal='A', Comment, String, Keyword, Number, Type, Function,
  Operator, Directive, Key, Heading, Variable, Addition, Deletion };
struct Mode { Language language; const char *name; };
const std::vector<Mode> &modes();
const char *name(Language);
Language detect(const std::filesystem::path &, std::string_view);
// One ASCII style byte per UTF-8 source byte, as required by Fl_Text_Display.
// Large documents retain plain styling to bound interactive lexer work.
constexpr size_t limit = 4 * 1024 * 1024;
std::string colorize(std::string_view, Language);
}
