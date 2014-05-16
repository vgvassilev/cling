#include "FSUtils.h"
#include<string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
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
      return access( path.c_str(), F_OK ) != -1;
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

    std::string generate_tag_path()
    {
      std::string homedir=std::getenv("HOME");
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
