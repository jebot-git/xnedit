// SPDX-License-Identifier: GPL-2.0-or-later
#include "document.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <glib.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
static std::string read(const fs::path &path) {
  auto size = fs::file_size(path);
  if (size > 64 * 1024 * 1024)
    throw std::runtime_error(
        "File exceeds the 64 MiB interactive editing limit");
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot read " + path.string());
  return std::string(std::istreambuf_iterator<char>(in), {});
}
static std::string digest(const std::string &s) {
  auto p = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
                                       (const guchar *)s.data(), s.size());
  std::string out = p;
  g_free(p);
  return out;
}
static std::string convert(const std::string &s, const char *to,
                           const char *from) {
  GError *error = nullptr;
  gsize n = 0;
  auto p = g_convert(s.data(), s.size(), to, from, nullptr, &n, &error);
  if (!p) {
    std::string message = error ? error->message : "Encoding conversion failed";
    if (error)
      g_error_free(error);
    throw std::runtime_error(message);
  }
  std::string out(p, n);
  g_free(p);
  return out;
}
std::string utf8Decode(const std::string &bytes, std::string &encoding,
                       bool &bom) {
  bom = false;
  size_t skip = 0;
  if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0) {
    encoding = "UTF-8";
    bom = true;
    skip = 3;
  } else if (bytes.compare(0, 2, "\xff\xfe") == 0) {
    encoding = "UTF-16LE";
    bom = true;
    skip = 2;
  } else if (bytes.compare(0, 2, "\xfe\xff") == 0) {
    encoding = "UTF-16BE";
    bom = true;
    skip = 2;
  }
  auto data = bytes.substr(skip);
  if (encoding == "UTF-8" &&
      !g_utf8_validate(data.data(), data.size(), nullptr))
    throw std::runtime_error("File is not valid UTF-8; use Open with Encoding "
                             "to select its character set");
  auto text =
      encoding == "UTF-8" ? data : convert(data, "UTF-8", encoding.c_str());
  if (text.find('\0') != std::string::npos)
    throw std::runtime_error(
        "Binary/NUL-containing files cannot be edited safely");
  return text;
}
std::string encodeFile(const std::string &text, const std::string &encoding,
                       bool bom, const std::string &eol) {
  std::string normalized;
  for (char c : text) {
    if (c == '\n' && eol == "CRLF")
      normalized += '\r';
    if (c == '\n' && eol == "CR")
      normalized += '\r';
    else
      normalized += c;
  }
  auto out = encoding == "UTF-8"
                 ? normalized
                 : convert(normalized, encoding.c_str(), "UTF-8");
  if (bom) {
    if (encoding == "UTF-8")
      out = "\xef\xbb\xbf" + out;
    else if (encoding == "UTF-16LE")
      out = "\xff\xfe" + out;
    else if (encoding == "UTF-16BE")
      out = "\xfe\xff" + out;
  }
  return out;
}
void atomicWrite(const fs::path &requested, const std::string &bytes,
                 bool privateFile) {
  fs::path path = fs::is_symlink(requested) ? fs::canonical(requested)
                                            : fs::absolute(requested);
  if (!fs::exists(path.parent_path()))
    throw std::runtime_error("Destination directory does not exist");
  struct stat old{};
  bool existed = !stat(path.c_str(), &old);
  std::string pattern =
      (path.parent_path() / ("." + path.filename().string() + ".xnedit-XXXXXX"))
          .string();
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back(0);
  int fd = mkstemp(name.data());
  if (fd < 0)
    throw std::runtime_error(std::strerror(errno));
  try {
    mode_t mask = umask(0);
    umask(mask);
    mode_t mode = privateFile ? 0600
                  : existed   ? (old.st_mode & 0777)
                              : (0666 & ~mask);
    if (fchmod(fd, mode))
      throw std::runtime_error(std::strerror(errno));
    size_t done = 0;
    while (done < bytes.size()) {
      ssize_t n = write(fd, bytes.data() + done, bytes.size() - done);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error(std::strerror(errno));
      done += n;
    }
    if (fsync(fd))
      throw std::runtime_error(std::strerror(errno));
    close(fd);
    fd = -1;
    if (rename(name.data(), path.c_str()))
      throw std::runtime_error(std::strerror(errno));
  } catch (...) {
    if (fd >= 0)
      close(fd);
    unlink(name.data());
    throw;
  }
}
std::string Document::text() const {
  char *s = BufGetAll(model);
  std::string out(s, model->length);
  NEditFree(s);
  return out;
}
void Document::assign(const std::string &s, bool clean) {
  if (s.find('\0') != std::string::npos ||
      !g_utf8_validate(s.data(), s.size(), nullptr))
    throw std::runtime_error("Invalid UTF-8 text");
  BufSetAll(model, s.c_str());
  past.clear();
  future.clear();
  historyBytes = 0;
  revision++;
  if (clean) {
    saved = s;
    savedEncoding = encoding;
    savedEol = eol;
    savedBom = bom;
  }
}
void Document::editorConfig() {
  if (path.empty())
    return;
  auto dir = path.parent_path().string(), name = path.filename().string();
  auto ec = EditorConfigGet(dir.c_str(), name.c_str());
  if (ec.tab_width > 0)
    tabWidth = std::min(ec.tab_width, 32);
  if (ec.indent_size > 0)
    indentWidth = std::min(ec.indent_size, 32);
  if (ec.indent_style != EC_INDENT_STYLE_UNSET)
    spaces = ec.indent_style == EC_SPACE;
  if (ec.end_of_line != EC_EOL_UNSET)
    eol = ec.end_of_line == EC_CRLF ? "CRLF"
          : ec.end_of_line == EC_CR ? "CR"
                                    : "LF";
  if (ec.charset) {
    encoding = ec.charset;
    std::transform(encoding.begin(), encoding.end(), encoding.begin(),
                   [](unsigned char c) { return g_ascii_toupper(c); });
    NEditFree(ec.charset);
  }
  if (ec.bom == EC_BOM)
    bom = true;
}
void Document::load(const fs::path &file) {
  auto full = fs::canonical(file);
  path = full;
  editorConfig();
  auto bytes = read(full);
  loadContents(bytes);
  path = full;
  diskDigest = digest(bytes);
  observedSignature.clear();
  editorConfig();
}
void Document::loadContents(const std::string &bytes) {
  if (bytes.size() > 64 * 1024 * 1024) throw std::runtime_error("Document exceeds 64 MiB");
  auto s = utf8Decode(bytes, encoding, bom);
  eol = s.find("\r\n") != std::string::npos ? "CRLF"
        : s.find('\r') != std::string::npos ? "CR"
                                            : "LF";
  std::string normalized;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r') {
      normalized += '\n';
      if (i + 1 < s.size() && s[i + 1] == '\n')
        i++;
    } else
      normalized += s[i];
  }
  assign(normalized, true);
}
void Document::markSaved(const std::string &snapshot, const std::string &enc,
                         const std::string &ending, bool withBom) {
  saved = snapshot;
  savedEncoding = enc;
  savedEol = ending;
  savedBom = withBom;
  checkedRevision = ~uint64_t(0);
}
void Document::replace(int start, int end, const std::string &value,
                       bool record) {
  if (start < 0 || end < start || end > length())
    throw std::runtime_error("Invalid edit range");
  if (value.find('\0') != std::string::npos ||
      !g_utf8_validate(value.data(), value.size(), nullptr))
    throw std::runtime_error("Invalid UTF-8 edit");
  if (size_t(length() - (end - start)) + value.size() > 64 * 1024 * 1024)
    throw std::runtime_error("Edit exceeds the 64 MiB document limit");
  if ((start < length() &&
       (static_cast<unsigned char>(BufGetCharacter(model, start)) & 0xc0) ==
           0x80) ||
      (end < length() &&
       (static_cast<unsigned char>(BufGetCharacter(model, end)) & 0xc0) ==
           0x80))
    throw std::runtime_error("Edit splits a UTF-8 character");
  char *deleted = BufGetRange(model, start, end);
  std::string before(deleted, end - start);
  NEditFree(deleted);
  BufReplace(model, start, end, value.c_str());
  revision++;
  if (record) {
    future.clear();
    historyBytes += before.size() + value.size();
    if (historyBytes > 64 * 1024 * 1024 || past.size() > 10000) {
      past.clear();
      historyBytes = before.size() + value.size();
    }
    past.push_back({start, before, value});
  }
}
std::optional<Edit> Document::undo() {
  if (past.empty())
    return {};
  auto edit = past.back();
  past.pop_back();
  replace(edit.pos, edit.pos + edit.after.size(), edit.before, false);
  future.push_back(edit);
  return edit;
}
std::optional<Edit> Document::redo() {
  if (future.empty())
    return {};
  auto edit = future.back();
  future.pop_back();
  replace(edit.pos, edit.pos + edit.before.size(), edit.after, false);
  past.push_back(edit);
  return edit;
}
bool Document::diskChanged(bool force) const {
  if (path.empty())
    return false;
  try {
    if (!fs::exists(path))
      return !diskDigest.empty();
    auto signature =
        std::to_string(fs::file_size(path)) + ":" +
        std::to_string(fs::last_write_time(path).time_since_epoch().count());
    if (!force && signature == observedSignature)
      return observedChanged;
    observedSignature = signature;
    observedChanged = digest(read(path)) != diskDigest;
    return observedChanged;
  } catch (...) {
    return true;
  }
}
void Document::save(const fs::path &target, bool overwrite) {
  auto full = fs::absolute(target).lexically_normal();
  bool same =
      !path.empty() &&
      (full == path || (fs::exists(full) && fs::equivalent(full, path)));
  if (same && !overwrite && diskChanged(true))
    throw std::runtime_error("File changed on disk");
  if (!same && fs::exists(full) && !overwrite)
    throw std::runtime_error("Destination already exists");
  auto bytes = encodeFile(text(), encoding, bom, eol);
  atomicWrite(full, bytes);
  path = fs::canonical(full);
  diskDigest = digest(bytes);
  observedSignature.clear();
  saved = text();
  savedEncoding = encoding;
  savedEol = eol;
  savedBom = bom;
  checkedRevision = ~uint64_t(0);
}
std::optional<Match> Document::find(const std::string &pattern, int start,
                                    bool regex, bool sensitive, bool reverse,
                                    const std::string &replacement) const {
  if (pattern.empty())
    return {};
  auto s = text();
  start = std::clamp(start, 0, int(s.size()) + (reverse ? 1 : 0));
  if (reverse && start == 0) return {};
  if (regex) {
    char *error = nullptr;
    auto re = CompileRE(pattern.c_str(), &error,
                        sensitive ? 0 : REDFLT_CASE_INSENSITIVE);
    if (!re)
      throw std::runtime_error(error ? error : "Invalid regular expression");
    const char *begin = s.c_str(), *end = begin + s.size();
    // A null forward candidate boundary allows zero-width matches at EOF.
    // Backward searches exclude the current insertion point, just as literal
    // searches do, while retaining the real beginning-of-line context.
    int ok = ExecRE(re, reverse ? begin : begin + start,
                    reverse ? begin + start - 1 : nullptr, reverse,
                    !reverse && start ? s[start - 1] : '\n', '\n',
                    ".,/\\`'!|@#%^&*()-=+{}[]\":;<>?", begin, end);
    std::optional<Match> match;
    if (ok) {
      std::string expanded;
      if (!replacement.empty()) {
        std::vector<char> out(1024 * 1024);
        if (!SubstituteRE(re, replacement.c_str(), out.data(), out.size())) {
          NEditFree(re);
          throw std::runtime_error("Replacement exceeds the supported size");
        }
        expanded = out.data();
      }
      match = Match{int(re->startp[0] - begin), int(re->endp[0] - begin),
                    std::move(expanded)};
    }
    NEditFree(re);
    return match;
  }
  if (!sensitive) {
    auto escaped = g_regex_escape_string(pattern.c_str(), pattern.size());
    GError *error = nullptr;
    auto re =
        g_regex_new(escaped, G_REGEX_CASELESS, G_REGEX_MATCH_NOTEMPTY, &error);
    g_free(escaped);
    if (!re) {
      std::string message = error ? error->message : "Invalid search";
      if (error)
        g_error_free(error);
      throw std::runtime_error(message);
    }
    GMatchInfo *info = nullptr;
    std::optional<Match> match;
    g_regex_match_full(re, s.data(), s.size(), reverse ? 0 : start,
                       G_REGEX_MATCH_DEFAULT, &info, nullptr);
    while (g_match_info_matches(info)) {
      int a, b;
      g_match_info_fetch_pos(info, 0, &a, &b);
      if (reverse && a >= start)
        break;
      match = Match{a, b, replacement};
      if (!reverse)
        break;
      if (!g_match_info_next(info, nullptr))
        break;
    }
    g_match_info_free(info);
    g_regex_unref(re);
    return match;
  }
  auto at = reverse ? s.rfind(pattern, start ? start - 1 : 0)
                    : s.find(pattern, start);
  if (at == std::string::npos)
    return {};
  return Match{int(at), int(at + pattern.size()), replacement};
}
int Document::replaceAll(const std::string &pattern,
                         const std::string &replacement, bool regex,
                         bool sensitive) {
  auto original = text();
  int pos = 0, count = 0;
  try {
    while (pos <= length()) {
      auto match = find(pattern, pos, regex, sensitive, false, replacement);
      if (!match)
        break;
      auto m = *match;
      replace(m.start, m.end, m.replacement, false);
      pos = m.start + m.replacement.size();
      if (++count > 1000000)
        throw std::runtime_error("Too many replacements");
      if (m.start == m.end) {
        auto now = text();
        if (pos >= int(now.size()))
          break;
        pos += Utf8CharLen((const unsigned char *)now.data() + pos);
      }
    }
    if (count) {
      auto after = text();
      BufSetAll(model, original.c_str());
      replace(0, length(), after);
    }
  } catch (...) {
    BufSetAll(model, original.c_str());
    revision++;
    throw;
  }
  return count;
}
void Document::indent(int start, int end, bool unindent) {
  auto s = text();
  start = start <= 0 ? 0 : int(s.rfind('\n', start - 1) + 1);
  if (end < start)
    end = start;
  int stop = end;
  if (stop > start && stop <= int(s.size()) && s[stop - 1] == '\n')
    stop--;
  std::string out;
  int pos = start;
  while (pos <= stop && pos < int(s.size())) {
    auto next = s.find('\n', pos);
    if (next == std::string::npos)
      next = s.size();
    auto line = s.substr(pos, next - pos);
    if (unindent) {
      int n = 0;
      if (!line.empty() && line[0] == '\t')
        n = 1;
      else
        while (n < int(line.size()) && n < indentWidth && line[n] == ' ')
          n++;
      line.erase(0, n);
    } else
      line = (spaces ? std::string(indentWidth, ' ') : "\t") + line;
    out += line;
    if (next < s.size())
      out += '\n';
    pos = next + 1;
    if (int(next) >= stop)
      break;
  }
  replace(start, std::min(pos, int(s.size())), out);
}
