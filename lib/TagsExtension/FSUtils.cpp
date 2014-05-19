#include "FSUtils.h"
#include <string>
#include <cstdlib>
#include <algorithm>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

namespace cling {
  void mkdir(std::string path){
      /*auto ec=*/llvm::sys::fs::create_directory(path);
//        if(!ec.success())
//        {
//            //TODO show error
//        }
  }

  std::pair<std::string,std::string> splitPath(std::string path){
    auto it=std::find(path.rbegin(),path.rend(),'/');
    auto idx=std::distance(path.rend(),it);
    idx=-idx;
    return {path.substr(0,idx),path.substr(idx,path.length())};
  }

  std::string path_to_file_name(std::string path){
    for(auto& c:path)
      if(c=='/')
        c='_';
    return path;
  }
  bool file_exists(std::string path){
    return llvm::sys::fs::exists(path);
  }

  bool file_is_newer(std::string path,std::string dir){
    return true;//TODO Timestamp checks go here
  }

  bool need_to_generate(std::string tagpath,std::string filename,std::string dirpath){
    if( file_exists(tagpath+filename)) {
      return false;
    }
    else if (!file_is_newer(tagpath+filename,dirpath)){
      return false;
    }
    else {
      //std::cout<<"File doesn't exist";
      return true;
    }
  }
  std::string get_home(){
    llvm::SmallString<30> result;
    llvm::sys::path::home_directory(result);
    return result.c_str();
  }

  std::string generate_tag_path(){
    std::string homedir=get_home();

    std::string tagdir="/.cling/";
    std::string result=homedir+tagdir;
    mkdir(result.c_str());
    return result;
  }
  bool isHeaderFile(llvm::StringRef str){
    return str.endswith(".h")
            ||str.endswith(".hpp")
            ||str.find("include")!=llvm::StringRef::npos;
  }
}
