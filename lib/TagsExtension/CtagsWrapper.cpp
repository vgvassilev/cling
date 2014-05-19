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

  static int callback(const char *fpath, const struct stat *sb, int typeflag) {
    if (typeflag == FTW_F && isHeaderFile(fpath))
        FilePaths.push_back(fpath);
    return 0;
  }

  CtagsFileWrapper::CtagsFileWrapper(std::string path,bool recurse) {
    m_tf=new TagFileInternals();
    if(recurse) {
        //TODO: Do this without ftw (and get rid of the global in the process)
        //use llvm::recursive_directory_iterator
      ftw(path.c_str(),callback,maxfd);
      generate(FilePaths,path);
      read();
      FilePaths.clear();
    }
    else{
      llvm::error_code ec;
      llvm::sys::fs::directory_iterator dit(path,ec);
      std::vector<std::string> list;
      while(dit!=decltype(dit)()){// !=end iterator
        auto entry=*dit;
        if(llvm::sys::fs::is_regular_file (entry.path()))
          //llvm::outs()<<entry.path()<<"\n";
          list.push_back(entry.path());
        dit.increment(ec);
      }
      ///auto pair=splitPath(path);
      ///TODO Preprocess the files in list and generate tags for them

    }
  }

  CtagsFileWrapper::CtagsFileWrapper(const std::vector<std::string>& file_list){
    m_tf=new TagFileInternals();
    generate(file_list);
    read();
    FilePaths.clear();
  }

  bool CtagsFileWrapper::operator==(const CtagsFileWrapper& t){
    return m_tagfilename==t.m_tagfilename;
  }

  std::map<std::string,TagFileWrapper::LookupResult>
  CtagsFileWrapper::match(std::string name, bool partialMatch){
    std::map<std::string,LookupResult> map;
    tagEntry entry;
    int options=TAG_OBSERVECASE | (partialMatch?TAG_PARTIALMATCH:TAG_FULLMATCH);
    
    tagResult result = tagsFind(m_tf->tf, &entry, name.c_str(),options);
    
    while(result==TagSuccess){
      LookupResult r;
      r.name=entry.name;
      r.kind=entry.kind;
      map[entry.file]=r;
      result=tagsFindNext(m_tf->tf,&entry);
    }
    
    return map;
  }
  //no more than `arglimit` arguments in a single invocation
  void CtagsFileWrapper::generate(const std::vector<std::string>& paths, std::string dirpath, int argLimit){

    int no_of_args=0;
    std::string concat;
    m_tagpath=generateTagPath();
    m_tagfilename=pathToFileName(dirpath);

    if(!needToGenerate(m_tagpath,m_tagfilename,dirpath)){
      m_generated=false;
      return;
    }
    //std::cout<<"XFile "<<tagpath+tagfilename<<" read.\n";
    auto it=paths.begin(),end=paths.end();
    while(it!=end){
      concat+=(*it+" ");
      no_of_args++;
      if(no_of_args==argLimit){
        //TODO: Convert these to twine
        std::string filename=" -f "+m_tagpath+m_tagfilename+" ";
        std::string lang=" --language-force=c++ ";
        std::string sorted=" --sort=yes ";
        std::string append=" -a ";
        std::string cmd="ctags "+append+lang+filename+sorted+concat;
        std::system(cmd.c_str());

        
        no_of_args=0;
        it++;
      }
    }
    m_generated=true;
  }
  
  void CtagsFileWrapper::read() {
    m_tf->tf=tagsOpen((m_tagpath+m_tagfilename).c_str(),&(m_tf->tfi));
    //std::cout<<"File "<<tagpath+tagfilename<<" read.\n";
    if(m_tf->tfi.status.opened == false)
        m_validfile=false;
    else
        m_validfile=true;
  }
}
