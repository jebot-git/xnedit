// SPDX-License-Identifier: GPL-2.0-or-later
#include "highlight.h"
#include <cassert>
#include <iostream>
#include <random>
using namespace highlight;
static int cases=0;
static void token(Language language,const std::string &source,const std::string &text,Style expected,size_t from=0) {
  auto colors=colorize(source,language);auto i=source.find(text,from);
  assert(i!=source.npos && !text.empty() && colors.size()==source.size());
  for(size_t p=i;p<i+text.size();++p) if(colors[p]!=expected) {
    std::cerr<<name(language)<<": ["<<text<<"] at "<<p<<" expected "<<char(expected)<<" got "<<colors[p]<<"\n";std::abort();
  }
  ++cases;
}
int main() {
  using L=Language;
  assert(detect("foo.CPP","")==L::Cpp);
  assert(detect("Makefile.am","")==L::Make);
  assert(detect("CMakeLists.txt","")==L::CMake);
  assert(detect(".editorconfig","")==L::Ini);
  assert(detect("script","#!/usr/bin/env -S python3 -u\n")==L::Python);
  assert(detect(".bashrc","")==L::Shell);
  assert(detect("notes.txt","return if 42")==L::Plain);
  token(L::Cpp,"#include <vector>\nint main() { // if 42\nreturn 0x2a; }","#include",Directive);
  token(L::Cpp,"int main() { // if 42\nreturn 0x2a; }","int",Type);
  token(L::Cpp,"int main() { // if 42\nreturn 0x2a; }","main",Function);
  token(L::Cpp,"int main() { // if 42\nreturn 0x2a; }","// if 42",Comment);
  token(L::Cpp,"auto s = R\"tag(a \"quote\" // raw)tag\"; return 1;","a \"quote\" // raw",String);
  token(L::Cpp,"\"unfinished\nreturn 1;","return",Keyword);
  token(L::Cpp,"#include <vector>","<vector>",String);
  token(L::Rust,"#[derive(Clone)]\nfn f() {}","#[derive(Clone)]",Directive);
  token(L::Rust,"println!(\"hello\");","println",Function);
  token(L::Css,"color: #aabbcc;","#aabbcc",Number);
  token(L::Rust,"fn f<'a>(x: &'a str) { /* outer /* nested */ still */ let n = r##\"hi \"# //\"##; }","'a",Variable);
  token(L::Rust,"/* outer /* nested */ still */ fn f() {}","still",Comment);
  token(L::Rust,"let x = r##\"hi \"# //\"##;","r##\"hi \"# //\"##",String);
  token(L::Rust,"let c = '日'; let n = 1..2;","'日'",String);
  token(L::Rust,"let c = '日'; let n = 1..2;","..",Operator);
  token(L::Python,"text = 42", "text",Normal);
  token(L::Sql,"CREATE TABLE x (name VARCHAR)","VARCHAR",Type);
  token(L::Yaml,"'title': text","'title'",Key);
  token(L::Python,"async def f():\n    s = f\"\"\"# text\nstill\"\"\"\n    return None","# text\nstill",String);
  token(L::Python,"@decorator\ndef f(): return 1e-3 # x","@decorator",Directive);
  token(L::Python,"return 1e-3 # x","1e-3",Number);
  token(L::JavaScript,"const r = /[a/b]+/gi; const n = total / count;","/[a/b]+/gi",String);
  token(L::JavaScript,"const n = total / count;","/",Operator);
  token(L::JavaScript,"interface X { id: number; }","interface",Keyword);
  token(L::Go,"package main\nvar x = `hi\n// raw`","// raw",String);
  token(L::Java,"public class Test { String s; }","class",Keyword);
  token(L::CSharp,"var s = @\"one\"\"two\n//text\";","@\"one\"\"two\n//text\"",String);
  token(L::Shell,"echo \"hello ${USER}\" # comment","${USER}",Variable);
  token(L::Shell,"echo ${#name} tail#word # comment","tail",Normal);
  token(L::Shell,"cat <<'EOF'\n# not a comment\nEOF\necho ok","# not a comment",String);
  token(L::Lua,"--[=[ outer\ncomment ]=]\nlocal x=[==[raw ' text]==]","outer\ncomment",Comment);
  token(L::Lua,"local x=[==[raw ' text]==]","raw ' text",String);
  token(L::Sql,"SELECT 'it''s -- a string' FROM users -- comment","SELECT",Keyword);
  token(L::Sql,"SELECT 'it''s -- a string' FROM users -- comment","'it''s -- a string'",String);
  token(L::Json,"{\"return\": true, \"url\": \"https://x/#hi\", \"n\": -1.5e+2}","\"return\"",Key);
  token(L::Json,"{\"return\": true}","true",Keyword);
  token(L::Yaml,"title: value\ntext: |\n  # literal\n  hello\nnext: true","# literal",String);
  token(L::Yaml,"title: value\n# comment","title",Key);
  token(L::Toml,"[package]\nname = '''raw\n# text'''","[package]",Heading);
  token(L::Toml,"name = '''raw\n# text'''","# text",String);
  token(L::Ini,"[Desktop Entry]\nExec=program\n; comment","Exec",Key);
  token(L::Html,"<!-- comment --><a href=\"/x\">hi &amp;</a>","href",Type);
  token(L::Html,"<a href=\"/x\">hi &amp;</a>","&amp;",Variable);
  token(L::Html,"<script>const x = 42;</script><style>a { color: red; }</style>","const",Keyword);
  token(L::Html,"<script>const x = 42;</script><style>a { color: red; }</style>","color",Key);
  token(L::Xml,"<![CDATA[<not-a-tag>]]><tag/>","<not-a-tag>",String);
  token(L::Css,"/* comment */ @media screen { a { color: red; } }","@media",Directive);
  token(L::Markdown,"# Title\n```python\nreturn 42 # comment\n```\n[link](url)","Title",Heading);
  token(L::Markdown,"# Title\n```python\nreturn 42 # comment\n```\n[link](url)","return",Keyword);
  token(L::Markdown,"# Title\n```python\nreturn 42 # comment\n```\n[link](url)","# comment",Comment);
  token(L::Make,"all: main.o\n\t$(CC) $@ # comment","all",Key);
  token(L::Make,"all: main.o\n\t$(CC) $@ # comment","$(CC)",Variable);
  token(L::CMake,"#[=[comment\nblock]=]\nset(VAR ${OTHER})","comment\nblock",Comment);
  token(L::CMake,"set(VAR ${OTHER})","${OTHER}",Variable);
  token(L::Diff,"--- a/file\n+++ b/file\n@@ -1 +1 @@\n-old\n+new\n","-old",Deletion);
  token(L::Diff,"--- a/file\n+++ b/file\n@@ -1 +1 @@\n-old\n+new\n","+new",Addition);
  assert(detect("INDEX.GMI", "# Title")==L::Gemini);
  assert(detect("page.gemtext", "")==L::Gemini);
  assert(detect("page.gmi", "```\n=: /not-a-prompt\n```\n")==L::Gemini);
  assert(detect("page.gmi", "=: /submit Sign the guestbook")==L::Spartan);
  assert(detect("page.spartan", "")==L::Spartan);
  assert(detect("gophermap", "Welcome\n1Menu\t/menu\n")==L::Gopher);
  assert(detect("include.GOPHERMAP", "")==L::Gopher);
  assert(detect("gophermap", "+INFO: 1Menu\t/menu\thost\t70\t+\n")==L::GopherPlus);
  assert(detect("page.gopher+", "")==L::GopherPlus);
  assert(detect("page.gopherplus", "")==L::GopherPlus);
  assert(detect("index", "Hello\n=> ../about.txt About\n")==L::Nex);
  assert(detect("index", "plain text")==L::Plain);
  assert(detect("index.txt", "=> nex://host/\n")==L::Plain);
  assert(detect("index.nex", "")==L::Nex);
  assert(detect("page.SCROLL", "")==L::Scroll);
  token(L::Gemini,"#日本語\n", "#日本語",Heading);
  token(L::Gemini,"=>gemini://example/ héllo 世界", "gemini://example/",String);
  token(L::Gemini,"=> /about Label", "=>",Directive);
  token(L::Gemini,"=> /about Label", "Label",Key);
  token(L::Gemini,"* item\n> quoted", "*",Operator);
  token(L::Gemini,"* item\n> quoted", "> quoted",Comment);
  token(L::Gemini,"*strong* _emphasis_ `code` [link](url)","*strong* _emphasis_ `code` [link](url)",Normal);
  token(L::Gemini,"  # not a heading", "# not a heading",Normal);
  token(L::Gemini,"``` alt text\r\n=> /literal #plain\r\n``` end\r\n=> /real Link", "=> /literal #plain",String);
  token(L::Gemini,"``` alt text\n# unfinished", "# unfinished",String);
  token(L::Gemini,"=: /submit Prompt", "=: /submit Prompt",Normal);
  token(L::Spartan,"=: /submit Prompt", "=:",Directive);
  token(L::Spartan,"=:\t/submit\tPrompt", "/submit",String);
  token(L::Spartan,"=: /submit Prompt", "Prompt",Key);
  token(L::Nex,"# ASCII title\n* text\n```\n", "# ASCII title",Normal);
  token(L::Nex,"=> ../directory/ Optional text", "../directory/",String);
  token(L::Nex,"=> ../directory/ Optional text", "Optional text",Key);
  token(L::Nex,"=>/not-a-link\n=>\t/not-a-link", "=>/not-a-link",Normal);
  token(L::Gopher,"Informational text without tabs", "Informational text without tabs",Normal);
  token(L::Gopher,"1Menu\t/path\thost.example\t70\t+", "1",Type);
  token(L::Gopher,"1Menu\t/path\thost.example\t70\t+", "Menu",Key);
  token(L::Gopher,"1Menu\t/path\thost.example\t70\t+", "/path",String);
  token(L::Gopher,"1Menu\t/path\thost.example\t70\t+", "host.example",Variable);
  token(L::Gopher,"1Menu\t/path\thost.example\t70\t+", "70",Number);
  token(L::Gopher,"1Menu\t/path\thost.example\t70\t+", "+",Directive);
  token(L::Gopher,"iInfo 世界\tignored\thost\t0", "Info 世界",Comment);
  token(L::Gopher,"hWeb\tURL:https://example/\r\n", "URL:https://example/",String);
  token(L::Gopher,"!Title\n# Comment\n=include\n.", "!Title",Heading);
  token(L::Gopher,"# Comment", "# Comment",Comment);
  token(L::Gopher,"=include", "=include",Directive);
  token(L::GopherPlus,"+INFO: 1Menu\t/path\thost\t70\t+", "+INFO:",Directive);
  token(L::GopherPlus,"+INFO: 1Menu\t/path\thost\t70\t+", "/path",String);
  token(L::GopherPlus,"+ADMIN:\n Admin: Name <mail@example>\n", "Admin",Key);
  token(L::GopherPlus,"+ASK:\r\n AskP: Password\r\n", "AskP",Keyword);
  token(L::GopherPlus,"+VIEWS:\n text/plain en: <1k>\n", "text/plain en",Key);
  token(L::Scroll,"##### Minor heading", "Minor heading",Heading);
  token(L::Scroll,"---\ntext", "---",Directive);
  token(L::Scroll,"** １２. Nested item", "**",Operator);
  token(L::Scroll,"** １２. Nested item", "１２.",Number);
  token(L::Scroll,"* a. Alphabetical", "a.",Number);
  token(L::Scroll,"=> #3.2 Title [Citation]", "#3.2",String);
  token(L::Scroll,"=> #3.2 Title [Citation]", "[Citation]",Type);
  token(L::Scroll,"=: /search Search [+]", "=:",Directive);
  token(L::Scroll,"word *bold 世界* normal", "*bold 世界*",Heading);
  token(L::Scroll,"word _emphasis_ normal", "_emphasis_",Keyword);
  token(L::Scroll,"word `*literal* _also_` normal", "`*literal* _also_`",String);
  token(L::Scroll,"word *unclosed\nnormal next", "normal next",Normal);
  token(L::Scroll,"symbol ✦*✦ normal", "✦*✦",Normal);
  token(L::Scroll,"word *\u200b rest", "*",Normal);
  token(L::Scroll,"\\=> /literal *bold*", "=> /literal ",Normal);
  token(L::Scroll,"\\=> /literal *bold*", "*bold*",Heading);
  token(L::Scroll,"\\``` escaped\n# heading", "# heading",Heading);
  token(L::Scroll,"```plain\n*not markup*\n```\n# heading", "*not markup*",String);
  token(L::Scroll,"```plain\n*not markup*\n```\n# heading", "# heading",Heading);
  // Partial tokens during typing, arbitrary bytes and delimiter truncation must
  // always produce a style for each source byte without reading past the buffer.
  const std::string seed="é日 r###\" x /* nested */ ```python\n'#\\$<<[=[\n";
  for(auto &mode:modes()) for(size_t n=0;n<=seed.size();++n) {
    auto text=seed.substr(0,n),styles=colorize(text,mode.language);
    assert(styles.size()==text.size());
    for(auto c:styles)assert(c>=Normal && c<=Deletion);
  }
  std::mt19937 generator(7);
  for(auto &mode:modes()) for(int trial=0;trial<50;++trial) {
    std::string bytes;for(int i=0;i<256;++i)bytes+=char(generator()%256);
    assert(colorize(bytes,mode.language).size()==bytes.size());
  }
  for(auto mode:{L::Cpp,L::Toml,L::Ini,L::Markdown,L::JavaScript,L::Gopher,L::GopherPlus,L::Scroll}) {
    std::string incomplete;
    for(int i=0;i<15000;++i)incomplete += mode==L::Gopher?"Info text without tabs\n":mode==L::GopherPlus?"+UNFINISHED\n Ask: value\n":mode==L::Scroll?"=> /url Label ]\n":mode==L::Cpp?"#include <unfinished\n":mode==L::Markdown?"[unfinished `code\n":mode==L::JavaScript?"x = /[[unclosed /[ /[\n":"[section\n";
    assert(colorize(incomplete,mode).size()==incomplete.size());
  }
  auto plain=colorize(std::string(limit+1,'x'),L::Cpp);
  assert(plain.find_first_not_of(Normal)==plain.npos);
  std::cout<<"PASS "<<cases<<" syntax cases, language detection, incomplete tokens, byte fuzz and size limit\n";
}
