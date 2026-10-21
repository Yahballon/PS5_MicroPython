// SPDX-License-Identifier: GPL-3.0-or-later
#include "../native/editor.hpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
int main() {
 using namespace pyedit;
 assert(valid_filename("my_test.py"));
 for(const char* s:{"../escape.py","/tmp/evil.py",".hidden.py","evil\\file.py",
                    "bad|name.py","no.py.txt","a.py/hi"})assert(!valid_filename(s));
 Buffer b;
 b.reset("print('hi')\nfoo\n");
 assert(b.line()==2&&b.col()==0);
 b.left();assert(b.line()==1&&b.col()==3);
 b.vertical(-1);assert(b.line()==0&&b.col()==3);
 b.vertical(1);assert(b.line()==1&&b.col()==3);
 b.right();assert(b.line()==2);
 assert(b.insert("bar"));assert(b.code=="print('hi')\nfoo\nbar");
 assert(b.backspace());assert(b.code=="print('hi')\nfoo\nba");
 assert(b.dirty&&!b.display_lines().empty());
 Buffer max;max.reset(std::string(kMaxBytes,'x'));assert(!max.insert("y"));
 max.reset("a");assert(!max.insert(std::string("x\0y",3)));
 for(int page=0;page<3;page++)for(int idx=0;idx<50;idx++)
  assert(character(page,idx)!='\0');
 char dir[]="/tmp/ps5py-editor-XXXXXX";
 assert(mkdtemp(dir)!=nullptr);
 assert(unique_filename(dir)=="script_001.py");
 assert(save_script(dir,"script_001.py",b.code)==SaveResult::ok);
 std::ifstream in(std::string(dir)+"/script_001.py");
 std::string saved((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
 assert(saved==b.code);in.close();
 assert(unique_filename(dir)=="script_002.py");
 assert(save_script(dir,"script_001.py","print(42)\n")==SaveResult::ok);
 assert(save_script(dir,"../bad.py","x")==SaveResult::invalid_name);
 assert(save_script(dir,"script_001.py","")==SaveResult::invalid_code);
 assert(save_script(dir,"script_001.py",std::string(kMaxBytes+1,'x'))==SaveResult::invalid_code);
 assert(symlink("/tmp",(std::string(dir)+"/danger.py").c_str())==0);
 assert(save_script(dir,"danger.py","x")==SaveResult::unsafe_target);
 assert(unlink((std::string(dir)+"/danger.py").c_str())==0);
 assert(unlink((std::string(dir)+"/script_001.py").c_str())==0);
 assert(rmdir(dir)==0);
 return 0;
}
