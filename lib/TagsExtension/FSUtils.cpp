#include "FSUtils.h"
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

namespace cling {
    std::string path_to_file_name(std::string path)
    {
        for(auto& c:path)
            if(c=='/')
                c='_';
        return path;
    }
    bool file_exists(std::string path)
    {
      return llvm::sys::fs::exists(path);
    }

    bool file_is_newer(std::string path,std::string dir)
    {
        return true;//TODO Timestamp checks go here
    }

    bool need_to_generate(std::string tagpath,std::string filename,std::string dirpath)
    {
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
    std::string get_home()
    {
        llvm::SmallString<30> result;
        llvm::sys::path::home_directory(result);
        return result.c_str();
    }

    std::string generate_tag_path()
    {
      std::string homedir=get_home();

      std::string tagdir="/.cling/";
      std::string result=homedir+tagdir;
      mkdir(result.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      return result;
    }
    bool isHeaderFile(llvm::StringRef str)
    {
      return str.endswith(".h")
              ||str.endswith(".hpp")
              ||str.find("include")!=llvm::StringRef::npos;
    }
}
