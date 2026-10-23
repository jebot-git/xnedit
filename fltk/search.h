// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "document.h"
struct SearchResult { Match match; bool wrapped = false; };
// Per-document navigation state prevents repeated zero-length matches from
// trapping Find Next at the same position, including UTF-8 and EOF anchors.
class SearchCursor {
  std::string previousPattern;
  uint64_t previousGeneration = ~uint64_t(0);
  bool previousRegex = false, previousCase = false, previousReverse = false;
  std::optional<Match> previous;
public:
  bool current(const Document &doc,const std::string &pattern,int position,bool regex,bool sensitive) const {
    return previous && previous->start==position && previousGeneration==doc.generation()
      && previousPattern==pattern && previousRegex==regex && previousCase==sensitive;
  }
  std::optional<SearchResult> next(const Document &, const std::string &, int,
                                   bool regex, bool sensitive, bool reverse);
};
struct RegexExample {
  const char *label, *pattern, *sample, *replacement, *description;
};
const std::vector<RegexExample> &regexExamples();
