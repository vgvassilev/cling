#include "FSUtils.h"
#include <string>
#include <cstdlib>
#include <algorithm>
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

namespace cling {
  void mkdir(std::string path){
      /*auto ec=*/llvm::sys::fs::create_directory(path);
//        if(!ec.success())
//        {
//            //TODO show error
//        }
  }

  std::pair<std::string,std::string> splitPath(std::string path){
    auto filename=llvm::sys::path::filename(path);
    llvm::SmallString<128> p(path.begin(),path.end());
    llvm::sys::path::remove_filename(p);
    return {p.c_str(),filename};
  }

  std::string pathToFileName(std::string path){
    for(auto& c:path)
      if(c=='/')
        c='_';
    return path;
  }
  bool fileExists(std::string path){
    return llvm::sys::fs::exists(path);
  }

  bool fileIsNewer(std::string path,std::string dir){
    return true;//TODO Timestamp checks go here
  }

  bool needToGenerate(std::string tagpath,std::string filename,std::string dirpath){
    if( fileExists(tagpath+filename)) {
      return false;
    }
    else if (!fileIsNewer(tagpath+filename,dirpath)){
      return false;
    }
    else {
      //std::cout<<"File doesn't exist";
      return true;
    }
  }
  std::string getHome(){
    llvm::SmallString<30> result;
    llvm::sys::path::home_directory(result);
    return result.c_str();
  }

  std::string generateTagPath(){
    std::string homedir=getHome();

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
}//end namespace cling
