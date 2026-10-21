// SPDX-License-Identifier: GPL-3.0-or-later
// Host mock of real sceKernelOpen/Getdents/Read PS5 libkernel ABI.
// Same 64-KiB aligned dirent layout as zquestclassic-ps5 native_scan_mock_test.cpp.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
static std::map<int,DIR*> handles;
static int id=100;
extern "C" int sceKernelOpen(const char* path,int flags,int) {
 if(flags&O_DIRECTORY){
  if((flags&O_NOFOLLOW)==0)return -EINVAL;
  DIR* d=opendir(path);
  if(!d)return -errno;
  const int fd=id++;handles[fd]=d;return fd;
 }
 return ::open(path,flags);
}
extern "C" int sceKernelGetdents(int fd,char* buf,int cap) {
 auto it=handles.find(fd);
 if(it==handles.end())return -EBADF;
 if(cap!=65536||(uintptr_t(buf)%16384)!=0)return -EINVAL;
 while(dirent* e=readdir(it->second)){
  const size_t length=std::strlen(e->d_name);
  const size_t reclen=(8+length+1+3)&~size_t(3);
  assert(reclen<size_t(cap));
  std::memset(buf,0,reclen);
  const unsigned int ino=unsigned(e->d_ino);
  std::memcpy(buf,&ino,4);
  buf[4]=char(reclen&0xff);
  buf[5]=char((reclen>>8)&0xff);
  buf[6]=char(e->d_type);
  buf[7]=char(length);
  std::memcpy(buf+8,e->d_name,length);
  return int(reclen);
 }
 return 0;
}
extern "C" long sceKernelRead(int fd,void* buf,size_t n) {return ::read(fd,buf,n);}
extern "C" int sceKernelClose(int fd) {
 auto it=handles.find(fd);
 if(it==handles.end())return ::close(fd);
 const int rc=closedir(it->second);
 handles.erase(it);return rc;
}
#define PS5_MICROPYTHON_NATIVE_SCAN 1
#include "../native/scripts_scan.hpp"
int main(){
 using namespace pyscan;
 char d[]="/tmp/ps5py-kernel-XXXXXX";
 assert(mkdtemp(d));
 const std::string dir=d;
 const std::string path=dir+"/helloworld.py";
 {std::ofstream f(path);f<<"print('Kernel loader works!')\n";}
 {std::ofstream f(dir+"/UPPER.PY");f<<"print('upper')\n";}
 {std::ofstream f(dir+"/empty.py");}
 assert(symlink(path.c_str(),(dir+"/shortcut.py").c_str())==0);
 auto scan=scan_roots({{dir,"DATA"},{dir+"/not-a-real-root","USB0"}});
 assert(scan.roots.size()==2);
 assert(scan.roots[0].opened);
 assert(scan.roots[0].found==3);
 assert(!scan.roots[1].opened);
 assert(scan.files.size()==3);
 assert(scan.filenames_seen.size()>=4);
 bool saw_link=false;
 for(const auto& name:scan.filenames_seen)
  if(name==dir+"/shortcut.py")saw_link=true;
 assert(saw_link);
 assert(scan.status().find("SCAN 3 files; roots 1/2")!=std::string::npos);
 assert(read_script(path,kMaxScriptBytes)=="print('Kernel loader works!')\n");
 assert(read_script(dir+"/empty.py",kMaxScriptBytes).empty());
 assert(read_script(dir+"/absent.py",kMaxScriptBytes).empty());
 assert(handles.empty());
 for(const char* name:{"helloworld.py","UPPER.PY","empty.py","shortcut.py"})
  assert(unlink((dir+"/"+name).c_str())==0);
 assert(rmdir(dir.c_str())==0);
 return 0;
}
