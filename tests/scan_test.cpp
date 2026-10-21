// SPDX-License-Identifier: GPL-3.0-or-later
#include "../native/scripts_scan.hpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
int main(){
 using namespace pyscan;
 for(const char* bad:{"a.txt","../escape.py","h.py/../","bad\\name.py",".hidden.py","bad?.py"})
  assert(!valid_name(bad));
 assert(valid_name("helloworld.py")&&valid_name("UPPER.PY"));
 auto all=roots();assert(all.size()==17);
 assert(all[0].path=="/data/PS5MicroPython/scripts");
 assert(all[1].path=="/mnt/usb0/PS5MicroPython/scripts");
 assert(all[2].path=="/mnt/usb0/scripts");
 char d[]="/tmp/ps5py-scan-XXXXXX";
 assert(mkdtemp(d));
 const std::string dir=d;
 const std::string hello=dir+"/helloworld.py";
 {std::ofstream f(hello);f<<"print('Hi PS5!')\n";}
 {std::ofstream f(dir+"/empty.py");}
 {std::ofstream f(dir+"/big.py");f<<std::string(kMaxScriptBytes+1,'x');}
 {std::ofstream f(dir+"/notpython.txt");f<<"abc";}
 assert(symlink(hello.c_str(),(dir+"/shortcut.py").c_str())==0);
 const auto found=scan_roots({{dir,"DATA"},{dir+"/not-present","USB0"}});
 assert(found.roots.size()==2 && found.roots[0].opened && !found.roots[1].opened);
 assert(found.files.size()==3); // Name discovery is intentionally not blocked by size.
 assert(found.files[0].name=="big.py");
 assert(found.files[1].name=="empty.py");
 assert(found.files[2].name=="helloworld.py");
 assert(found.status().find("SCAN 3 files")!=std::string::npos);
 assert(read_script(hello,kMaxScriptBytes)=="print('Hi PS5!')\n");
 assert(read_script(dir+"/empty.py",kMaxScriptBytes).empty());
 assert(read_script(dir+"/big.py",kMaxScriptBytes).empty());
 assert(unlink(hello.c_str())==0);
 for(const char* file:{"empty.py","big.py","notpython.txt","shortcut.py"})
  assert(unlink((dir+"/"+file).c_str())==0);
 assert(rmdir(dir.c_str())==0);
 return 0;
}
