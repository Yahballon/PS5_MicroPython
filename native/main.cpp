// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 MicroPython by YAHBALLON v0.2.3
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "port/micropython_embed.h"
#define PS5_MICROPYTHON_NATIVE_STORAGE 1
#include "editor.hpp"
#define PS5_MICROPYTHON_NATIVE_SCAN 1
#include "scripts_scan.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
const char* const kScripts="/data/PS5MicroPython/scripts";
const size_t kMaxSource=65536, kMaxOutput=24576;
constexpr int kVisibleScripts=8; // Keep lower-left script-folder instructions visible.
alignas(16) char heap[256*1024];
std::string capture;
struct Script {std::string name,path;bool builtin;};
std::vector<Script> library;
std::string scanStatus="SCAN: not started";
std::string scanDetails;
enum class Page {list,editor,confirm_discard};
Page page=Page::list;
pyedit::Buffer editor;
std::string editorName, editorMessage;
bool editorNew=false;
int keyboardIndex=0, keyboardPage=0;
std::string readFile(const std::string& path,size_t limit) {
 FILE* f=fopen(path.c_str(),"rb"); if(!f)return {};
 if(fseek(f,0,SEEK_END)){fclose(f);return {};}
 long n=ftell(f); if(n<1||static_cast<unsigned long>(n)>limit){fclose(f);return {};}
 rewind(f); std::string s(size_t(n),'\0');
 size_t got=fread(s.data(),1,s.size(),f);fclose(f);
 return got==s.size()?s:std::string();
}
void dirs() {
 mkdir("/data/PS5MicroPython",0755);
 mkdir(kScripts,0755);
 mkdir("/data/PS5MicroPython/logs",0755);
}
bool validName(const std::string& s) {
 return s.size()>3 && s.size()<100 && s[0]!='.' &&
 s.compare(s.size()-3,3,".py")==0 && s.find('/')==std::string::npos &&
 s.find('\\')==std::string::npos;
}
void reload() {
 library.clear();
 for(const char* n:{"hello.py","fibonacci.py","collections.py"})
  library.push_back({n,std::string("/app0/assets/scripts/")+n,true});
 const auto result=pyscan::scan_roots(pyscan::roots());
 for(const auto& file:result.files)
  library.push_back({file.source+"  "+file.name,file.path,false});
 if(library.size()>3)
  std::sort(library.begin()+3,library.end(),
   [](const Script& a,const Script& b){return a.name<b.name;});
 scanStatus=result.status();
 scanDetails.clear();
 for(const auto& root:result.roots){
  char msg[400];
  std::snprintf(msg,sizeof(msg),"root=%s open=%d items=%u found=%u open_error=%d read_error=%d\n",
      root.path.c_str(),root.opened?1:0,root.entries,root.found,
      root.open_code,root.read_code);
  if(scanDetails.size()<6000)scanDetails+=msg;
 }
 // The UI always displays the status even when libc logging is unavailable.
 if(FILE* log=std::fopen("/data/PS5MicroPython/logs/scan.log","ab")){
  std::fprintf(log,"%s\n%s",scanStatus.c_str(),scanDetails.c_str());
  std::fclose(log);
 }
}
void logOutput(const std::string& name){
 if(FILE* f=fopen("/data/PS5MicroPython/logs/runtime.log","ab")){
  fprintf(f,"\n=== %s ===\n",name.c_str());
  fwrite(capture.data(),1,capture.size(),f);fputc('\n',f);fclose(f);
 }
}
void run(const Script& script,void* stack){
 capture=">>> Running "+script.name+"\n";
 std::string code=pyscan::read_script(script.path,kMaxSource);
 if(code.empty()&&script.builtin)code=readFile(script.path,kMaxSource);
 if(code.empty()&&script.builtin)
  code=readFile(std::string("/data/homebrew/PPSA99791/assets/scripts/")+script.name,kMaxSource);
 if(code.empty()){
  capture+="Could not read Python script (max 64 KiB).\n";logOutput(script.name);return;
 }
 mp_embed_init(heap,sizeof(heap),stack);
 mp_embed_exec_str(code.c_str());
 mp_embed_deinit();
 capture+="\n>>> Finished.\n";logOutput(script.name);
}
std::vector<std::string> getLines(const std::string& s) {
 std::vector<std::string> lines;
 size_t start=0;
 while(start<=s.size()&&lines.size()<1024){
  size_t end=s.find('\n',start);
  if(end==std::string::npos)end=s.size();
  std::string line=s.substr(start,end-start);
  if(!line.empty()&&line.back()=='\r')line.pop_back();
  do{
   size_t n=std::min<size_t>(92,line.size());
   lines.push_back(line.substr(0,n));line.erase(0,n);
  }while(!line.empty()&&lines.size()<1024);
  if(end==s.size())break;
  start=end+1;
 }
 return lines;
}
std::string shortName(const std::string& s){
 return s.size()>32?s.substr(0,29)+"...":s;
}
// Match new filenames against the PS5-native getdents scan instead of lstat.
// If the DATA directory cannot be scanned, do not risk overwriting any file.
std::string next_filename(const char* prefix="script") {
 const auto report=pyscan::scan_roots({{kScripts,"DATA"}});
 if(report.roots.empty()||!report.roots[0].opened||
    report.roots[0].read_code!=0||
    report.files.size()>=pyscan::kMaxUserScripts||report.visited>=4096){
  capture="\nCannot scan DATA script directory.\n";
  return {};
 }
 for(int i=1;i<1000;i++){
  char suffix[16];
  std::snprintf(suffix,sizeof(suffix),"_%03d.py",i);
  const std::string name=std::string(prefix)+suffix;
  bool used=false;
  // Includes symlinks and unreadable entries; avoid overwriting any of them.
  for(const auto& filename:report.filenames_seen)
   if(filename==std::string(kScripts)+"/"+name){used=true;break;}
  if(!used)return name;
 }
 return {};
}
void start_new() {
 editorName=next_filename();
 if(editorName.empty()){capture="\nToo many filenames; cannot create another script.\n";return;}
 editor.reset("# New MicroPython script\nprint('Hello from PS5!')\n");
 editor.dirty=true;editorNew=true;page=Page::editor;
 editorMessage="New script: press OPTIONS to save";
 keyboardIndex=0;keyboardPage=0;
}
void start_edit(const Script& s) {
 auto code=pyscan::read_script(s.path,kMaxSource);
 if(code.empty()&&s.builtin)code=readFile(s.path,kMaxSource);
 if(code.empty()&&s.builtin)
  code=readFile(std::string("/data/homebrew/PPSA99791/assets/scripts/")+s.name,kMaxSource);
 if(code.empty()){capture="\nCould not load script for editing.\n";return;}
 if(s.builtin||s.path.rfind(std::string(kScripts)+"/",0)!=0) {
  editorName=next_filename(s.builtin?"demo":"usb");
  if(editorName.empty()){capture="\nNo available filename for demo copy.\n";return;}
 }else{
  editorName=s.path.substr(std::string(kScripts).size()+1);
  if(!pyedit::valid_filename(editorName)){capture="\nUnsafe filename.\n";return;}
 }
 editor.reset(code);editorNew=s.builtin||s.path.rfind(std::string(kScripts)+"/",0)!=0;
 page=Page::editor;keyboardIndex=0;keyboardPage=0;
 editorMessage=s.builtin?"Demo: save makes DATA copy.":editorNew?"USB read-only: save makes DATA copy.":"Editing existing DATA file.";
}
bool save_editor() {
 if(editorNew){
  // New/demo/USB scripts must never silently overwrite an older user file.
  const char* prefix=editorName.rfind("demo_",0)==0?"demo":
                     editorName.rfind("usb_",0)==0?"usb":"script";
  const std::string fresh=next_filename(prefix);
  if(fresh.empty()){editorMessage="DATA scan unavailable; not saved";return false;}
  editorName=fresh;
 }
 int kernel_error=0;
 const auto result=pyedit::save_script(kScripts,editorName,editor.code,&kernel_error);
 editorMessage=pyedit::save_message(result);
 if(result!=pyedit::SaveResult::ok){
  const char* step=result==pyedit::SaveResult::temp_failed?"OPEN":
                   result==pyedit::SaveResult::write_failed?"WRITE":
                   result==pyedit::SaveResult::rename_failed?"RENAME":
                   result==pyedit::SaveResult::unsafe_target?"UNSAFE":"INPUT";
  editorMessage=std::string("SAVE ")+step+" "+std::to_string(kernel_error);
 }
 if(result!=pyedit::SaveResult::ok)return false;
 editor.dirty=false;
 page=Page::list;
 reload();
 for(size_t i=0;i<library.size();i++)
  if(!library[i].builtin && library[i].path==std::string(kScripts)+"/"+editorName){
   capture="\nSaved "+editorName+" - press X to run it.\n";return true;
  }
 capture="\nSaved "+editorName+" but not listed yet. "+scanStatus+"\n";
 return true;
}
}
extern "C" void ps5_mp_console_write(const char* bytes,size_t len){
 if(!bytes)return;
 for(size_t i=0;i<len&&capture.size()<kMaxOutput;i++)
  if(bytes[i]!='\r')capture.push_back(bytes[i]);
}
int main(){
 dirs();reload();
 hui::ps5::Display display;if(!display.open())hui::sys::park();
 hui::gfx::Renderer renderer;if(!renderer.init())hui::sys::park();
 hui::gfx::Font font;
 std::string fonts=readFile("/app0/assets/fonts/inter-semibold.huifont",16000000);
 if(fonts.empty())fonts=readFile("/data/homebrew/PPSA99791/assets/fonts/inter-semibold.huifont",16000000);
 if(!font.load(fonts))hui::sys::park();
 unsigned atlas=renderer.batch().create_font_texture(font);
 hui::ps5::Pad pad;pad.open();
 int selected=0,scroll=0,stack_anchor=0;unsigned previous=0;
 unsigned long long repeatAt=0;
 capture="MicroPython is ready.\nSelect a script and press X to run.\n";
 using namespace hui::gfx;
 const Color fg=Color::rgb(0xeef5ff),cyan=Color::rgb(0x5fe5fb);
 const Color mute=Color::rgb(0x8fa8ca),edge=Color::rgb(0x233d5d);
 for(;;){
  std::array<hui::PadSample,64> samples{};
  auto n=pad.read(samples);unsigned keys=0,pressed=0;bool connected=false;
  for(size_t i=0;i<n;i++)if(samples[i].connected){
   connected=true;keys=samples[i].buttons;
   if(samples[i].left_y<64)keys|=hui::pad_bits::kUp;
   if(samples[i].left_y>192)keys|=hui::pad_bits::kDown;
   if(samples[i].left_x<64)keys|=hui::pad_bits::kLeft;
   if(samples[i].left_x>192)keys|=hui::pad_bits::kRight;
   pressed|=keys&~previous;previous=keys;
  }
  if(!connected)previous=0;
  auto now=static_cast<unsigned long long>(hui::sys::monotonic_us());
  unsigned dirs=keys&(hui::pad_bits::kUp|hui::pad_bits::kDown|hui::pad_bits::kLeft|hui::pad_bits::kRight);
  unsigned nav=pressed&dirs;
  if(nav)repeatAt=now+320000;
  else if(dirs&&now>=repeatAt){nav=dirs;repeatAt=now+120000;}
  else if(!dirs)repeatAt=0;
  if(page==Page::list){
   if((nav&hui::pad_bits::kUp)&&selected>0)--selected;
   if((nav&hui::pad_bits::kDown)&&selected+1<int(library.size()))++selected;
   if(pressed&hui::pad_bits::kTriangle){
    reload();selected=std::min(selected,std::max(0,int(library.size())-1));
    capture+="\n"+scanStatus+"\n"+scanDetails.substr(0,2000);
   }
   if(pressed&hui::pad_bits::kSquare){
    if(!library.empty())start_edit(library[size_t(selected)]);
   }
   if(pressed&hui::pad_bits::kTouchpad){capture.clear();scroll=0;}
   if(pressed&hui::pad_bits::kOptions)start_new();
   if(pressed&hui::pad_bits::kCross){
    if(!library.empty())run(library[size_t(selected)],&stack_anchor);
    scroll=0;
   }
   if(pressed&hui::pad_bits::kL1)scroll=std::max(0,scroll-8);
   if(pressed&hui::pad_bits::kR1)scroll+=8;
   if(pressed&hui::pad_bits::kCircle){
    pad.close();renderer.release();display.close();hui::sys::quit();
   }
  }else if(page==Page::editor){
   if(nav&hui::pad_bits::kUp)keyboardIndex=(keyboardIndex+40)%50;
   if(nav&hui::pad_bits::kDown)keyboardIndex=(keyboardIndex+10)%50;
   if(nav&hui::pad_bits::kLeft)keyboardIndex=(keyboardIndex/10)*10+(keyboardIndex%10+9)%10;
   if(nav&hui::pad_bits::kRight)keyboardIndex=(keyboardIndex/10)*10+(keyboardIndex%10+1)%10;
   if(pressed&hui::pad_bits::kR2)keyboardPage=(keyboardPage+1)%3;
   if(pressed&hui::pad_bits::kL1)editor.left();
   if(pressed&hui::pad_bits::kR1)editor.right();
   if(pressed&hui::pad_bits::kL3)editor.vertical(-1);
   if(pressed&hui::pad_bits::kR3)editor.vertical(1);
   if(pressed&hui::pad_bits::kL2 && !editor.insert("    "))
    editorMessage="Script size limit (64 KiB)";
   if(pressed&hui::pad_bits::kCross){
    char c=pyedit::character(keyboardPage,keyboardIndex);
    if(!editor.insert(c=='\t'?std::string("    "):std::string(1,c)))
     editorMessage="Script size limit (64 KiB)";
    else editorMessage="Unsaved changes";
   }
   if(pressed&hui::pad_bits::kSquare)editor.backspace();
   if(pressed&hui::pad_bits::kTriangle){
    if(!editor.insert("\n"))editorMessage="Script size limit (64 KiB)";
   }
   if(pressed&hui::pad_bits::kOptions){
    if(save_editor()){
     for(size_t i=0;i<library.size();i++)
      if(library[i].path==std::string(kScripts)+"/"+editorName)selected=int(i);
    }
   }
   if(pressed&hui::pad_bits::kCircle){
    page=editor.dirty?Page::confirm_discard:Page::list;
   }
  }else if(page==Page::confirm_discard){
   if(pressed&hui::pad_bits::kCross)page=Page::list;
   else if(pressed&hui::pad_bits::kCircle)page=Page::editor;
  }
  DrawList ui;
  ui.gradient_rect({0,0,1920,1080},0,Color::rgb(0x0b1427),Color::rgb(0x040710));
  ui.rounded_rect({48,45,1824,103},18,Color::rgb(0x101d34));
  ui.rounded_rect({70,68,9,57},4,cyan);
  ui.text(font,atlas,"PS5 MicroPython",103,116,47,fg);
  ui.text(font,atlas,"by YAHBALLON   v0.2.3",1838,109,23,mute,Align::right);
  if(page==Page::list){
  ui.bordered_rect({48,171,610,811},19,Color::rgb(0x0c172b),2,edge);
  ui.bordered_rect({679,171,1193,811},19,Color::rgb(0x07101e),2,edge);
  ui.text(font,atlas,"PYTHON SCRIPTS",84,228,26,cyan);
  ui.text(font,atlas,"CONSOLE OUTPUT",721,228,26,cyan);
  ui.text(font,atlas,"MicroPython Embed   |   256 KB heap",1833,229,18,mute,Align::right);
  ui.line(84,251,627,251,2,edge);ui.line(721,251,1830,251,2,edge);
  ui.text(font,atlas,scanStatus,721,276,18,
          scanStatus.rfind("SCAN ERROR:",0)==0?Color::rgb(0xffb66b):mute);
  int first=std::max(0,selected-(kVisibleScripts-1));
  for(int i=first;i<int(library.size())&&i<first+kVisibleScripts;i++){
   int y=274+(i-first)*53;bool active=(i==selected);
   ui.bordered_rect({80.f,float(y),543,46},9,active?Color::rgb(0x193a61):Color::rgb(0x102039),
                    active?2.f:1.f,active?cyan:edge);
   ui.text(font,atlas,active?">":" ",99,y+30,22,cyan);
   ui.text(font,atlas,shortName(library[size_t(i)].name),130,y+30,22,fg);
   ui.text(font,atlas,library[size_t(i)].builtin?"DEMO":"LOCAL",
           600,y+29,15,mute,Align::right);
  }
  // Persistent guidance remains visible even after executing a Python script.
  // Eight script rows end at y=691; this card starts at y=726 with no overlap.
  ui.bordered_rect({80,726,543,233},14,Color::rgb(0x0d2540),2,edge);
  ui.text(font,atlas,"ADD YOUR OWN SCRIPTS",105,766,23,cyan);
  ui.line(105,784,597,784,1,edge);
  ui.text(font,atlas,"Place your .py files here:",105,821,20,fg);
  ui.text(font,atlas,"/data/PS5MicroPython/scripts/",105,857,20,cyan);
  ui.text(font,atlas,"Transfer via FTP or a file manager.",105,895,18,mute);
  ui.text(font,atlas,"USB: /mnt/usb0/scripts  |  TRIANGLE rescan",105,930,17,mute);
  auto lines=getLines(capture);
  const int visible=23, last=std::max(0,int(lines.size())-visible);
  scroll=std::min(scroll,last);
  int start=std::max(0,last-scroll);
  for(int i=0;i<visible&&start+i<int(lines.size());i++)
   ui.text(font,atlas,lines[size_t(start+i)],722,317.f+i*27.2f,20,fg);
  ui.rounded_rect({48,996,1824,61},13,Color::rgb(0x101d34));
  ui.text(font,atlas,
   "D-PAD Select     X Run     SQUARE Edit     OPTIONS New     TRIANGLE Reload     L1/R1 Scroll     O Exit",
   960,1035,19,mute,Align::center);
  }else{
   ui.bordered_rect({48,171,1159,811},19,Color::rgb(0x0c172b),2,edge);
   ui.bordered_rect({1225,171,647,811},19,Color::rgb(0x07101e),2,edge);
   ui.text(font,atlas,"PYTHON CODE EDITOR",81,222,28,cyan);
   ui.text(font,atlas,editorName,1177,224,22,fg,Align::right);
   ui.text(font,atlas,editor.dirty?"UNSAVED":"SAVED",81,258,18,
           editor.dirty?Color::rgb(0xffc164):Color::rgb(0x55edb3));
   ui.line(80,276,1171,276,2,edge);
   auto codeLines=editor.display_lines();
   const int visible=21;
   const int cursorLine=int(editor.line());
   const int firstLine=std::max(0,cursorLine-visible+2);
   for(int i=firstLine;i<int(codeLines.size())&&i<firstLine+visible;i++){
    float y=316.f+(i-firstLine)*29.f;
    const bool active=i==cursorLine;
    if(active)ui.rounded_rect({78,y-23,1098,31},5,Color::rgb(0x152d4a));
    char lineNo[12];std::snprintf(lineNo,sizeof(lineNo),"%3d",i+1);
    ui.text(font,atlas,lineNo,91,y,19,mute);
    ui.text(font,atlas,codeLines[size_t(i)],162,y,20,active?cyan:fg);
   }
   ui.text(font,atlas,"ON-SCREEN KEYBOARD",1252,223,26,cyan);
   ui.text(font,atlas,keyboardPage==0?"lowercase":keyboardPage==1?"UPPERCASE":"symbols",
           1837,223,20,mute,Align::right);
   ui.line(1250,246,1840,246,2,edge);
   for(int k=0;k<50;k++){
    const float x=1257.f+(k%10)*58.7f;
    const float y=282.f+(k/10)*67.f;
    const bool selectedKey=k==keyboardIndex;
    ui.bordered_rect({x,y,52,57},9,selectedKey?Color::rgb(0x195078):Color::rgb(0x142440),
      selectedKey?2.f:1.f,selectedKey?cyan:edge);
    const char key=pyedit::character(keyboardPage,k);
    const std::string label=key==' '?"SP":key=='\t'?"TAB":std::string(1,key);
    ui.text(font,atlas,label,x+26,y+37,key==' '?18:23,fg,Align::center);
   }
   ui.rounded_rect({1253,653,577,276},15,Color::rgb(0x112039));
   ui.text(font,atlas,"X  Insert key      SQUARE  Backspace",1275,697,21,fg);
   ui.text(font,atlas,"TRIANGLE  New line   L2  4 spaces",1275,740,20,fg);
   ui.text(font,atlas,"L1/R1  Move caret  L3/R3  Line",1275,783,20,fg);
   ui.text(font,atlas,"R2  Keyboard page   OPTIONS  Save",1275,826,20,fg);
   ui.text(font,atlas,"CIRCLE  Back / discard",1275,869,20,mute);
   ui.text(font,atlas,shortName(editorMessage),1280,955,19,cyan);
   ui.rounded_rect({48,996,1824,61},13,Color::rgb(0x101d34));
   ui.text(font,atlas,"D-PAD Keyboard   X Type   SQUARE Delete   TRIANGLE Enter   OPTIONS Save   O Back",
            960,1035,20,mute,Align::center);
   if(page==Page::confirm_discard){
    ui.bordered_rect({578,378,785,280},20,Color::rgb(0x142443),4,Color::rgb(0xffc064));
    ui.text(font,atlas,"DISCARD UNSAVED CHANGES?",971,465,30,fg,Align::center);
    ui.text(font,atlas,"X  Discard     O  Keep editing",971,540,25,cyan,Align::center);
   }
  }
  renderer.begin();renderer.draw(ui);
  renderer.present(0,display.width(),display.height());display.swap();
  hui::sys::hide_splash_screen();hui::sys::sleep_us(16000);
 }
}
