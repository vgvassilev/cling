#include "cling/CtagsExtension/Wrapper.h"
#include "llvm/ADT/StringRef.h"
#include "readtags.h"
#include <ftw.h>
#include <fnmatch.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <iostream>
namespace cling {
  struct TagFileInternals{
    tagFile* tf;
    tagFileInfo tfi;  
  };
  
  int TagFileWrapper::counter=0;

  TagFileWrapper::TagFileWrapper(const std::vector<std::string>& file_list)
  {
    tf=new TagFileInternals();
    generate(file_list);
    read();
  }
  TagFileWrapper::~TagFileWrapper()
  {
      //Define copy or move constructors and then delete and remove here
  }
  std::string path_to_file_name(std::string path)
  {
      for(auto& c:path)
      {
          if(c=='/')
              c='_';
      }
      return path;
  }

  //Now existense based, will be timestamp based if feasible
  bool need_to_generate(std::string tagpath,std::string filename,std::string dirpath)
  {
      if( access( (tagpath+filename).c_str(), F_OK ) != -1 ) {
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

  static std::vector<std::string> FilePaths; ///TODO:Find a way to do this without globals

  static const int maxfd=512;

  bool isHeaderFile(llvm::StringRef str)
  {
    return str.endswith(".h")
            ||str.endswith(".hpp")
            ||str.find("include")!=llvm::StringRef::npos;
  }

  static int callback(const char *fpath, const struct stat *sb, int typeflag)
  {
    /* if it's a file */
    if (typeflag == FTW_F && isHeaderFile(fpath))
    {
//      int i;

//      /* for each filter, */
//      for (i = 0; i < sizeof(headertypes) / sizeof(headertypes[0]); i++)
//      {
//        /* if the filename matches the filter, */
//        if (fnmatch(headertypes[i], fpath, FNM_CASEFOLD) == 0)
//        {
//          /* do something */
//
//          break;
//        }

 //     }
        FilePaths.push_back(fpath);
    }
    return 0;
  }
  
  TagFileWrapper::TagFileWrapper(std::string path)
  {
    tf=new TagFileInternals();
    ftw(path.c_str(),callback,maxfd);
    generate(FilePaths,path);
    read();
  }
  
  
  
  std::map<std::string,TagFileWrapper::LookupResult> 
  TagFileWrapper::match(std::string name, bool partialMatch)
  {
    std::map<std::string,LookupResult> map;
    tagEntry entry;
    int options=TAG_OBSERVECASE | (partialMatch?TAG_PARTIALMATCH:TAG_FULLMATCH);
    
    tagResult result = tagsFind(tf->tf, &entry, name.c_str(),options);
    
    while(result==TagSuccess)
    {
      LookupResult r;
      r.name=entry.name;
      r.kind=entry.kind;
      map[entry.file]=r;
      result=tagsFindNext(tf->tf,&entry);
    }
    
    return map;
  }
  //no more than `arglimit` arguments in a single invocation
  void TagFileWrapper::generate(const std::vector<std::string>& paths,std::string dirpath,int arglimit)
  {
    auto it=paths.begin();
    int no_of_args=0;
    std::string concat;
    tagpath=generate_tag_path();
    tagfilename=path_to_file_name(dirpath);

    if(!need_to_generate(tagpath,tagfilename,dirpath))
    {
        generated=false;
        return;
    }
    //std::cout<<"XFile "<<tagpath+tagfilename<<" read.\n";

    while(it!=paths.end())
    {
      concat+=(*it+" ");
      no_of_args++;
      if(no_of_args==arglimit)
      {
        std::string filename=" -f "+tagpath+tagfilename+" ";
        std::string sorted=" --sort=yes ";
        std::string append=" -a ";
        std::string cmd="ctags "+append+filename+sorted+concat;
        std::system(cmd.c_str());//FIXME: Possible security hole ? (MM)

        
        no_of_args=0;
        it++;
      }
    }
    generated=true;
  }
  
  void TagFileWrapper::read()
  {
    tf->tf=tagsOpen((tagpath+tagfilename).c_str(),&(tf->tfi));
    //std::cout<<"File "<<tagpath+tagfilename<<" read.\n";
    if(tf->tfi.status.opened == false)
        validfile=false;
    else
        validfile=true;
  }
}
