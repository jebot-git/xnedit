// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu-window.h"
#include "search.h"
#include <glib.h>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>
class RegexHelper : public dx::GpuWindow {
  Fl_Hold_Browser examples{12,34,200,376};
  Fl_Input pattern{236,34,576,32,"Pattern"}, replacement{236,94,576,32,"Replacement (&& = match, \\1..\\9 = groups)"};
  Fl_Multiline_Input sample{236,154,576,162,"Sample text"};
  Fl_Check_Button sensitive{236,326,240,26,"Match case"};
  Fl_Multiline_Output preview{236,376,576,110,"Preview"}, description{12,502,800,60};
  Fl_Button insert{12,422,200,30,"Insert pattern"}, example{12,462,200,30,"Load example"},
      apply{524,576,180,32,"Use in find/replace"}, close{716,576,96,32,"Close"};
  static void delayed(void *p) { static_cast<RegexHelper *>(p)->update(); }
  void schedule() { Fl::remove_timeout(delayed,this); Fl::add_timeout(.18,delayed,this); }
  const RegexExample &selected() const {
    auto index=std::clamp(examples.value()-1,0,int(regexExamples().size())-1);
    return regexExamples()[index];
  }
  void update() {
    Fl::remove_timeout(delayed,this);
    try {
      apply.activate();
      if(!*pattern.value()){apply.deactivate();preview.value("Enter a pattern or load an example.");return;}
      if(strlen(sample.value())>16384){preview.value("Use a sample of at most 16 KiB.");return;}
      Document document;document.assign(sample.value(),true);
      auto match=document.find(pattern.value(),0,true,sensitive.value(),false,replacement.value());
      if(!match){preview.value("Valid pattern; no match in the sample.");return;}
      auto matched=document.text().substr(match->start,match->end-match->start);
      if(matched.empty())matched="(empty match)";
      auto escaped=[](const std::string &text) {
        // The legacy regex engine works on bytes; keep partial captures safe
        // for the UTF-8 display, and never clip a preview inside a code point.
        char *valid=g_utf8_make_valid(text.data(),text.size());
        std::string input(valid),value;g_free(valid);
        for(size_t i=0;i<input.size();) {
          if(value.size()>600){value+="…";break;}
          char c=input[i];
          if(c=='\n'){value+="\\n";++i;}
          else if(c=='\t'){value+="\\t";++i;}
          else if(c=='\r'){value+="\\r";++i;}
          else {size_t end=i+1;while(end<input.size() && (static_cast<unsigned char>(input[end])&0xc0)==0x80)++end;
            value.append(input,i,end-i);i=end;}
        }
        return value;
      };
      auto text="Match: "+escaped(matched)+"\nReplacement: "+escaped(match->replacement);
      preview.value(text.c_str());
    } catch(const std::exception &error) {
      apply.deactivate();
      preview.value((std::string("Pattern error: ")+error.what()).c_str());
    }
  }
public:
  std::function<void(const std::string &,const std::string &,bool)> use;
  RegexHelper():dx::GpuWindow(824,620,"Regular expression helper — XNEdit XR") {
    end();size_range(720,560);set_non_modal();
    for(Fl_Widget *field:std::initializer_list<Fl_Widget *>{&pattern,&replacement,&sample,&preview})field->align(FL_ALIGN_TOP_LEFT);
    for(auto &item:regexExamples())examples.add(item.label);
    examples.value(1);description.value(selected().description);
    examples.callback([](Fl_Widget *,void *p){auto self=static_cast<RegexHelper *>(p);self->description.value(self->selected().description);},this);
    insert.callback([](Fl_Widget *,void *p){auto self=static_cast<RegexHelper *>(p);
      auto a=self->pattern.insert_position(),b=self->pattern.mark();
      self->pattern.replace(std::min(a,b),std::max(a,b),self->selected().pattern);
      self->pattern.insert_position(std::min(a,b)+strlen(self->selected().pattern));
      self->pattern.take_focus();self->schedule();},this);
    example.callback([](Fl_Widget *,void *p){auto self=static_cast<RegexHelper *>(p);auto &item=self->selected();
      self->pattern.value(item.pattern);self->replacement.value(item.replacement);self->sample.value(item.sample);self->update();},this);
    for(Fl_Widget *field:std::initializer_list<Fl_Widget *>{&pattern,&replacement,&sample,&sensitive}) {
      field->when(FL_WHEN_CHANGED);field->callback([](Fl_Widget *,void *p){static_cast<RegexHelper *>(p)->schedule();},this);
    }
    apply.callback([](Fl_Widget *,void *p){auto self=static_cast<RegexHelper *>(p);self->hide();
      if(self->use)self->use(self->pattern.value(),self->replacement.value(),self->sensitive.value());},this);
    close.callback([](Fl_Widget *,void *p){static_cast<RegexHelper *>(p)->hide();},this);
    callback([](Fl_Widget *w,void *){w->hide();});
    pattern.tooltip("XNEdit regular expression; choose a token and Insert pattern, or Load example.");
    replacement.tooltip("& = full match; \\1 through \\9 = capture groups; $1 is literal text.");
    description.wrap(1);preview.wrap(1);
    dx::style(this);
  }
  ~RegexHelper() { Fl::remove_timeout(delayed,this); }
  void values(const std::string &expression,const std::string &replace,const std::string &text,bool matchCase) {
    pattern.value(expression.c_str());replacement.value(replace.c_str());sensitive.value(matchCase);
    size_t end=std::min(text.size(),size_t(16384));
    while(end<text.size() && end>0 && (static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
    sample.value(text.substr(0,end).c_str());update();
  }
  int handle(int event) override {
    if((event==FL_KEYDOWN || event==FL_SHORTCUT) && Fl::event_key()==FL_Escape){hide();return 1;}
    return dx::GpuWindow::handle(event);
  }
  void resize(int x,int y,int w,int h) override {
    dx::GpuWindow::resize(x,y,w,h);
    examples.resize(12,34,200,h-244);
    pattern.resize(236,34,w-248,32);replacement.resize(236,94,w-248,32);
    sample.resize(236,154,w-248,h-458);sensitive.resize(236,h-294,240,26);
    preview.resize(236,h-244,w-248,110);description.resize(12,h-118,w-24,60);
    insert.resize(12,h-198,200,30);example.resize(12,h-158,200,30);
    apply.resize(w-300,h-44,180,32);close.resize(w-108,h-44,96,32);
  }
};
