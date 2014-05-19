#include "cling/TagsExtension/CtagsWrapper.h"
#include "llvm/ADT/StringRef.h"
#include "readtags.h"
#include "FSUtils.h"
#include <ftw.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>

namespace cling {

  struct TagFileInternals{
    tagFile* tf;
    tagFileInfo tfi;  
  };
  static std::vector<std::string> FilePaths; ///TODO:Find a way to do this without globals

  static const int maxfd=512;

  static int callback(const char *fpath, const struct stat *sb, int typeflag)
  {
    if (typeflag == FTW_F && isHeaderFile(fpath))
        FilePaths.push_back(fpath);
    return 0;
  }

  CtagsFileWrapper::CtagsFileWrapper(std::string path,bool recurse)
  {
    tf=new TagFileInternals();
    if(recurse)
    {
        //TODO: Do this without ftw (and get rid of the global in the process)
        //use llvm::recursive_directory_iterator
      ftw(path.c_str(),callback,maxfd);
      generate(FilePaths,path);
      read();
    }
    else
    {
      llvm::error_code ec;
      llvm::sys::fs::directory_iterator dit(path,ec);
      std::vector<std::string> list;
      while(dit!=decltype(dit)())// !=end iterator
      {
        auto entry=*dit;
        if(llvm::sys::fs::is_regular_file (entry.path()))
          //llvm::outs()<<entry.path()<<"\n";
          list.push_back(entry.path());
        dit.increment(ec);
      }
      //TODO: Run "clang -x c++ -E" over all the files in list and save them to .cling
    }
  }

  CtagsFileWrapper::CtagsFileWrapper(const std::vector<std::string>& file_list)
  {
    tf=new TagFileInternals();
    generate(file_list);
    read();
  }

  bool CtagsFileWrapper::operator==(const CtagsFileWrapper& t)
  {
      return tagfilename==t.tagfilename;
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
        //TODO: Convert these to twine
        std::string filename=" -f "+tagpath+tagfilename+" ";
        std::string lang=" --language-force=c++ ";
        std::string sorted=" --sort=yes ";
        std::string append=" -a ";
        std::string cmd="ctags "+append+lang+filename+sorted+concat;
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
