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
static Fl_Widget *widget(Fl_Group &group,const char *label) {
  for(int i=0;i<group.children();++i) {
    auto child=group.child(i);
    if(child->label() && std::string(child->label())==label)return child;
    if(auto nested=dynamic_cast<Fl_Group *>(child))if(auto found=widget(*nested,label))return found;
  }
  return nullptr;
}
static void capture(Fl_Window &window,const char *name) {
  auto directory=getenv("XNEDIT_UI_CAPTURE_DIR");if(!directory)return;
  settle();std::unique_ptr<Fl_RGB_Image> image(fl_capture_window(&window,0,0,window.w(),window.h()));
  assert(image && image->data());
  std::ofstream out(fs::path(directory)/(std::string(name)+".ppm"),std::ios::binary);
  out<<"P6\n"<<image->w()<<' '<<image->h()<<"\n255\n";
  int stride=image->ld()?image->ld():image->w()*image->d();
  for(int y=0;y<image->h();++y)for(int x=0;x<image->w();++x)out.write(image->data()[0]+y*stride+x*image->d(),3);
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
  page->doc.path="fixture.rs";
  text("fn main() {\n    let text = r#\"hello 日\"#;\n    // nested /* comment */\n    let value: i32 = 42;\n}\n");
  page->highlight();
  assert(page->detectedLanguage==highlight::Language::Rust);
  auto code=page->style.text();assert(code[0]==highlight::Keyword);free(code);
  window.command("Plain text");code=page->style.text();
  assert(std::string(code).find_first_not_of(highlight::Normal)==std::string::npos);free(code);
  window.command("Automatic");assert(page->detectedLanguage==highlight::Language::Rust);
  page->doc.path="fixture.py";window.poll();
  assert(page->detectedLanguage==highlight::Language::Python);
  window.command("Syntax highlighting");code=page->style.text();
  assert(std::string(code).find_first_not_of(highlight::Normal)==std::string::npos);free(code);
  window.command("Syntax highlighting");
  text("async def greet(name):\n    \"\"\"A multiline docstring.\n    Comments stay inside this string.\"\"\"\n    # Unicode names stay intact.\n    return print(\"Hello 日\", 42)\n");
  page->highlight();settle();capture(window,"highlight-dark");
  std::cout<<"PASS Language override, path changes, highlighting toggle and both split views\n";
  for (auto mode : {highlight::Language::Gemini, highlight::Language::Gopher,
                    highlight::Language::GopherPlus, highlight::Language::Nex,
                    highlight::Language::Spartan, highlight::Language::Scroll}) {
    text("=> /about About\n=: /submit Prompt\n# Heading\n");
    auto unchanged=page->doc.text();
    window.command(highlight::name(mode));
    assert(page->detectedLanguage==mode && page->doc.text()==unchanged);
    code=page->style.text();
    assert(std::string(code)==highlight::colorize(unchanged,mode));free(code);
  }
  page->doc.path="fixture.gmi";window.command("Automatic");
  assert(page->detectedLanguage==highlight::Language::Spartan);
  text("# Gemini 日本語\n=> gemini://example/ Capsule\n* Item\n> Quote\n``` ASCII art\n=> literal\n```\n");
  page->highlight();assert(page->detectedLanguage==highlight::Language::Gemini);
  capture(window,"highlight-gemini");
  page->doc.path="fixture.scroll";
  text("# Scroll 日本語\n##### Small heading\n** １２. Ordered sub-list\n> _Quote_ with *strong* and `code`\n=> #3.2 Section [Citation]\n=: /search Search\n---\n\\=> Escaped link\n");
  page->highlight();assert(page->detectedLanguage==highlight::Language::Scroll);
  capture(window,"highlight-scroll");
  window.command("Syntax highlighting");code=page->style.text();
  assert(std::string(code).find_first_not_of(highlight::Normal)==std::string::npos);free(code);
  window.command("Syntax highlighting");
  page->doc.path="fixture.py";window.command("Automatic");
  std::cout<<"PASS All six protocol modes, automatic Gemini/Spartan detection, Scroll, split styling and syntax toggle\n";
  // Drive the real helper controls, then apply them to the native search panel.
  window.command("Regex helper...");settle();
  RegexHelper *helper=nullptr;
  for(auto w=Fl::first_window();w;w=Fl::next_window(w))if(auto h=dynamic_cast<RegexHelper *>(w))helper=h;
  assert(helper && helper->shown());
  auto pattern=dynamic_cast<Fl_Input *>(widget(*helper,"Pattern"));
  auto repl=dynamic_cast<Fl_Input *>(widget(*helper,"Replacement (&& = match, \\1..\\9 = groups)"));
  auto sample=dynamic_cast<Fl_Input *>(widget(*helper,"Sample text"));
  auto preview=dynamic_cast<Fl_Input *>(widget(*helper,"Preview"));
  assert(pattern && repl && sample && preview);
  Fl_Hold_Browser *examples=nullptr;
  for(int i=0;i<helper->children();++i)if(auto browser=dynamic_cast<Fl_Hold_Browser *>(helper->child(i)))examples=browser;
  assert(examples);examples->value(6);examples->do_callback();
  widget(*helper,"Load example")->do_callback();settle();
  assert(std::string(preview->value()).find("Replacement: Jane Doe")!=std::string::npos);
  pattern->value("");pattern->insert_position(0);
  widget(*helper,"Insert pattern")->do_callback();settle();
  assert(std::string(pattern->value())==regexExamples()[5].pattern);
  assert(pattern->insert_position()==int(strlen(pattern->value())));
  pattern->value("[");pattern->do_callback();settle();settle();
  assert(std::string(preview->value()).find("error")!=std::string::npos);
  pattern->value(R"((\d+))");repl->value(R"(n\1)");sample->value("a1 a2");pattern->do_callback();settle();settle();
  assert(std::string(preview->value()).find("Replacement: n1")!=std::string::npos);
  capture(*helper,"regex-helper");
  helper->resize(helper->x(),helper->y(),720,560);settle();capture(*helper,"regex-helper-small");
  widget(*helper,"Use in find/replace")->do_callback();settle();
  assert(!helper->shown());
  auto findField=dynamic_cast<Fl_Input *>(widget(window,"Find:"));
  auto replaceField=dynamic_cast<Fl_Input *>(widget(window,"Replace:"));
  auto regexBox=dynamic_cast<Fl_Check_Button *>(widget(window,"Regex"));
  assert(findField && replaceField && regexBox && regexBox->value());
  assert(std::string(findField->value())==R"((\d+))");
  text("a1 a2");editor->insert_position(0);window.find(false);window.replace(false);
  assert(page->doc.text()=="an1 a2");
  text("a1 a2");window.replace(true);assert(page->doc.text()=="an1 an2");
  window.command("Undo");assert(page->doc.text()=="a1 a2");
  std::cout<<"PASS Regex helper validation, capture preview, apply, replace one/all and undo\n";
  findField->value("^");text("one\ntwo");editor->insert_position(0);window.find(false);
  assert(editor->insert_position()==0);window.find(false);assert(editor->insert_position()==4);
  window.find(true);assert(editor->insert_position()==0);
  findField->value("[");window.find(false);assert(page->doc.text()=="one\ntwo");
  regexBox->value(0);findField->value("one");replaceField->value("&");window.replace(true);
  assert(page->doc.text()=="&\ntwo");
  std::cout<<"PASS Zero-width find progress, previous search, inline errors and literal replacements\n";
  fs::create_directories(dx::config().parent_path());
  atomicWrite(dx::config(),Json{{"light",true},{"font","Unifont / Sans-Serif"},{"font_size",16}}.dump());
  dx::appearance();window.poll();
  text("def f():\n    return \"hello\" # comment\n");page->highlight();capture(window,"highlight-light");
  page->doc.readOnly=true;auto readOnlyBefore=page->doc.text();window.replace(true);
  assert(page->doc.text()==readOnlyBefore);
  page->doc.readOnly = true;
  auto before = page->doc.text();
  press(editor, FL_KP_Enter);
  assert(page->doc.text() == before);
  std::cout << "PASS Keypad Enter respects read-only mode\n";
  if(auto uri=getenv("XNEDIT_REMOTE_TEST_URL")) {
    auto waitRemote=[&]{auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(window.remoteBusy()&&std::chrono::steady_clock::now()<end){Fl::wait(.01);window.poll();}assert(!window.remoteBusy());settle();};
    window.retrieveRemote(uri);waitRemote();auto remote=window.current();
    assert(remote->remote && remote->doc.text()=="# Original\nHello 世界\n" && remote->doc.eol=="CRLF");
    assert(remote->detectedLanguage==highlight::Language::Gemini && remote->doc.path.empty() && !remote->doc.modified());
    remote->doc.replace(0,remote->doc.length(),"# Remote UI\nEdited 世界\n");remote->refresh();
    window.command("Save");waitRemote();assert(!remote->doc.modified());
    std::cout<<"PASS Native remote retrieve, Gemtext mode, UTF-8/CRLF preservation and Ctrl+S Titan push\n";
    capture(window,"remote-page");
    remote->doc.replace(remote->doc.length(),remote->doc.length(),"snapshot\n");remote->refresh();window.command("Save");
    remote->doc.replace(remote->doc.length(),remote->doc.length(),"later\n");remote->refresh();waitRemote();assert(remote->doc.modified());
    auto originalEdits=remote->doc.text();window.retrieveRemote(uri,remote);
    remote->doc.replace(remote->doc.length(),remote->doc.length(),"during reload\n");remote->refresh();waitRemote();
    assert(window.current()!=remote && remote->doc.text()==originalEdits+"during reload\n");
    std::cout<<"PASS Edits made during push stay dirty; edits made during reload are retained in their original tab\n";
    window.recoverSave();Json recovery;std::ifstream(remote->recovery)>>recovery;
    assert(recovery["remote"]["read"]==uri && recovery["remote"].contains("baseline"));
    assert(recovery.dump().find("PRIVATE KEY")==std::string::npos);
    window.showRemoteIdentity();settle();
    Fl_Window *identityDialog=nullptr;for(auto w=Fl::first_window();w;w=Fl::next_window(w))if(std::string(w->label()?w->label():"")=="Remote identity — XNEdit XR")identityDialog=w;
    assert(identityDialog);auto identities=dynamic_cast<Fl_Choice *>(widget(*identityDialog,"Penguin identity:"));assert(identities && identities->text() && std::string(identities->text())=="test@@localhost");
    capture(*identityDialog,"remote-identity");widget(*identityDialog,"Cancel")->do_callback();
    window.pushRemote(window.current(),true);settle();Fl_Window *pushDialog=nullptr;
    for(auto w=Fl::first_window();w;w=Fl::next_window(w))if(std::string(w->label()?w->label():"")=="Push remote document — XNEdit XR")pushDialog=w;
    assert(pushDialog);auto secret=dynamic_cast<Fl_Input *>(widget(*pushDialog,"Token:"));assert(secret);secret->value("ui-secret");
    capture(*pushDialog,"remote-push");widget(*pushDialog,"Cancel")->do_callback();assert(std::string(secret->value()).empty());
    std::cout<<"PASS Remote recovery metadata, private identity selection and non-persistent push-token dialog\n";
  }
  if(auto uri=getenv("XNEDIT_SPARTAN_TEST_URL")) {
    auto waitRemote=[&]{auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(window.remoteBusy()&&std::chrono::steady_clock::now()<end){Fl::wait(.01);window.poll();}assert(!window.remoteBusy());settle();};
    window.retrieveRemote(uri);waitRemote();auto remote=window.current();
    assert(remote->remote && remote->remote->identity.empty() && remote->doc.text()=="# Original\nHello 世界\n");
    assert(remote->detectedLanguage==highlight::Language::Spartan && remote->remote->pushUrl==uri);
    remote->doc.replace(0,remote->doc.length(),"# Spartan UI\n=: /submit Prompt\nEdited 世界\n");remote->refresh();
    window.command("Save");waitRemote();assert(!remote->doc.modified());capture(window,"remote-spartan");
    remote->doc.replace(remote->doc.length(),remote->doc.length(),"later edit\n");remote->refresh();window.recoverSave();
    Json recovery;std::ifstream(remote->recovery)>>recovery;
    assert(recovery["remote"]["read"]==uri && recovery["remote"]["push"]==uri && recovery["remote"]["identity"]["penguin_id"]=="");
    window.pushRemote(remote,true);settle();Fl_Window *dialog=nullptr;
    for(auto w=Fl::first_window();w;w=Fl::next_window(w))if(std::string(w->label()?w->label():"")=="Push remote document — XNEdit XR")dialog=w;
    assert(dialog);auto mime=widget(*dialog,"MIME type:");auto token=dynamic_cast<Fl_Input *>(widget(*dialog,"Token:"));
    assert(mime && !mime->active() && token && !token->active() && std::string(token->value()).empty());
    capture(*dialog,"remote-spartan-push");widget(*dialog,"Cancel")->do_callback();
    std::cout<<"PASS Native Spartan retrieve/edit/upload, syntax mode, recovery and protocol-specific push controls\n";
  }
  for(auto pair:std::vector<std::pair<const char *,const char *>>{{"XNEDIT_HTTP_TEST_URL","HTTP"},{"XNEDIT_SFTP_TEST_URL","SFTP"}}) {
    if(auto uri=getenv(pair.first)) {
      auto waitRemote=[&]{auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(window.remoteBusy()&&std::chrono::steady_clock::now()<end){Fl::wait(.01);window.poll();}assert(!window.remoteBusy());settle();};
      window.retrieveRemote(uri);waitRemote();auto remote=window.current();
      assert(remote->remote && remote->doc.text()=="Original\n" && remote->doc.eol=="CRLF" && remote->remote->identity.empty());
      std::string replacement=std::string(pair.second)+" UI 世界\n";
      remote->doc.replace(0,remote->doc.length(),replacement);remote->refresh();
      window.command("Save");waitRemote();assert(!remote->doc.modified() && remote->remote->readUrl==uri);
      remote->doc.replace(remote->doc.length(),remote->doc.length(),"snapshot\n");remote->refresh();window.command("Save");
      remote->doc.replace(remote->doc.length(),remote->doc.length(),"later\n");remote->refresh();waitRemote();assert(remote->doc.modified());
      window.recoverSave();Json recovery;std::ifstream(remote->recovery)>>recovery;
      assert(recovery["remote"]["read"]==uri && recovery["remote"]["push"]==uri);
      capture(window,pair.second);
      std::cout<<"PASS Existing Remote workflow retrieves, saves, preserves concurrent edits and recovers "<<pair.second<<" documents\n";
    }
  }
  if(auto uri=getenv("XNEDIT_NC_TEST_URL")) {
    auto waitRemote=[&]{auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(window.remoteBusy()&&std::chrono::steady_clock::now()<end){Fl::wait(.01);window.poll();}assert(!window.remoteBusy());settle();};
    window.retrieveRemote(uri);waitRemote();auto remote=window.current();
    assert(remote->remote && remote->doc.text()=="Response 世界\n");
    remote->doc.replace(remote->doc.length(),remote->doc.length(),"raw edit\n");remote->refresh();
    auto before=remote->doc.text();window.command("Save");settle();
    Fl_Window *dialog=nullptr;for(auto w=Fl::first_window();w;w=Fl::next_window(w))if(std::string(w->label()?w->label():"")=="Push remote document — XNEdit XR" && w->shown())dialog=w;
    assert(dialog && !widget(*dialog,"Token:")->active() && !widget(*dialog,"MIME type:")->active());
    widget(*dialog,"Check and push")->do_callback();waitRemote();
    assert(remote->doc.modified() && remote->doc.text()==before);window.recoverSave();

    capture(window,"remote-nc");std::cout<<"PASS Existing Push dialog sends byte streams without clearing unsaved edits\n";
  }
  if(auto uri=getenv("XNEDIT_FINGER_TEST_URL")) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);window.retrieveRemote(uri);
    while(window.remoteBusy()&&std::chrono::steady_clock::now()<end){Fl::wait(.01);window.poll();}
    assert(!window.remoteBusy());auto remote=window.current();
    assert(remote->remote && remote->doc.text()=="Response 世界\n" && remote->remote->pushUrl.empty());
    remote->doc.replace(remote->doc.length(),remote->doc.length(),"plan edit\n");remote->refresh();window.recoverSave();
    Json recovery;std::ifstream(remote->recovery)>>recovery;assert(recovery["remote"]["push"]=="");
    window.command("Save");settle();Fl_Window *dialog=nullptr;
    for(auto w=Fl::first_window();w;w=Fl::next_window(w))if(std::string(w->label()?w->label():"")=="Push remote document — XNEdit XR" && w->shown())dialog=w;
    assert(dialog);widget(*dialog,"Cancel")->do_callback();assert(remote->doc.modified());
    std::cout<<"PASS Finger retrieval remains editable and Save offers the existing upload destination dialog\n";
  }
  for(auto pair:std::vector<std::pair<const char *,highlight::Language>>{{"XNEDIT_SCROLL_TEST_URL",highlight::Language::Scroll},{"XNEDIT_PLUS_TEST_URL",highlight::Language::GopherPlus}}) {
    if(auto uri=getenv(pair.first)) {
      auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);window.retrieveRemote(uri);
      while(window.remoteBusy()&&std::chrono::steady_clock::now()<end){Fl::wait(.01);window.poll();}
      assert(!window.remoteBusy());auto remote=window.current();
      assert(remote->remote && remote->remote->pushUrl.empty() && remote->detectedLanguage==pair.second);
      if(pair.second==highlight::Language::Scroll)assert(remote->doc.text()=="# Scroll\n* Content 世界\n");
      else assert(remote->doc.text().find("+INFO:")==0);
      capture(window,pair.second==highlight::Language::Scroll?"remote-scroll":"remote-gopherplus");
      std::cout<<"PASS Existing Remote menu retrieves and selects "<<highlight::name(pair.second)<<" syntax\n";
    }
  }
  window.hide();
}
