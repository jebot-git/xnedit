// SPDX-License-Identifier: GPL-2.0-or-later
#include "highlight.h"
#include <algorithm>
#include <cctype>
#include <glib.h>
#include <unordered_map>
#include <unordered_set>
namespace highlight {
using L = Language;
const std::vector<Mode> &modes() {
  static const std::vector<Mode> values = {
    {L::Automatic,"Automatic"},{L::Plain,"Plain text"},{L::Cpp,"C and C++"},
    {L::Rust,"Rust"},{L::Python,"Python"},{L::JavaScript,"JavaScript and TypeScript"},
    {L::Go,"Go"},{L::Java,"Java"},{L::CSharp,"C#"},{L::Shell,"Shell"},{L::Lua,"Lua"},
    {L::Sql,"SQL"},{L::Json,"JSON"},{L::Yaml,"YAML"},{L::Toml,"TOML"},{L::Ini,"INI"},
    {L::Html,"HTML"},{L::Xml,"XML"},{L::Css,"CSS"},{L::Markdown,"Markdown"},
    {L::Make,"Makefile"},{L::CMake,"CMake"},{L::Diff,"Diff"},
    {L::Gemini,"Gemini (Gemtext)"},{L::Gopher,"Gopher (gophermap)"},
    {L::GopherPlus,"Gopher+"},{L::Nex,"Nex"},{L::Spartan,"Spartan"},
    {L::Scroll,"Scroll (Scrolltext)"}};
  return values;
}
const char *name(L language) {
  for (auto &mode : modes()) if (mode.language == language) return mode.name;
  return "Plain text";
}
static std::string lower(std::string_view s) {
  std::string result(s);
  for (char &c : result) c = char(std::tolower(static_cast<unsigned char>(c)));
  return result;
}
static L alias(std::string_view input) {
  static const std::unordered_map<std::string,L> values = {
    {"c",L::Cpp},{"h",L::Cpp},{"cc",L::Cpp},{"cpp",L::Cpp},{"c++",L::Cpp},{"cxx",L::Cpp},{"hpp",L::Cpp},{"hh",L::Cpp},{"hxx",L::Cpp},{"ino",L::Cpp},
    {"rs",L::Rust},{"rust",L::Rust},{"py",L::Python},{"pyw",L::Python},{"pyi",L::Python},{"python",L::Python},
    {"js",L::JavaScript},{"mjs",L::JavaScript},{"cjs",L::JavaScript},{"ts",L::JavaScript},{"tsx",L::JavaScript},{"jsx",L::JavaScript},{"javascript",L::JavaScript},{"typescript",L::JavaScript},
    {"go",L::Go},{"java",L::Java},{"cs",L::CSharp},{"csharp",L::CSharp},
    {"sh",L::Shell},{"bash",L::Shell},{"zsh",L::Shell},{"ksh",L::Shell},{"shell",L::Shell},
    {"lua",L::Lua},{"sql",L::Sql},{"json",L::Json},{"jsonl",L::Json},{"ipynb",L::Json},
    {"yaml",L::Yaml},{"yml",L::Yaml},{"toml",L::Toml},{"ini",L::Ini},{"cfg",L::Ini},{"conf",L::Ini},
    {"desktop",L::Ini},{"service",L::Ini},{"socket",L::Ini},{"target",L::Ini},{"timer",L::Ini},{"mount",L::Ini},{"path",L::Ini},
    {"html",L::Html},{"htm",L::Html},{"xml",L::Xml},{"svg",L::Xml},{"xhtml",L::Xml},{"xsl",L::Xml},{"ui",L::Xml},
    {"css",L::Css},{"md",L::Markdown},{"markdown",L::Markdown},{"mk",L::Make},{"make",L::Make},{"makefile",L::Make},
    {"cmake",L::CMake},{"diff",L::Diff},{"patch",L::Diff},
    {"gmi",L::Gemini},{"gemini",L::Gemini},{"gemtext",L::Gemini},
    {"gophermap",L::Gopher},{"gph",L::Gopher},{"gopher",L::Gopher},
    {"gopher+",L::GopherPlus},{"gopherplus",L::GopherPlus},{"gophermap+",L::GopherPlus},
    {"nex",L::Nex},{"spartan",L::Spartan},{"scroll",L::Scroll},{"scrolltext",L::Scroll}};
  auto found = values.find(lower(input));
  return found == values.end() ? L::Plain : found->second;
}
L detect(const std::filesystem::path &path, std::string_view text) {
  const auto file = lower(path.filename().string());
  if (file == "cmakelists.txt") return L::CMake;
  if (file == "makefile" || file == "gnumakefile" || file == "makefile.am" || file == "makefile.in") return L::Make;
  if (file == ".bashrc" || file == ".bash_profile" || file == ".profile" || file == ".zshrc") return L::Shell;
  if (file == ".editorconfig" || file == ".gitconfig") return L::Ini;
  auto extension = path.extension().string();
  L language = alias(extension.empty() ? "" : std::string_view(extension).substr(1));
  if (file == "gophermap") language = L::Gopher;
  if (file == "gophermap+" || file == "gopherplus") return L::GopherPlus;
  // Shared extensions need only a bounded look at structural line prefixes.
  // Do not guess a protocol from ordinary prose or take over .txt documents.
  if (language == L::Gemini || language == L::Gopher || file == "index") {
    auto probe = text.substr(0, 65536);
    bool pre = false;
    for (size_t i = 0; i < probe.size();) {
      auto end = probe.find('\n', i); if (end == probe.npos) end = probe.size();
      auto line = probe.substr(i, end-i);
      if (language == L::Gemini && line.substr(0,3) == "```") pre = !pre;
      else if (language == L::Gemini && !pre && line.substr(0,2) == "=:") return L::Spartan;
      if (language == L::Gopher && line.substr(0,6) == "+INFO:") return L::GopherPlus;
      if (file == "index" && line.substr(0,3) == "=> ") return L::Nex;
      i = end < probe.size() ? end+1 : end;
    }
  }
  if (language != L::Plain) return language;
  if (text.substr(0,2) == "#!") {
    auto line = text.substr(2, text.find('\n') == text.npos ? text.size()-2 : text.find('\n')-2);
    // Parse interpreter tokens, including /usr/bin/env -S python3 -u.
    for (size_t i=0; i<line.size();) {
      while (i<line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
      size_t end=i; while (end<line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) ++end;
      auto token = line.substr(i,end-i); i=end;
      auto slash = token.rfind('/'); if (slash != token.npos) token.remove_prefix(slash+1);
      if (token == "env" || token == "-S" || token.find('=') != token.npos) continue;
      if (token.substr(0,6) == "python") return L::Python;
      if (token == "node" || token == "nodejs" || token == "deno" || token == "bun") return L::JavaScript;
      if (token.substr(0,3) == "lua") return L::Lua;
      if (token == "sh" || token == "bash" || token == "dash" || token == "zsh" || token == "ksh") return L::Shell;
      if (!token.empty() && token[0] != '-') break;
    }
  }
  return L::Plain;
}
using Words = std::unordered_set<std::string>;
static Words words(const char *s) {
  Words result; std::string_view text(s);
  for (size_t i=0; i<text.size();) {
    auto end=text.find(' ',i); if (end==text.npos) end=text.size();
    if (end>i) result.emplace(text.substr(i,end-i)); i=end+1;
  }
  return result;
}
static const Words &keywords(L language) {
  static const std::unordered_map<L,Words> sets = {
    {L::Cpp,words("alignas alignof asm auto break case catch class concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern final for friend goto if inline mutable namespace new noexcept nullptr operator override private protected public register reinterpret_cast requires return sizeof static static_assert static_cast struct switch template this thread_local throw true false try typedef typeid typename union using virtual volatile while _Atomic _Generic _Noreturn restrict")},
    {L::Rust,words("as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while")},
    {L::Python,words("and as assert async await break case class continue def del elif else except False finally for from global if import in is lambda match None nonlocal not or pass raise return True try type while with yield")},
    {L::JavaScript,words("abstract as async await break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null of override private protected public readonly return satisfies set static super switch this throw true try type typeof undefined unique unknown var void while with yield")},
    {L::Go,words("break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil")},
    {L::Java,words("abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for if implements import instanceof int interface long native new null package private protected public record return sealed short static strictfp super switch synchronized this throw throws transient true false try var void volatile while yield permits non-sealed")},
    {L::CSharp,words("abstract as async await base break case catch checked class const continue default delegate do else enum event explicit extern false finally fixed for foreach goto if implicit in interface internal is lock namespace new null operator out override params partial private protected public readonly record ref required return sealed sizeof stackalloc static struct switch this throw true try typeof unchecked unsafe using var virtual volatile when where while with yield")},
    {L::Shell,words("if then else elif fi for while until do done case esac in function select time coproc local export readonly declare typeset unset return break continue source eval exec trap shift set")},
    {L::Lua,words("and break do else elseif end false for function goto if in local nil not or repeat return then true until while")},
    {L::Sql,words("select from where join inner outer left right full cross on as and or not null true false insert into values update set delete create alter drop table index view trigger database schema primary key foreign references unique constraint check default distinct union all group by having order asc desc limit offset with recursive case when then else end exists in like between is begin commit rollback transaction grant revoke returning explain analyze over partition window replace conflict cascade")},
    {L::Json,words("true false null")},{L::Yaml,words("true false null True False Null TRUE FALSE NULL")},
    {L::Toml,words("true false inf nan")},{L::CMake,words("if elseif else endif foreach endforeach while endwhile function endfunction macro endmacro return break continue block endblock")}
  };
  static const Words empty;
  auto found=sets.find(language); return found==sets.end()?empty:found->second;
}
static const Words &types(L language) {
  static const std::unordered_map<L,Words> sets = {
    {L::Cpp,words("bool char char8_t char16_t char32_t wchar_t double float int long short signed unsigned void size_t ssize_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t string")},
    {L::Rust,words("bool char str String Vec Option Result Box Arc Rc usize isize u8 u16 u32 u64 u128 i8 i16 i32 i64 i128 f32 f64")},
    {L::Python,words("bool bytes bytearray complex dict float frozenset int list memoryview object range set slice str tuple")},
    {L::JavaScript,words("number string boolean bigint symbol object any Array Promise Map Set Uint8Array")},
    {L::Go,words("bool byte rune string error any int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 uintptr float32 float64 complex64 complex128")},
    {L::Java,words("String Integer Long Double Boolean Object List Map Set Optional")},
    {L::CSharp,words("bool byte char decimal double dynamic float int long object sbyte short string uint ulong ushort void nint nuint")},
    {L::Sql,words("bool boolean char varchar text int integer bigint numeric decimal float double timestamp date uuid json jsonb real")}
  };
  static const Words empty;
  auto found=sets.find(language);return found==sets.end()?empty:found->second;
}
static bool idStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c=='_' || static_cast<unsigned char>(c)>=128; }
static bool idPart(char c) { return idStart(c) || std::isdigit(static_cast<unsigned char>(c)); }
class Lexer {
  std::string_view s;
  L language;
  unsigned depth;
  std::string colors;
  bool at(size_t i, std::string_view token) const { return s.substr(i,token.size())==token; }
  void paint(size_t a,size_t b,Style style) { std::fill(colors.begin()+a,colors.begin()+std::min(b,s.size()),char(style)); }
  size_t eol(size_t i) const { auto end=s.find('\n',i); return end==s.npos?s.size():end; }
  size_t lineStart(size_t i) const { auto p=i?s.rfind('\n',i-1):s.npos; return p==s.npos?0:p+1; }
  bool linePrefix(size_t i) const {
    for (size_t p=lineStart(i);p<i;++p) if (s[p]!=' ' && s[p]!='\t') return false;
    return true;
  }
  size_t next(size_t i) const { while(i<s.size() && (s[i]==' ' || s[i]=='\t')) ++i; return i; }
  size_t closed(size_t start,std::string_view end) const { auto p=s.find(end,start); return p==s.npos?s.size():p+end.size(); }
  void embedded(size_t a,size_t b,L mode) {
    if (depth>=4 || mode==L::Plain) { paint(a,b,String); return; }
    auto result=Lexer(s.substr(a,b-a),mode,depth+1).run();
    colors.replace(a,b-a,result);
  }
  // Quote runs are byte-based; UTF-8 bytes stay together under one style.
  size_t quote(size_t i,char q,bool multiline,bool doubled=false,bool escapes=true) const {
    for (size_t j=i+1;j<s.size();++j) {
      if (!multiline && s[j]=='\n') return j;
      if (escapes && s[j]=='\\' && j+1<s.size()) { ++j; continue; }
      if (s[j]==q) {
        if (doubled && j+1<s.size() && s[j+1]==q) { ++j; continue; }
        return j+1;
      }
    }
    return s.size();
  }
  size_t bracket(size_t i) const {
    if (i>=s.size() || s[i]!='[') return i;
    size_t p=i+1; while(p<s.size() && s[p]=='=') ++p;
    if (p>=s.size() || s[p]!='[') return i;
    return closed(p+1,"]"+std::string(p-i-1,'=')+"]");
  }
  size_t variable(size_t i) const {
    size_t j=i+1;
    if (j<s.size() && (s[j]=='{' || s[j]=='(')) {
      char open=s[j],close=open=='{'?'}':')'; int nesting=1; ++j;
      while(j<s.size() && nesting) { if(s[j]==open)++nesting; if(s[j]==close)--nesting; ++j; }
    } else if (j<s.size() && (idPart(s[j]) || std::string_view("?@*#$!-0123456789").find(s[j])!=s.npos)) {
      ++j; while(j<s.size() && idPart(s[j]))++j;
    }
    return j;
  }
  size_t number(size_t i) const {
    size_t j=i;
    if (at(j,"0x") || at(j,"0X") || at(j,"0b") || at(j,"0B") || at(j,"0o") || at(j,"0O")) {
      j+=2; while(j<s.size() && (std::isxdigit(static_cast<unsigned char>(s[j])) || s[j]=='_'))++j;
    } else {
      while(j<s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j]=='_'))++j;
      if(j<s.size() && s[j]=='.' && !at(j,"..")) {
        ++j; while(j<s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j]=='_'))++j;
      }
      if(j<s.size() && (s[j]=='e' || s[j]=='E') && j+1<s.size()) {
        size_t p=j+1; if(s[p]=='+' || s[p]=='-')++p;
        if(p<s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
          j=p+1; while(j<s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j]=='_'))++j;
        }
      }
    }
    if(language!=L::Json) while(j<s.size() && idPart(s[j]))++j;
    return std::max(i+1,j);
  }
  void markup() {
    auto lowered=lower(s);
    for(size_t i=0;i<s.size();) {
      if(at(i,"<!--")) { auto end=closed(i+4,"-->");paint(i,end,Comment);i=end; }
      else if(at(i,"<![CDATA[")) { auto end=closed(i+9,"]]>");paint(i,end,String);i=end; }
      else if(s[i]=='<' && i+1<s.size() && (idStart(s[i+1]) || s[i+1]=='/' || s[i+1]=='!' || s[i+1]=='?')) {
        size_t begin=i++; bool closing=s[i]=='/';
        if(s[i]=='/' || s[i]=='!' || s[i]=='?')++i;
        size_t tag=i; while(i<s.size() && (idPart(s[i]) || s[i]=='-' || s[i]==':'))++i;
        auto tagName=lower(s.substr(tag,i-tag));paint(begin,i,Key);
        bool finished=false;
        while(i<s.size()) {
          if(s[i]=='"' || s[i]=='\'') {auto end=quote(i,s[i],true,false,false);paint(i,end,String);i=end;}
          else if(s[i]=='>') {paint(i,i+1,Key);++i;finished=true;break;}
          else if(idStart(s[i])) {size_t start=i++;while(i<s.size() && (idPart(s[i]) || s[i]=='-' || s[i]==':'))++i;paint(start,i,Type);}
          else {if(s[i]=='=' || s[i]=='/' || s[i]=='?')paint(i,i+1,Operator);++i;}
        }
        if(language==L::Html && finished && !closing && (tagName=="script" || tagName=="style")) {
          auto end=lowered.find("</"+tagName,i);if(end==s.npos)end=s.size();
          L mode=tagName=="script"?L::JavaScript:L::Css;
          auto opening=lowered.substr(begin,i-begin);
          if(opening.find("application/json")!=std::string::npos || opening.find("application/ld+json")!=std::string::npos) mode=L::Json;
          embedded(i,end,mode);i=end;
        }
      } else if(s[i]=='&') {
        size_t end=i+1;while(end<s.size() && end-i<32 && (idPart(s[end]) || s[end]=='#'))++end;
        if(end<s.size() && s[end]==';'){paint(i,end+1,Variable);i=end+1;}else ++i;
      } else ++i;
    }
  }
  void markdown() {
    for(size_t i=0;i<s.size();) {
      size_t end=eol(i),first=next(i);
      if(first-i<=3 && first<end && (s[first]=='`' || s[first]=='~')) {
        char c=s[first];size_t n=first;while(n<end && s[n]==c)++n;
        if(n-first>=3) {
          paint(first,end,Directive);auto info=next(n),infoEnd=info;
          while(infoEnd<end && idPart(s[infoEnd]))++infoEnd;
          L mode=alias(s.substr(info,infoEnd-info));size_t body=end<s.size()?end+1:end,finish=body,after=s.size();
          for(;finish<s.size();) {
            size_t p=next(finish),q=p;while(q<s.size() && s[q]==c)++q;
            if(p-finish<=3 && q-p>=n-first && next(q)>=eol(q)) {after=eol(q);break;}
            auto line=eol(finish);finish=line<s.size()?line+1:line;
          }
          embedded(body,finish,mode);paint(finish,after,Directive);i=after<s.size()?after+1:after;continue;
        }
      }
      if(first<end && (s[first]=='#' || s[first]=='>')) {paint(first,end,s[first]=='#'?Heading:Directive);i=end<s.size()?end+1:end;continue;}
      bool missingLink=false;
      for(size_t p=i;p<end;) {
        if(s[p]=='`') {
          size_t q=p;while(q<end && s[q]=='`')++q;auto found=s.substr(q,end-q).find(s.substr(p,q-p));
          auto close=found==s.npos?s.npos:q+found;
          if(close!=s.npos){paint(p,close+q-p,String);p=close+q-p;}else p=q;
        } else if(s[p]=='[' && !missingLink) {
          auto found=s.substr(p+1,end-p-1).find("](");
          auto q=found==s.npos?s.npos:p+1+found;
          auto tail=q==s.npos?s.npos:s.substr(q+2,end-q-2).find(')');
          auto close=tail==s.npos?s.npos:q+2+tail;
          if(close!=s.npos && close<end){paint(p,q+1,Key);paint(q+1,close+1,String);p=close+1;}else {missingLink=true;++p;}
        } else if(s[p]=='*' || s[p]=='_' && (p==i || !idPart(s[p-1]))) {
          auto q=s.find(s[p],p+1);
          if(q!=s.npos && q<end && q>p+1){paint(p,q+1,Heading);p=q+1;}else ++p;
        } else ++p;
      }
      i=end<s.size()?end+1:end;
    }
  }
  size_t lineFind(char c, size_t start, size_t end) const {
    auto offset=s.substr(start,end-start).find(c);
    return offset==s.npos?end:start+offset;
  }
  void link(size_t start, size_t end, bool relations = false) {
    paint(start, start+2, Directive);
    size_t url = start+2;
    while (url < end && (s[url]==' ' || s[url]=='\t')) ++url;
    size_t tail = url;
    while (tail < end && s[tail]!=' ' && s[tail]!='\t') ++tail;
    paint(url, tail, String);
    size_t label = tail;
    while (label < end && (s[label]==' ' || s[label]=='\t')) ++label;
    paint(label, end, Key);
    if (relations && end > label && s[end-1]==']') {
      auto open = s.substr(label,end-label).rfind('[');
      if (open != s.npos) paint(label+open,end,Type);
    }
  }
  // Scroll's inline toggles are line bounded and Unicode aware. In particular,
  // punctuation around an ASCII-art marker must not turn the rest into emphasis.
  void scrollInline(size_t start, size_t end) {
    auto point = [&](size_t p) -> gunichar {
      if (p >= end) return 0;
      auto c = g_utf8_get_char_validated(s.data()+p,end-p);
      return c == gunichar(-1) || c == gunichar(-2) ? 0xfffd : c;
    };
    auto space = [](gunichar c) { return !c || c==0x200b || g_unichar_isspace(c); };
    auto symbol = [](gunichar c) {
      auto type = g_unichar_type(c);
      return g_unichar_ispunct(c) || type==G_UNICODE_MATH_SYMBOL ||
          type==G_UNICODE_CURRENCY_SYMBOL || type==G_UNICODE_MODIFIER_SYMBOL ||
          type==G_UNICODE_OTHER_SYMBOL;
    };
    bool strong=false, emphasis=false, code=false;
    for (size_t p=start;p<end;++p) {
      char c=s[p];
      bool marker=c=='*' || c=='_' || c=='`';
      bool toggle=false;
      if (marker) {
        size_t prior=p;
        if (prior>start) { --prior; while(prior>start && (static_cast<unsigned char>(s[prior])&0xc0)==0x80) --prior; }
        auto left=p==start?0:point(prior), right=point(p+1);
        toggle=((!space(left) && left!=gunichar(c)) || (!space(right) && right!=gunichar(c))) &&
            !(symbol(left) && symbol(right));
      }
      Style before=code?String:strong?Heading:emphasis?Keyword:Normal;
      if (toggle) {
        if (c=='`') code=!code;
        else if (!code && c=='*') strong=!strong;
        else if (!code && c=='_') emphasis=!emphasis;
      }
      Style after=code?String:strong?Heading:emphasis?Keyword:Normal;
      colors[p]=before!=Normal?before:after;
    }
  }
  void smallText() {
    const bool scroll=language==L::Scroll, nex=language==L::Nex;
    bool pre=false;
    for (size_t i=0;i<s.size();) {
      auto finish=eol(i), end=finish;
      if (end>i && s[end-1]=='\r') --end;
      if (nex) {
        if (at(i,"=> ")) link(i,end);
      } else if (at(i,"```")) {
        paint(i,end,Directive);pre=!pre;
      } else if (pre) paint(i,end,String);
      else if (scroll && s[i]=='\\') {
        auto rest=s.substr(i+1,end-i-1);
        bool escaped=rest.substr(0,2)=="=>" || rest.substr(0,2)=="=:" ||
            rest.substr(0,3)=="```" || rest=="---" ||
            (!rest.empty() && (rest[0]=='#' || rest[0]=='>' || rest[0]=='*'));
        if (escaped) {
          paint(i,i+1,Operator);
          size_t prefix=i+2;
          if (rest.substr(0,2)=="=>" || rest.substr(0,2)=="=:") prefix=i+3;
          else if (rest.substr(0,3)=="```" || rest=="---") prefix=i+4;
          else while(prefix<end && s[prefix]==s[i+1]) ++prefix;
          scrollInline(prefix,end);
        }
        else scrollInline(i,end);
      } else if (at(i,"=>") || (language==L::Spartan || scroll) && at(i,"=:")) link(i,end,scroll);
      else if (i<end && s[i]=='#') paint(i,end,Heading);
      else if (scroll && s.substr(i,end-i)=="---") paint(i,end,Directive);
      else {
        size_t body=i;
        if (i<end && s[i]=='>') {
          while (body<end && s[body]=='>') ++body;
          if (!scroll) paint(i,end,Comment);
          else { paint(i,body,Directive);scrollInline(body,end); }
        } else if (i<end && s[i]=='*') {
          size_t bullet=i+1;
          if (scroll) while(bullet<end && s[bullet]=='*') ++bullet;
          bool list=bullet<end && (s[bullet]==' ' || scroll && s[bullet]=='\t') && (!scroll || bullet-i<=4);
          if (list) {
            paint(i,bullet,Operator);body=bullet+1;
            if (scroll) {
              while(body<end && (s[body]==' ' || s[body]=='\t')) ++body;
              size_t marker=body;
              while(marker<end) {
                auto c=g_utf8_get_char_validated(s.data()+marker,end-marker);
                if (c==gunichar(-1) || c==gunichar(-2) || !g_unichar_isdigit(c)) break;
                marker+=g_unichar_to_utf8(c,nullptr);
              }
              if (marker==body && marker<end && g_ascii_isalpha(s[marker])) ++marker;
              if (marker>body && marker<end && s[marker]=='.') {
                paint(body,marker+1,Number);body=marker+1;
              }
            }
          }
          if (scroll) scrollInline(body,end);
        } else if (scroll) scrollInline(i,end);
      }
      i=finish<s.size()?finish+1:finish;
    }
  }
  void gopherEntry(size_t start, size_t end) {
    if (start>=end) return;
    if (std::string_view("0123456789+TgIihds;cM").find(s[start])==s.npos) return;
    auto tab=lineFind('\t',start+1,end);
    if (tab>=end) return;
    paint(start,start+1,Type);
    auto fieldEnd=tab;
    paint(start+1,fieldEnd,s[start]=='i'?Comment:Key);
    size_t field=0;
    for (size_t p=fieldEnd;p<end;++field) {
      paint(p,p+1,Operator);
      auto next=lineFind('\t',p+1,end);
      paint(p+1,next,field==0?String:field==1?Variable:field==2?Number:Directive);
      p=next;
    }
  }
  void gopher() {
    bool attribute=false, ask=false;
    for (size_t i=0;i<s.size();) {
      auto finish=eol(i),end=finish;if(end>i && s[end-1]=='\r') --end;
      if (i<end) {
        if (language==L::GopherPlus && s[i]=='+') {
          auto colon=lineFind(':',i+1,end);
          if (colon<end) {
            attribute=true;ask=s.substr(i,colon-i)=="+ASK";
            paint(i,colon+1,Directive);
            size_t value=colon+1;while(value<end && s[value]==' ') ++value;
            if (s.substr(i,colon-i)=="+INFO") gopherEntry(value,end);
            else paint(value,end,Key);
          } else paint(i,end,Directive);
        } else if (attribute && (s[i]==' ' || s[i]=='\t')) {
          if (ask) {
            size_t word=i;while(word<end && (s[word]==' ' || s[word]=='\t')) ++word;
            size_t colon=lineFind(':',word,end);
            paint(word,colon,Keyword);
            if (colon<end) paint(colon,colon+1,Operator);
          } else {
            auto colon=lineFind(':',i,end);if(colon<end) paint(i+1,colon,Key);
          }
        } else {
          attribute=false;ask=false;
          if (s[i]=='#') paint(i,end,Comment);
          else if (s[i]=='!' && i==0) paint(i,end,Heading);
          else if (std::string_view("-:=").find(s[i])!=s.npos || end==i+1 && std::string_view("~%*.").find(s[i])!=s.npos) paint(i,end,Directive);
          else gopherEntry(i,end);
        }
      }
      i=finish<s.size()?finish+1:finish;
    }
  }
  void diff() {
    for(size_t i=0;i<s.size();) {
      auto end=eol(i);Style style=Normal;
      if(at(i,"diff ") || at(i,"index "))style=Directive;
      else if(at(i,"--- ") || at(i,"+++ "))style=Key;
      else if(at(i,"@@"))style=Heading;
      else if(s[i]=='+')style=Addition;
      else if(s[i]=='-')style=Deletion;
      paint(i,end,style);i=end<s.size()?end+1:end;
    }
  }
public:
  Lexer(std::string_view input,L mode,unsigned nesting=0):s(input),language(mode),depth(nesting),colors(s.size(),Normal){}
  std::string run() {
    if(language==L::Plain || language==L::Automatic)return colors;
    if(language==L::Html || language==L::Xml){markup();return colors;}
    if(language==L::Markdown){markdown();return colors;}
    if(language==L::Diff){diff();return colors;}
    if(language==L::Gemini || language==L::Spartan || language==L::Nex || language==L::Scroll){smallText();return colors;}
    if(language==L::Gopher || language==L::GopherPlus){gopher();return colors;}
    bool hash=language==L::Python || language==L::Shell || language==L::Yaml || language==L::Toml || language==L::Ini || language==L::Make || language==L::CMake;
    bool slash=language==L::Cpp || language==L::Rust || language==L::JavaScript || language==L::Go || language==L::Java || language==L::CSharp;
    bool block=slash || language==L::Css || language==L::Sql;
    bool regexAllowed=true;
    size_t retryRegexAt=0;
    std::vector<std::pair<std::string,bool>> heredocs;
    for(size_t i=0;i<s.size();) {
      size_t start=i;
      if(s[i]=='\n') {
        ++i;
        for(auto &[delimiter,tabs]:heredocs) {
          size_t body=i;
          while(i<s.size()) {
            size_t end=eol(i),p=i;if(tabs)while(p<end && s[p]=='\t')++p;
            if(s.substr(p,end-p)==delimiter) {paint(body,i,String);paint(i,end,Directive);i=end<s.size()?end+1:end;break;}
            i=end<s.size()?end+1:end;
          }
          if(i==s.size())paint(body,i,String);
        }
        heredocs.clear();continue;
      }
      if(hash && s[i]=='#' && (language!=L::Shell && language!=L::Yaml || linePrefix(i) || i>0 && std::isspace(static_cast<unsigned char>(s[i-1])))) {
        auto end=language==L::CMake?bracket(i+1):i+1;
        if(end==i+1)end=eol(i);
        paint(i,end,Comment);i=end;continue;
      }
      if(language==L::Ini && s[i]==';' && linePrefix(i)) {i=eol(i);paint(start,i,Comment);continue;}
      if(slash && at(i,"//") || (language==L::Sql || language==L::Lua) && at(i,"--")) {
        auto end=language==L::Lua?bracket(i+2):i+2;
        if(end==i+2)end=eol(i);
        paint(i,end,Comment);i=end;continue;
      }
      if(block && at(i,"/*")) {
        i+=2;unsigned nesting=1;
        while(i<s.size() && nesting) {
          if(language==L::Rust && at(i,"/*")){++nesting;i+=2;}
          else if(at(i,"*/")){--nesting;i+=2;}else ++i;
        }
        paint(start,i,Comment);continue;
      }
      if(language==L::Cpp && s[i]=='#' && linePrefix(i)) {
        ++i;i=next(i);size_t directive=i;while(i<s.size() && idPart(s[i]))++i;
        auto word=s.substr(directive,i-directive);paint(start,i,Directive);
        auto argument=next(i);
        if((word=="include" || word=="include_next") && argument<s.size() && s[argument]=='<') {
          auto lineEnd=eol(argument);
          auto close=s.substr(argument+1,lineEnd-argument-1).find('>');
          i=close==s.npos?lineEnd:argument+close+2;paint(argument,i,String);
        }
        continue;
      }
      if(language==L::Rust && at(i,"#[")) {
        i+=2;unsigned nesting=1;
        while(i<s.size() && nesting) {
          if(s[i]=='"')i=quote(i,'"',false);
          else {if(s[i]=='[')++nesting;if(s[i]==']')--nesting;++i;}
        }
        paint(start,i,Directive);continue;
      }
      if((language==L::Ini || language==L::Toml) && s[i]=='[' && linePrefix(i)) {
        const auto lineEnd=eol(i);
        auto end=s.substr(i+1,lineEnd-i-1).find(']');
        i=end==s.npos?lineEnd:i+end+2;
        if(i<s.size() && s[i]==']')++i;
        paint(start,i,Heading);continue;
      }
      if((language==L::Lua || language==L::CMake) && s[i]=='[') {
        auto end=bracket(i);if(end>i){paint(i,end,String);i=end;regexAllowed=false;continue;}
      }
      // C++ raw strings: an arbitrary delimiter up to 16 bytes, preserved verbatim.
      if(language==L::Cpp) {
        size_t p=i;
        for(auto prefix:{"u8R\"","uR\"","UR\"","LR\"","R\""})if(at(i,prefix)){p=i+std::string_view(prefix).size();break;}
        if(p>i) {
          auto relative=s.substr(p,17).find('(');
          auto open=relative==s.npos?s.npos:p+relative;
          if(open!=s.npos && open-p<=16 && s.substr(p,open-p).find_first_of(" \\)\t\r\n")==s.npos) {
            i=closed(open+1,")"+std::string(s.substr(p,open-p))+"\"");paint(start,i,String);regexAllowed=false;continue;
          }
        }
      }
      if(language==L::Rust && (s[i]=='r' || at(i,"br"))) {
        size_t p=i+(s[i]=='b'?2:1),hashes=p;while(p<s.size() && s[p]=='#')++p;
        if(p<s.size() && s[p]=='"') {i=closed(p+1,"\""+std::string(p-hashes,'#'));paint(start,i,String);regexAllowed=false;continue;}
      }
      if(language==L::Python && (s[i]=='r' || s[i]=='R' || s[i]=='b' || s[i]=='B' || s[i]=='u' || s[i]=='U' || s[i]=='f' || s[i]=='F')) {
        size_t p=i;while(p<s.size() && p-i<2 && std::string_view("rRbBuUfF").find(s[p])!=s.npos)++p;
        if(p<s.size() && (s[p]=='\'' || s[p]=='"')) {paint(i,p,String);i=p;}
      }
      if(language==L::CSharp && at(i,"@\"")) {
        i=quote(i+1,'"',true,true,false);paint(start,i,String);regexAllowed=false;continue;
      }
      if(s[i]=='\'' && language==L::Rust) {
        // Lifetimes/labels are not character literals. A char closes immediately
        // after one Unicode code point or one escaped character.
        size_t p=i+1;
        if(p<s.size() && s[p]=='\\') {
          ++p;
          if(p<s.size() && s[p]=='u' && p+1<s.size() && s[p+1]=='{'){auto end=s.find('}',p+2);p=end==s.npos?s.size():end+1;}
          else if(p<s.size() && s[p]=='x')p=std::min(s.size(),p+3);
          else if(p<s.size())++p;
        } else if(p<s.size()) {++p;while(p<s.size() && (static_cast<unsigned char>(s[p])&0xc0)==0x80)++p;}
        if(p>=s.size() || s[p]!='\'') {++i;while(i<s.size() && idPart(s[i]))++i;paint(start,i,Variable);continue;}
      }
      if(s[i]=='"' || s[i]=='\'' && language!=L::Json || s[i]=='`' && (language==L::JavaScript || language==L::Go || language==L::Shell || language==L::Sql)) {
        char q=s[i];
        if((language==L::Python || language==L::Toml || language==L::CSharp) && at(i,std::string(3,q))) {
          const auto delimiter=std::string(3,q);i+=3;
          while(i<s.size()) {
            if(at(i,delimiter)){i+=3;break;}
            if(s[i]=='\\' && i+1<s.size() && (language==L::Python || language==L::Toml && q=='"'))i+=2;
            else ++i;
          }
        }
        else i=quote(i,q,language==L::Shell || q=='`' || language==L::Sql,
                     language==L::Sql || language==L::Yaml,language!=L::Sql && !(language==L::Toml && q=='\'') && !(language==L::Shell && q=='\'') && !(language==L::Go && q=='`'));
        auto style=String;
        if((language==L::Json || language==L::Yaml) && next(i)<s.size() && s[next(i)]==':')style=Key;
        if((language==L::Toml || language==L::Ini) && next(i)<s.size() && s[next(i)]=='=')style=Key;
        if(language==L::Sql && q!='\'')style=Key;
        paint(start,i,style);
        if(language==L::Shell && q=='"') for(size_t p=start+1;p+1<i;) {
          if(s[p]=='\\')p=std::min(i,p+2);
          else if(s[p]=='$'){auto end=std::min(i,variable(p));paint(p,end,Variable);p=end;}else ++p;
        }
        regexAllowed=false;continue;
      }
      if(language==L::Shell && at(i,"<<") && !at(i,"<<<")) {
        i+=2;bool tabs=i<s.size() && s[i]=='-';if(tabs)++i;i=next(i);
        std::string delimiter;
        while(i<s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && std::string_view(";|&<>").find(s[i])==s.npos) {
          if(s[i]=='\'' || s[i]=='"') {auto end=quote(i,s[i],false);auto count=end-i;delimiter+=s.substr(i+1,count>=2?count-2:0);i=end;}
          else {if(s[i]=='\\' && i+1<s.size())++i;delimiter+=s[i++];}
        }
        if(!delimiter.empty())heredocs.emplace_back(delimiter,tabs);
        paint(start,i,Directive);continue;
      }
      if((language==L::Shell || language==L::Make || language==L::CMake || language==L::JavaScript) && s[i]=='$') {
        i=variable(i);paint(start,i,Variable);continue;
      }
      if(language==L::Yaml && (s[i]=='|' || s[i]=='>')) {
        size_t bol=lineStart(i),prefix=next(bol),p=i;
        while(p>bol && (s[p-1]==' ' || s[p-1]=='\t'))--p;
        if(p>bol && (s[p-1]==':' || s[p-1]=='-')) {
          auto end=eol(i);paint(i,end,Directive);size_t body=end<s.size()?end+1:end,finish=body;
          while(finish<s.size()) {
            auto line=eol(finish),nonspace=next(finish);
            if(nonspace<line && nonspace-finish<=prefix-bol)break;
            finish=line<s.size()?line+1:line;
          }
          paint(body,finish,String);i=finish;continue;
        }
      }
      if(language==L::JavaScript && s[i]=='/' && regexAllowed && i>=retryRegexAt && !at(i,"/=")) {
        size_t p=i+1;bool characterClass=false,done=false;
        while(p<s.size() && s[p]!='\n') {
          if(s[p]=='\\' && p+1<s.size()){p+=2;continue;}
          if(s[p]=='[')characterClass=true;else if(s[p]==']')characterClass=false;
          else if(s[p]=='/' && !characterClass){++p;done=true;break;}++p;
        }
        if(done){while(p<s.size() && std::isalpha(static_cast<unsigned char>(s[p])))++p;paint(i,p,String);i=p;regexAllowed=false;continue;}
        retryRegexAt=p; // an unterminated literal must not rescan this line at every slash
      }
      if(language==L::Css && s[i]=='#') {
        size_t end=i+1;while(end<s.size() && std::isxdigit(static_cast<unsigned char>(s[end])))++end;
        size_t digits=end-i-1;
        if((digits==3 || digits==4 || digits==6 || digits==8) && (end==s.size() || !idPart(s[end]))) {
          paint(i,end,Number);i=end;continue;
        }
      }
      if(std::isdigit(static_cast<unsigned char>(s[i])) || s[i]=='.' && (i==0 || s[i-1]!='.') && i+1<s.size() && std::isdigit(static_cast<unsigned char>(s[i+1]))) {
        i=number(i);paint(start,i,Number);regexAllowed=false;continue;
      }
      if(idStart(s[i])) {
        ++i;while(i<s.size() && (idPart(s[i]) || (language==L::Css || language==L::Yaml || language==L::Ini) && s[i]=='-'))++i;
        auto token=s.substr(start,i-start);auto lookup=language==L::Sql?lower(token):std::string(token);
        auto after=next(i);Style style=Normal;
        bool key=(language==L::Yaml || language==L::Css) && after<s.size() && s[after]==':' && (after+1==s.size() || language==L::Css || std::isspace(static_cast<unsigned char>(s[after+1])));
        key |= (language==L::Toml || language==L::Ini) && after<s.size() && s[after]=='=';
        key |= language==L::Make && after<s.size() && s[after]==':' && linePrefix(start);
        if(key)style=Key;
        else if(keywords(language).count(lookup))style=Keyword;
        else if(language!=L::Json && language!=L::Yaml && language!=L::Toml && language!=L::Ini && language!=L::Css && types(language).count(lookup))style=Type;
        else if(language==L::Rust && after<s.size() && s[after]=='!')style=Function;
        else if(after<s.size() && s[after]=='(' && language!=L::Json && language!=L::Yaml && language!=L::Toml && language!=L::Ini)style=Function;
        paint(start,i,style);
        regexAllowed=lookup=="return" || lookup=="throw" || lookup=="case" || lookup=="yield" || lookup=="await" || lookup=="typeof" || lookup=="void" || lookup=="delete" || lookup=="in" || lookup=="of";
        continue;
      }
      if((language==L::Python || language==L::Java || language==L::CSharp || language==L::Css) && s[i]=='@') {
        ++i;while(i<s.size() && (idPart(s[i]) || s[i]=='.' || s[i]=='-'))++i;paint(start,i,Directive);continue;
      }
      if(std::string_view("+-=*/%<>!&|^~?:;,.()[]{}\\").find(s[i])!=s.npos) {
        paint(i,i+1,Operator);
        regexAllowed=s[i]!=')' && s[i]!=']';
      }
      ++i;
    }
    return colors;
  }
};
std::string colorize(std::string_view text,L language) {
  if(text.size()>limit)return std::string(text.size(),Normal);
  return Lexer(text,language).run();
}
}
