#ifndef CLING_AUTOLOADING_VISITOR_H
#define CLING_AUTOLOADING_VISITOR_H
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Basic/Module.h"
#include "llvm/Support/raw_ostream.h"
namespace cling {
//  class NamespacePrinterRAII {
//  public:
//    NamespacePrinterRAII(std::string name) {
//      llvm::outs()<< "namespace " <<name<<" {\n";
//    }
//    ~NamespacePrinterRAII() {
//      llvm::outs()<<"\n}\n";
//    }
//  };

//  class AutoloadingVisitor
//    :public clang::RecursiveASTVisitor<AutoloadingVisitor> {
//  public:
//    AutoloadingVisitor(llvm::StringRef InFile,llvm::StringRef OutFile)
//        :m_InFile(InFile),m_OutFile(OutFile){}
//    bool VisitCXXRecordDecl(clang::CXXRecordDecl* Declaration);
//    bool VisitFunctionDecl(clang::FunctionDecl* Declaration);
//    bool VisitClassTemplateDecl(clang::ClassTemplateDecl* Declaration);
//    bool VisitNamespaceDecl(clang::NamespaceDecl* Declaration);
////    bool VisitTranslationUnitDecl(clang::TranslationUnitDecl* Declaration);
//  private:
//    llvm::StringRef m_InFile;
//    llvm::StringRef m_OutFile;
//  };
}//end namespace cling
using namespace clang;
namespace cling {

class DeclPrinter : public DeclVisitor<DeclPrinter> {
  raw_ostream &Out;
  PrintingPolicy Policy;
  unsigned Indentation;
  bool PrintInstantiation;

  raw_ostream& Indent() { return Indent(Indentation); }
  raw_ostream& Indent(unsigned Indentation);
  void ProcessDeclGroup(SmallVectorImpl<Decl*>& Decls);

  void Print(AccessSpecifier AS);

public:
  DeclPrinter(raw_ostream &Out, const PrintingPolicy &Policy,
              unsigned Indentation = 0, bool PrintInstantiation = false)
    : Out(Out), Policy(Policy), Indentation(Indentation),
      PrintInstantiation(PrintInstantiation) { }

  void VisitDeclContext(DeclContext *DC, bool Indent = true);

  void VisitTranslationUnitDecl(TranslationUnitDecl *D);
  void VisitTypedefDecl(TypedefDecl *D);
  void VisitTypeAliasDecl(TypeAliasDecl *D);
  void VisitEnumDecl(EnumDecl *D);
  void VisitRecordDecl(RecordDecl *D);
  void VisitEnumConstantDecl(EnumConstantDecl *D);
  void VisitEmptyDecl(EmptyDecl *D);
  void VisitFunctionDecl(FunctionDecl *D);
  void VisitFriendDecl(FriendDecl *D);
  void VisitFieldDecl(FieldDecl *D);
  void VisitVarDecl(VarDecl *D);
  void VisitLabelDecl(LabelDecl *D);
  void VisitParmVarDecl(ParmVarDecl *D);
  void VisitFileScopeAsmDecl(FileScopeAsmDecl *D);
  void VisitImportDecl(ImportDecl *D);
  void VisitStaticAssertDecl(StaticAssertDecl *D);
  void VisitNamespaceDecl(NamespaceDecl *D);
  void VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
  void VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
  void VisitCXXRecordDecl(CXXRecordDecl *D);
  void VisitLinkageSpecDecl(LinkageSpecDecl *D);
  void VisitTemplateDecl(const TemplateDecl *D);
  void VisitFunctionTemplateDecl(FunctionTemplateDecl *D);
  void VisitClassTemplateDecl(ClassTemplateDecl *D);
//  void VisitObjCMethodDecl(ObjCMethodDecl *D);
//  void VisitObjCImplementationDecl(ObjCImplementationDecl *D);
//  void VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
//  void VisitObjCProtocolDecl(ObjCProtocolDecl *D);
//  void VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
//  void VisitObjCCategoryDecl(ObjCCategoryDecl *D);
//  void VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D);
//  void VisitObjCPropertyDecl(ObjCPropertyDecl *D);
//  void VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
//  void VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
//  void VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
//  void VisitUsingDecl(UsingDecl *D);
//  void VisitUsingShadowDecl(UsingShadowDecl *D);
//  void VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D);

  void PrintTemplateParameters(const TemplateParameterList *Params,
                               const TemplateArgumentList *Args = 0);
  void prettyPrintAttributes(Decl *D);
};
}
#endif
