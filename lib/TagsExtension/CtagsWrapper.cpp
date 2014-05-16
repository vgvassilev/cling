#include "cling/TagsExtension/CtagsWrapper.h"
#include "llvm/ADT/StringRef.h"
#include "readtags.h"
#include "FSUtils.h"
#include <ftw.h>
#include <iostream>
namespace cling {

  struct TagFileInternals{
    tagFile* tf;
    tagFileInfo tfi;  
  };


  CtagsFileWrapper::CtagsFileWrapper(const std::vector<std::string>& file_list)
  {
    tf=new TagFileInternals();
    generate(file_list);
    read();
  }
//  CtagsFileWrapper::~CtagsFileWrapper()
//  {
//      //Define copy or move constructors and then delete and remove here
//  }

  bool CtagsFileWrapper::operator==(const CtagsFileWrapper& t)
  {
      return tagfilename==t.tagfilename;
  }



  static std::vector<std::string> FilePaths; ///TODO:Find a way to do this without globals

  static const int maxfd=512;



  static int callback(const char *fpath, const struct stat *sb, int typeflag)
  {
    if (typeflag == FTW_F && isHeaderFile(fpath))
        FilePaths.push_back(fpath);
    return 0;
  }
  
  CtagsFileWrapper::CtagsFileWrapper(std::string path)
  {
    tf=new TagFileInternals();
    ftw(path.c_str(),callback,maxfd);
    generate(FilePaths,path);
    read();
  }
  
  
  
  std::map<std::string,TagFileWrapper::LookupResult>
  CtagsFileWrapper::match(std::string name, bool partialMatch)
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
  void CtagsFileWrapper::generate(const std::vector<std::string>& paths,std::string dirpath,int arglimit)
  {

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
    auto it=paths.begin(),end=paths.end();
    while(it!=end)
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
  
  void CtagsFileWrapper::read()
  {
    tf->tf=tagsOpen((tagpath+tagfilename).c_str(),&(tf->tfi));
    //std::cout<<"File "<<tagpath+tagfilename<<" read.\n";
    if(tf->tfi.status.opened == false)
        validfile=false;
    else
        validfile=true;
  }
}
