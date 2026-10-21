// SPDX-License-Identifier: GPL-3.0-or-later
// Native PS5 storage host mocks: read-only libc metadata is deliberately broken
// while sceKernelOpen/Write/Close/Fsync/Rename/Unlink operate normally.
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static bool deny_open=false,deny_write=false,deny_rename=false;
static int lstat_calls=0;
extern "C" int lstat(const char*,struct stat*) {
 ++lstat_calls;
 errno=EACCES;
 return -1;
}
extern "C" int sceKernelOpen(const char* path,int flags,int mode) {
 if(deny_open)return -EACCES;
 assert((flags&O_EXCL)!=0 && (flags&O_CREAT)!=0);
 assert((flags&O_NOFOLLOW)!=0);
 const int fd=::open(path,flags,mode);
 return fd<0?-errno:fd;
}
extern "C" long sceKernelWrite(int fd,const void* b,std::size_t n) {
 if(deny_write)return -EIO;
 return ::write(fd,b,n);
}
extern "C" int sceKernelFsync(int fd) {
 const int n=::fsync(fd);return n<0?-errno:n;
}
extern "C" int sceKernelClose(int fd) {
 const int n=::close(fd);return n<0?-errno:n;
}
extern "C" int sceKernelRename(const char* from,const char* to) {
 if(deny_rename)return -EROFS;
 const int n=::rename(from,to);
 return n<0?-errno:n;
}
extern "C" int sceKernelUnlink(const char* path) {
 const int n=::unlink(path);return n<0?-errno:n;
}
#define PS5_MICROPYTHON_NATIVE_STORAGE 1
#include "../native/editor.hpp"

static std::string read_file(const std::string& filename){
 std::ifstream file(filename);
 return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}
int main(){
 using namespace pyedit;
 char d[]="/tmp/ps5mp-native-save-XXXXXX";
 assert(mkdtemp(d));
 const std::string dir=d,dest=dir+"/helloworld.py";
 int code=0;
 // The metadata path is unavailable but native saves succeed.
 assert(save_script(dir,"helloworld.py","print('first')\n",&code)==SaveResult::ok);
 assert(read_file(dest)=="print('first')\n");
 assert(save_script(dir,"helloworld.py","print('updated')\n",&code)==SaveResult::ok);
 assert(read_file(dest)=="print('updated')\n");
 assert(lstat_calls==0);
 assert(code==0);
 // A failing rename must leave the original file intact.
 deny_rename=true;
 assert(save_script(dir,"helloworld.py","print('bad')\n",&code)==SaveResult::rename_failed);
 assert(code==-EROFS);
 deny_rename=false;
 assert(read_file(dest)=="print('updated')\n");
 // A failed write must likewise preserve the original.
 deny_write=true;
 assert(save_script(dir,"helloworld.py","print('bad')\n",&code)==SaveResult::write_failed);
 assert(code==-EIO);
 deny_write=false;
 assert(read_file(dest)=="print('updated')\n");
 deny_open=true;
 assert(save_script(dir,"helloworld.py","print('bad')\n",&code)==SaveResult::temp_failed);
 assert(code==-EACCES);
 deny_open=false;
 assert(read_file(dest)=="print('updated')\n");
 assert(save_script(dir,"../escape.py","xxx",&code)==SaveResult::invalid_name);
 assert(save_script(dir,"helloworld.py","",&code)==SaveResult::invalid_code);
 assert(read_file(dest)=="print('updated')\n");
 // Verify all temporary files were removed.
 for(int i=0;i<16;i++)
  assert(access((dir+"/.helloworld.py.saving."+std::to_string(i)).c_str(),F_OK)!=0);
 assert(unlink(dest.c_str())==0);
 assert(rmdir(dir.c_str())==0);
 return 0;
}
