#include "cling/CtagsExtension/Wrapper.h"
#include "readtags.h"
#include <ftw.h>
#include <fnmatch.h>
#include <unistd.h>
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
  static const char *headertypes[] = 
  {
     "*.hh", "*.hpp", "*.h"
  };
  static std::vector<std::string> FilePaths; ///TODO:Find a way to do this without globals
  static const int maxfd=512;

  static int callback(const char *fpath, const struct stat *sb, int typeflag)
  {
    /* if it's a file */
    if (typeflag == FTW_F)
    {
      int i;
      /* for each filter, */
      for (i = 0; i < sizeof(headertypes) / sizeof(headertypes[0]); i++) 
      {
        /* if the filename matches the filter, */
        if (fnmatch(headertypes[i], fpath, FNM_CASEFOLD) == 0) 
        {
          /* do something */
          FilePaths.push_back(fpath);
          break;
        }
      }
    }
    return 0;
  }
  
  TagFileWrapper::TagFileWrapper(std::string path)
  {
    tf=new TagFileInternals();
    ftw(path.c_str(),callback,maxfd);
    generate(FilePaths);
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
  void TagFileWrapper::generate(const std::vector<std::string>& paths,int arglimit)
  {
    auto it=paths.begin();
    int no_of_args=0;
    std::string concat;
    tagfilename="tags"+std::to_string(TagFileWrapper::counter++);
    std::remove(tagfilename.c_str());
    while(it!=paths.end())
    {
      concat+=(*it+" ");
      no_of_args++;
      if(no_of_args==arglimit)
      {
        std::string filename=" -f "+tagfilename+" ";
        std::string sorted=" --sort=yes ";
        std::string append=" -a ";
        std::string cmd="ctags "+append+filename+sorted+concat;
        std::system(cmd.c_str());//FIXME: Possible security hole ? (MM)
        
        no_of_args=0;
        it++;
      }
    }
  }
  
  bool TagFileWrapper::read()
  {
    tf->tf=tagsOpen(tagfilename.c_str(),&(tf->tfi));
    //std::cout<<"File "<<tagfilename<<" read.\n";
    if(tf->tfi.status.opened == false)
    {
        //throw a std::runtime_error ?
//          std::cerr<<"Failed to open tag file.\n";//TODO
        return false;
    }
    return true;
  }
}
