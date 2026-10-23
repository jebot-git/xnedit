// SPDX-License-Identifier: GPL-2.0-or-later
#include "search.h"
#include <algorithm>
std::optional<SearchResult> SearchCursor::next(const Document &doc,const std::string &pattern,
                                              int start,bool regex,bool sensitive,bool reverse) {
  start=std::clamp(start,0,doc.length());
  bool wrap=reverse && start==0;
  if(!reverse && previous && previous->start==previous->end && previous->start==start
      && previousPattern==pattern && previousGeneration==doc.generation()
      && previousRegex==regex && previousCase==sensitive && previousReverse==reverse) {
    auto text=doc.text();
    if(start==doc.length())wrap=true;
    else {
      ++start;
      while(start<doc.length() && (static_cast<unsigned char>(text[start])&0xc0)==0x80)++start;
    }
  }
  auto match=wrap?std::optional<Match>{}:doc.find(pattern,start,regex,sensitive,reverse);
  if(!match) {
    wrap=true;
    match=doc.find(pattern,reverse?doc.length()+1:0,regex,sensitive,reverse);
  }
  previous=match;previousPattern=pattern;previousGeneration=doc.generation();
  previousRegex=regex;previousCase=sensitive;previousReverse=reverse;
  if(!match)return {};
  return SearchResult{*match,wrap};
}
const std::vector<RegexExample> &regexExamples() {
  static const std::vector<RegexExample> examples={
    {"Digits",R"(\d+)","Order 42","[&]","\\d matches a digit; + repeats one or more times."},
    {"Word characters",R"(\w+)","hello_42","[&]","\\w matches letters, digits and underscore."},
    {"Whitespace",R"(\s+)","one   two"," ","\\s matches horizontal whitespace, not line breaks. Use \\n for a newline."},
    {"Line break",R"(\n)","one\ntwo"," ","\\n matches a line break; replacement text also accepts \\n and \\t."},
    {"Whole word","<cat>","cat scatter cat","dog","< and > anchor the beginning and end of a word in XNEdit expressions."},
    {"Capture groups",R"((\w+), (\w+))","Doe, Jane",R"(\2 \1)","Parentheses capture text. In replacements, \\1 through \\9 insert groups; & inserts the entire match."},
    {"Either pattern","cat|dog","a dog","pet","| matches either alternative. Use (?:...) to group without capturing."},
    {"Beginning of line","^","one\ntwo","> ","^ matches the beginning of a line, without consuming a character."},
    {"End of line","$","one\ntwo",";","$ matches the end of a line, including the end of the document."},
    {"Any character",".","abc","x",". matches one character except a newline. Escape it as \\. to match a literal dot."},
    {"Shortest match",R"(".*?")","\"one\" \"two\"","[&]","*? repeats as few times as possible; * alone is greedy."},
    {"Positive lookahead","foo(?=bar)","foobar","baz","Match foo only when bar follows; bar is not part of the match."},
    {"Negative lookahead","foo(?!bar)","foobaz","new","Match foo only when bar does not follow."},
    {"Lookbehind",R"((?<=id=)\d+)","id=42","7","Match digits preceded by id=. Lookbehind requires bounded length."},
    {"Character class","[A-F0-9]+","A0 FF","[&]","Brackets match a set of characters; [^...] negates that set."},
    {"Optional character","colou?r","color colour","shade","? repeats the preceding item zero or one times."},
    {"Bounded repetition",R"(\d{2,4})","Year 2026","[&]","{2,4} repeats two to four times; {3} requires exactly three."},
    {"Literal dot",R"(\.)","file.txt","_","Escape regex punctuation with a backslash to match it literally."},
    {"Uppercase replacement",R"((\w+))","hello",R"(\U\1)","Replacement \\U uppercases a capture; \\L lowercases it. & is the full match; $1 is literal text."}
  };
  return examples;
}
