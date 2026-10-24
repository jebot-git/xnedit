// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
extern "C" {
#include "editorconfig.h"
#include "nedit_malloc.h"
#include "regularExp.h"
#include "textBuf.h"
}
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace fs = std::filesystem;
struct Edit {
  int pos;
  std::string before, after;
};
struct Match {
  int start, end;
  std::string replacement;
};
class Document {
  textBuffer *model = BufCreate();
  std::string saved, diskDigest;
  std::string savedEncoding = "UTF-8", savedEol = "LF";
  bool savedBom = false;
  mutable std::string observedSignature;
  mutable bool observedChanged = false;
  uint64_t revision = 0;
  mutable uint64_t checkedRevision = ~uint64_t(0);
  mutable bool textDirty = false;
  std::vector<Edit> past, future;
  size_t historyBytes = 0;

public:
  fs::path path;
  std::string encoding = "UTF-8", eol = "LF";
  bool bom = false, readOnly = false;
  int tabWidth = 4, indentWidth = 4;
  bool spaces = true;
  Document() = default;
  ~Document() { BufFree(model); }
  Document(const Document &) = delete;
  std::string text() const;
  int length() const { return model->length; }
  uint64_t generation() const { return revision; }
  bool modified() const {
    if (checkedRevision != revision) {
      textDirty = length() != int(saved.size()) || text() != saved;
      checkedRevision = revision;
    }
    return textDirty || encoding != savedEncoding || eol != savedEol ||
           bom != savedBom;
  }
  void load(const fs::path &);
  void loadContents(const std::string &bytes);
  void markSaved(const std::string &snapshot, const std::string &savedEncoding,
                 const std::string &savedEol, bool savedBom);
  void assign(const std::string &, bool clean = false);
  void replace(int, int, const std::string &, bool record = true);
  bool canUndo() const { return !past.empty(); }
  bool canRedo() const { return !future.empty(); }
  std::optional<Edit> undo(), redo();
  bool diskChanged(bool force = false) const;
  const std::string &originalDigest() const { return diskDigest; }
  void restoreDigest(const std::string &value) {
    diskDigest = value;
    observedSignature.clear();
  }
  void save(const fs::path &, bool overwrite = false);
  std::optional<Match> find(const std::string &, int, bool regex,
                            bool sensitive, bool reverse = false,
                            const std::string &replacement = "") const;
  int replaceAll(const std::string &, const std::string &, bool regex,
                 bool sensitive);
  void indent(int start, int end, bool unindent);
  void editorConfig();
};
std::string utf8Decode(const std::string &, std::string &encoding, bool &bom);
std::string encodeFile(const std::string &, const std::string &encoding,
                       bool bom, const std::string &eol);
void atomicWrite(const fs::path &, const std::string &,
                 bool privateFile = false);
