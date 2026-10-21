// SPDX-License-Identifier: GPL-3.0-or-later
// Host-testable DualSense text buffer and safe user script file persistence.
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

// On native PS5 use libkernel for writes, just as the script reader does.
// POSIX lstat/open/write/rename is still retained for host regression tests.
#if defined(PS5_MICROPYTHON_NATIVE_STORAGE)
extern "C" int sceKernelOpen(const char*,int,int);
extern "C" long sceKernelWrite(int,const void*,std::size_t);
extern "C" int sceKernelFsync(int);
extern "C" int sceKernelClose(int);
extern "C" int sceKernelRename(const char*,const char*);
extern "C" int sceKernelUnlink(const char*);
#endif

namespace pyedit {
constexpr std::size_t kMaxBytes=64*1024;

inline bool valid_filename(const std::string& name) {
 if(name.size()<4 || name.size()>96 ||
    name.compare(name.size()-3,3,".py")!=0 || name[0]=='.')return false;
 for(unsigned char c:name)
  if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
       (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.'))return false;
 return true;
}

struct Buffer {
 std::string code;
 std::size_t caret=0;
 bool dirty=false;
 void reset(std::string text) {code=std::move(text);caret=code.size();dirty=false;}
 bool insert(const std::string& text) {
  if(text.find('\0')!=std::string::npos || code.size()+text.size()>kMaxBytes)return false;
  code.insert(caret,text);caret+=text.size();dirty=true;return true;
 }
 bool backspace() {
  if(!caret)return false;
  code.erase(--caret,1);dirty=true;return true;
 }
 void left() {if(caret)--caret;}
 void right() {if(caret<code.size())++caret;}
 std::size_t line() const {
  return static_cast<std::size_t>(std::count(code.begin(),code.begin()+std::min(caret,code.size()),'\n'));
 }
 std::size_t col() const {
  const std::size_t here=std::min(caret,code.size());
  const auto prev=here==0?std::string::npos:code.rfind('\n',here-1);
  return here-(prev==std::string::npos?0:prev+1);
 }
 void vertical(int direction) {
  std::size_t pos=std::min(caret,code.size());
  const auto prev=pos==0?std::string::npos:code.rfind('\n',pos-1);
  const std::size_t rowStart=prev==std::string::npos?0:prev+1;
  const std::size_t desired=pos-rowStart;
  if(direction<0) {
   if(rowStart==0)return;
   const std::size_t end=rowStart-1;
   const auto p=end==0?std::string::npos:code.rfind('\n',end-1);
   const std::size_t start=p==std::string::npos?0:p+1;
   caret=start+std::min(desired,end-start);
  }else{
   const auto end=code.find('\n',pos);
   if(end==std::string::npos)return;
   const auto nextEnd=code.find('\n',end+1);
   const std::size_t start=end+1;
   caret=start+std::min(desired,(nextEnd==std::string::npos?code.size():nextEnd)-start);
  }
 }
 std::vector<std::string> display_lines() const {
  std::vector<std::string> lines;
  std::size_t start=0,row=0;
  const std::size_t cursorLine=line(),cursorCol=col();
  while(start<=code.size() && lines.size()<2048) {
   auto end=code.find('\n',start);
   if(end==std::string::npos)end=code.size();
   std::string content=code.substr(start,end-start);
   if(caret>=start&&caret<=end)content.insert(caret-start,"|");
   const std::size_t left=(row==cursorLine&&cursorCol>67)?cursorCol-67:0;
   if(content.size()>76)content=content.substr(left,76);
   lines.push_back(std::move(content));
   if(end==code.size())break;
   start=end+1;++row;
  }
  return lines;
 }
};

inline std::string unique_filename(const std::string& dir,const std::string& prefix="script"){
 for(int i=1;i<1000;i++){
  char suffix[16];std::snprintf(suffix,sizeof(suffix),"_%03d.py",i);
  std::string name=prefix+suffix;
  if(!valid_filename(name))return {};
  struct stat st{};
  if(lstat((dir+"/"+name).c_str(),&st)!=0&&errno==ENOENT)return name;
 }
 return {};
}

enum class SaveResult {ok,invalid_name,invalid_code,unsafe_target,temp_failed,write_failed,rename_failed};
inline const char* save_message(SaveResult v) {
 switch(v){
 case SaveResult::ok:return "Saved successfully";
 case SaveResult::invalid_name:return "Invalid script filename";
 case SaveResult::invalid_code:return "Script too large / invalid NUL byte";
 case SaveResult::unsafe_target:return "Unsafe existing file";
 case SaveResult::temp_failed:return "Cannot create temp file";
 case SaveResult::write_failed:return "Could not write full script";
 case SaveResult::rename_failed:return "Cannot rename temp script";
 }
 return "Unknown error";
}

// Writes a temp file in the same directory, then atomically renames it.
// On failures the destination stays untouched. User scripts only.
inline SaveResult save_script(const std::string& dir,const std::string& name,
                              const std::string& code,int* raw_error=nullptr) {
 if(raw_error)*raw_error=0;
 if(!valid_filename(name))return SaveResult::invalid_name;
 if(code.empty()||code.size()>kMaxBytes||code.find('\0')!=std::string::npos)
  return SaveResult::invalid_code;
 const std::string dest=dir+"/"+name;
#if defined(PS5_MICROPYTHON_NATIVE_STORAGE)
 // The PS5 big-app libc lstat() can reject ordinary, already-readable files.
 // Do not use it as a file-type or existence gate. Existing files are edited
 // only after being selected from the getdents scanner (which rejects symlinks).
 // The secure temporary file is always in the same vetted script directory.
 // O_EXCL ensures an old/stale temporary file is NEVER truncated.
 for(int attempt=0;attempt<16;attempt++){
  const std::string tmp=dir+"/."+name+".saving."+std::to_string(attempt);
  const int fd=sceKernelOpen(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
  if(fd<0){
   if(raw_error)*raw_error=fd;
   continue;
  }
  size_t written=0;
  long write_error=0;
  while(written<code.size()){
   const long n=sceKernelWrite(fd,code.data()+written,code.size()-written);
   if(n<=0||size_t(n)>code.size()-written){
    write_error=n;break;
   }
   written+=size_t(n);
  }
  const int fsync_error=written==code.size()?sceKernelFsync(fd):0;
  const int close_error=sceKernelClose(fd);
  if(written!=code.size()||fsync_error<0||close_error<0){
   if(raw_error)*raw_error=int(write_error<0?write_error:
                                fsync_error<0?fsync_error:
                                close_error<0?close_error:-1);
   sceKernelUnlink(tmp.c_str());
   return SaveResult::write_failed;
  }
  const int renamed=sceKernelRename(tmp.c_str(),dest.c_str());
  if(renamed!=0){
   if(raw_error)*raw_error=renamed;
   sceKernelUnlink(tmp.c_str());
   return SaveResult::rename_failed;
  }
  return SaveResult::ok;
 }
 return SaveResult::temp_failed;
#else
 struct stat st{};
 if(lstat(dest.c_str(),&st)==0){
  if(!S_ISREG(st.st_mode))return SaveResult::unsafe_target;
 }else if(errno!=ENOENT)return SaveResult::unsafe_target;
 std::string tmp;int fd=-1;
 for(int i=0;i<16;i++){
  tmp=dest+".tmp."+std::to_string(i);
  fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
  if(fd>=0)break;
  if(errno!=EEXIST){
   if(raw_error)*raw_error=errno;
   return SaveResult::temp_failed;
  }
 }
 if(fd<0)return SaveResult::temp_failed;
 size_t written=0;
 while(written<code.size()){
  const auto n=write(fd,code.data()+written,code.size()-written);
  if(n<=0)break;
  written+=size_t(n);
 }
 const int closed=close(fd);
 if(written!=code.size()||closed!=0){
  if(raw_error)*raw_error=errno;
  unlink(tmp.c_str());return SaveResult::write_failed;
 }
 if(rename(tmp.c_str(),dest.c_str())!=0){
  if(raw_error)*raw_error=errno;
  unlink(tmp.c_str());return SaveResult::rename_failed;
 }
 return SaveResult::ok;
#endif
}

// On-screen keyboard: 10 columns, 5 rows, 3 pages (lower/upper/symbols).
inline const char* keys(int page,int row){
 static const char* layout[3][5]={
  {"abcdefghij","klmnopqrst","uvwxyz0123","456789_=()","+-*/.:,'\" "},
  {"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ0123","456789_=()","+-*/.:,'\" "},
  {"!@#$%^&*()","[]{}<>?/\\|","~;:.,+-=* ","0123456789","_\"' \t#%?\\+"}
 };
 return layout[(page%3+3)%3][std::clamp(row,0,4)];
}
inline char character(int page,int index){return keys(page,index/10)[index%10];}
} // namespace pyedit
