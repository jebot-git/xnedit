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
  for(auto mode:{L::Cpp,L::Toml,L::Ini,L::Markdown,L::JavaScript}) {
    std::string incomplete;
    for(int i=0;i<15000;++i)incomplete += mode==L::Cpp?"#include <unfinished\n":mode==L::Markdown?"[unfinished `code\n":mode==L::JavaScript?"x = /[[unclosed /[ /[\n":"[section\n";
    assert(colorize(incomplete,mode).size()==incomplete.size());
  }
  auto plain=colorize(std::string(limit+1,'x'),L::Cpp);
  assert(plain.find_first_not_of(Normal)==plain.npos);
  std::cout<<"PASS "<<cases<<" syntax cases, language detection, incomplete tokens, byte fuzz and size limit\n";
}
