// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 MicroPython v0.2.2. Native directory backend adapted from
// homebrew/zquestclassic-ps5/native/scan_directory.hpp (by YAHBALLON)
// and PS5GBCX's libkernel getdents dirshim. NO libc opendir() on PS5.
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>

#if defined(PS5_MICROPYTHON_NATIVE_SCAN)
extern "C" int sceKernelOpen(const char*,int,int);
extern "C" int sceKernelGetdents(int,char*,int);
extern "C" int sceKernelClose(int);
extern "C" long sceKernelRead(int,void*,size_t);
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace pyscan {
constexpr size_t kMaxScriptBytes=64*1024;
constexpr size_t kMaxUserScripts=120;
struct File {std::string name,path,source;};
struct Root {std::string path,label;};
struct RootResult {
 std::string path;
 int open_code=0;
 int read_code=0;
 unsigned entries=0;
 unsigned found=0;
 bool opened=false;
};
struct Report {
 std::vector<File> files;
 std::vector<std::string> filenames_seen; // Include symlinks for safe NEW-name collision checks.
 std::vector<RootResult> roots;
 unsigned candidates=0,skipped=0,visited=0;
 std::string status() const {
  unsigned ok=0;
  for(const auto& r:roots)if(r.opened)++ok;
  std::string msg="SCAN "+std::to_string(files.size())+" files; roots "+
      std::to_string(ok)+"/"+std::to_string(roots.size());
  // Surface /data failure even when missing USB directories are expected.
  if(!roots.empty()&&!roots[0].opened)
   msg+="; DATA error "+std::to_string(roots[0].open_code);
  else if(!roots.empty()&&roots[0].read_code)
   msg+="; DATA read error "+std::to_string(roots[0].read_code);
  return msg;
 }
};
inline bool valid_name(const std::string& n) {
 if(n.size()<4||n.size()>96||n[0]=='.')return false;
 const char a=n[n.size()-3],b=n[n.size()-2],c=n[n.size()-1];
 if(a!='.'||(b!='p'&&b!='P')||(c!='y'&&c!='Y'))return false;
 for(unsigned char v:n)if(!((v>='a'&&v<='z')||(v>='A'&&v<='Z')||
  (v>='0'&&v<='9')||v=='_'||v=='-'||v=='.'))return false;
 return true;
}
inline std::vector<Root> roots() {
 std::vector<Root> result={{"/data/PS5MicroPython/scripts","DATA"}};
 for(int i=0;i<8;i++){
  const auto usb="/mnt/usb"+std::to_string(i);
  result.push_back({usb+"/PS5MicroPython/scripts","USB"+std::to_string(i)});
  result.push_back({usb+"/scripts","USB"+std::to_string(i)});
 }
 return result;
}
inline bool include_entry(unsigned char type) {
 if(type==4||type==10)return false; // DT_DIR/DT_LNK
 // PS5 may return DT_UNKNOWN=0 for ordinary files; include by suffix.
 return type==0||type==8;  // DT_UNKNOWN/DT_REG
}
inline bool already_present(const Report& result,const std::string& path) {
 for(const auto& file:result.files)if(file.path==path)return true;
 return false;
}
inline void record(Report& output,RootResult& root,const Root& origin,
                   const std::string& filename,unsigned char type) {
 ++root.entries;++output.visited;
 if(!valid_name(filename))return;
 ++output.candidates;
 const auto path=origin.path+"/"+filename;
 if(output.filenames_seen.size()<4096)output.filenames_seen.push_back(path);
 if(!include_entry(type)||output.files.size()>=kMaxUserScripts){++output.skipped;return;}
 if(already_present(output,path))return;
 output.files.push_back({filename,path,origin.label});
 ++root.found;
}
#if defined(PS5_MICROPYTHON_NATIVE_SCAN)
// FreeBSD dirent: inode 4, record size 2, type 1, name length 1, name.
// Buffer size and 16KiB alignment match ZQuest PS5 v0.1.3 / PS5GBCX.
inline void scan_directory(Report& report,const Root& origin) {
 RootResult rr;rr.path=origin.path;
 const int fd=sceKernelOpen(origin.path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
 if(fd<0){rr.open_code=fd;report.roots.push_back(rr);return;}
 rr.opened=true;
 alignas(0x4000) unsigned char buf[0x10000]{};
 bool bad=false;
 while(report.visited<4096&&report.files.size()<kMaxUserScripts){
  const int n=sceKernelGetdents(fd,reinterpret_cast<char*>(buf),sizeof(buf));
  if(n<0){rr.read_code=n;bad=true;break;}
  if(n==0)break;
  if(size_t(n)>sizeof(buf)){rr.read_code=-100004;bad=true;break;}
  size_t at=0;
  while(at<size_t(n)&&report.visited<4096){
   if(at+8>size_t(n)){bad=true;rr.read_code=-100003;break;}
   const auto* p=buf+at;
   const size_t len=size_t(p[4])|(size_t(p[5])<<8);
   const size_t chars=p[7];
   if(len<12||(len%4)!=0||at+len>size_t(n)||
      chars==0||chars>255||8+chars>=len){
    rr.read_code=-100001;bad=true;break;
   }
   std::string name(reinterpret_cast<const char*>(p+8),chars);
   if(name.find('\0')!=std::string::npos){rr.read_code=-100002;bad=true;break;}
   at+=len;
   if(name=="."||name=="..")continue;
   record(report,rr,origin,name,p[6]);
  }
  if(bad)break;
 }
 sceKernelClose(fd);
 report.roots.push_back(rr);
}
#else
inline void scan_directory(Report& report,const Root& origin) {
 RootResult rr;rr.path=origin.path;
 DIR* d=opendir(origin.path.c_str());
 if(!d){rr.open_code=errno;report.roots.push_back(rr);return;}
 rr.opened=true;
 errno=0;
 while(dirent* item=readdir(d)) {
  if(report.visited>=4096||report.files.size()>=kMaxUserScripts)break;
  const std::string name=item->d_name;
  if(name=="."||name=="..")continue;
  unsigned char type=item->d_type;
  if(type==DT_UNKNOWN){
   struct stat st{};
   const auto path=origin.path+"/"+name;
   if(lstat(path.c_str(),&st)!=0)continue;
   if(S_ISDIR(st.st_mode))type=4;
   else if(S_ISLNK(st.st_mode))type=10;
   else if(S_ISREG(st.st_mode))type=8;
   else continue;
  }
  record(report,rr,origin,name,type);
 }
 if(errno)rr.read_code=errno;
 closedir(d);
 report.roots.push_back(rr);
}
#endif
inline Report scan_roots(const std::vector<Root>& paths) {
 Report out;
 for(const auto& root:paths)scan_directory(out,root);
 std::sort(out.files.begin(),out.files.end(),
  [](const File& a,const File& b){
   if(a.source!=b.source)return a.source<b.source;
   return a.name<b.name;
  });
 return out;
}
inline std::string read_script(const std::string& path,size_t limit) {
#if defined(PS5_MICROPYTHON_NATIVE_SCAN)
 const int fd=sceKernelOpen(path.c_str(),O_RDONLY|O_NOFOLLOW,0);
 if(fd<0)return {};
 std::string result;
 char buf[4096];
 while(result.size()<=limit){
  size_t want=std::min<size_t>(sizeof(buf),limit+1-result.size());
  if(!want)break;
  const long n=sceKernelRead(fd,buf,want);
  if(n<0||size_t(n)>want){result.clear();break;}
  if(n==0)break;
  result.append(buf,size_t(n));
 }
 sceKernelClose(fd);
#else
 FILE* f=std::fopen(path.c_str(),"rb");
 if(!f)return {};
 std::string result;
 char buf[4096];
 while(result.size()<=limit){
  const size_t want=std::min<size_t>(sizeof(buf),limit+1-result.size());
  if(!want)break;
  const size_t n=std::fread(buf,1,want,f);
  result.append(buf,n);
  if(n<want){if(std::ferror(f))result.clear();break;}
 }
 std::fclose(f);
#endif
 if(result.empty()||result.size()>limit||result.find('\0')!=std::string::npos)return {};
 return result;
}
} // namespace pyscan
