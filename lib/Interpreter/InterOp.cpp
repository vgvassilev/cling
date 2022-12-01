//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/InterOp.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/DynamicLibraryManager.h"
#include "cling/Utils/AST.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Lookup.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_os_ostream.h"

#include <dlfcn.h>
#include <sstream>
#include <string>

namespace cling {
namespace InterOp {
  using namespace clang;
  using namespace llvm;

  bool IsNamespace(TCppScope_t scope) {
    Decl *D = static_cast<Decl*>(scope);
    return isa<NamespaceDecl>(D);
  }
  // See TClingClassInfo::IsLoaded
  bool IsComplete(TCppScope_t scope) {
    if (!scope)
      return false;
    Decl *D = static_cast<Decl*>(scope);
    if (auto *CXXRD = dyn_cast<CXXRecordDecl>(D))
      return CXXRD->hasDefinition();
    else if (auto *TD = dyn_cast<TagDecl>(D))
      return TD->getDefinition();

    // Everything else is considered complete.
    return true;
  }

  size_t SizeOf(TCppScope_t scope) {
    assert (scope);
    if (!IsComplete(scope))
      return 0;

    if (auto *RD = dyn_cast<RecordDecl>(static_cast<Decl*>(scope))) {
      ASTContext &Context = RD->getASTContext();
      const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
      return Layout.getSize().getQuantity();
    }

    return 0;
  }

  bool IsBuiltin(TCppType_t type) {
    QualType Ty = QualType::getFromOpaquePtr(type);
    if (Ty->isBuiltinType() || Ty->isAnyComplexType())
      return true;
    // FIXME: Figure out how to avoid the string comparison.
    return llvm::StringRef(Ty.getAsString()).contains("complex");
  }

  bool IsTemplate(TCppScope_t handle) {
    auto *D = (clang::Decl *)handle;
    return llvm::isa_and_nonnull<clang::TemplateDecl>(D);
  }

  bool IsAbstract(TCppType_t klass) {
    auto *D = (clang::Decl *)klass;
    if (auto *CXXRD = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(D))
      return CXXRD->isAbstract();

    return false;
  }

  bool IsEnum(TCppScope_t handle) {
    auto *D = (clang::Decl *)handle;
    return llvm::isa_and_nonnull<clang::EnumDecl>(D)
        || llvm::isa_and_nonnull<clang::EnumConstantDecl>(D);
  }

  bool IsEnumType(TCppType_t type) {
    QualType QT = QualType::getFromOpaquePtr(type);
    return QT->isEnumeralType();
  }

  TCppType_t GetEnumIntegerType(TCppScope_t handle) {
    auto *D = (clang::Decl *)handle;
    if (auto *ED = llvm::dyn_cast_or_null<clang::EnumDecl>(D)) {
      return ED->getIntegerType().getAsOpaquePtr();
    }

    return 0;
  }

  size_t GetSizeOfType(TCppSema_t sema, TCppType_t type) {
    auto S = (clang::Sema *) sema;
    QualType QT = QualType::getFromOpaquePtr(type);
    auto TI = S->getASTContext().getTypeInfo(QT);
    size_t TypeSize = TI.Width;
    return TypeSize/8;
  }

  bool IsVariable(TCppScope_t scope) {
    auto *D = (clang::Decl *)scope;
    return llvm::isa_and_nonnull<clang::VarDecl>(D);
  }

  std::string GetName(TCppType_t klass) {
    auto *D = (clang::NamedDecl *) klass;

    if (llvm::isa_and_nonnull<TranslationUnitDecl>(D)) {
      return "";
    }

    if (auto *ND = llvm::dyn_cast_or_null<NamedDecl>(D)) {
      return ND->getNameAsString();
    }

    return "<unnamed>";
  }

  std::string GetCompleteName(TCppType_t klass)
  {
    auto *D = (Decl *) klass;
    if (auto *ND = llvm::dyn_cast_or_null<NamedDecl>(D)) {
      return ND->getQualifiedNameAsString();
    }

    if (llvm::isa_and_nonnull<TranslationUnitDecl>(D)) {
      return "";
    }

    return "<unnamed>";
  }

  std::vector<TCppScope_t> GetUsingNamespaces(TCppScope_t scope) {
    auto *D = (clang::Decl *) scope;

    if (auto *DC = llvm::dyn_cast_or_null<clang::DeclContext>(D)) {
      std::vector<TCppScope_t> namespaces;
      for (auto UD : DC->using_directives()) {
        namespaces.push_back((TCppScope_t) UD->getNominatedNamespace());
      }
      return namespaces;
    }

    return {};
  }

  TCppScope_t GetGlobalScope(TCppSema_t sema)
  {
    auto *S = (Sema *) sema;
    return S->getASTContext().getTranslationUnitDecl();
  }

  TCppScope_t GetScope(TCppSema_t sema, const std::string &name, TCppScope_t parent)
  {
    if (name == "")
        return GetGlobalScope(sema);

    DeclContext *Within = 0;
    if (parent) {
      auto *D = (Decl *)parent;
      Within = llvm::dyn_cast<DeclContext>(D);
    }

    auto *S = (Sema *) sema;
    auto *ND = cling::utils::Lookup::Named(S, name, Within);

    if (!(ND == (NamedDecl *) -1) &&
            (llvm::isa_and_nonnull<NamespaceDecl>(ND) ||
             llvm::isa_and_nonnull<RecordDecl>(ND)    ||
             llvm::isa_and_nonnull<ClassTemplateDecl>(ND)))
      return (TCppScope_t)(ND->getCanonicalDecl());

    return 0;
  }

  TCppScope_t GetScopeFromCompleteName(TCppSema_t sema, const std::string &name)
  {
    std::string delim = "::";
    size_t start = 0;
    size_t end = name.find(delim);
    TCppScope_t curr_scope = 0;
    auto *S = (Sema *) sema;
    while (end != std::string::npos)
    {
      curr_scope = GetScope(S, name.substr(start, end - start), curr_scope);
      start = end + delim.length();
      end = name.find(delim, start);
    }
    return GetScope(S, name.substr(start, end), curr_scope);
  }

  TCppScope_t GetNamed(TCppSema_t sema, const std::string &name, TCppScope_t parent)
  {
    clang::DeclContext *Within = 0;
    if (parent) {
      auto *D = (clang::Decl *)parent;
      Within = llvm::dyn_cast<clang::DeclContext>(D);
    }

    auto *S = (Sema *) sema;
    auto *ND = cling::utils::Lookup::Named(S, name, Within);
    if (ND && ND != (clang::NamedDecl*) -1) {
      return (TCppScope_t)(ND->getCanonicalDecl());
    }

    return 0;
  }

  TCppScope_t GetParentScope(TCppScope_t scope)
  {
    auto *D = (clang::Decl *) scope;

    if (llvm::isa_and_nonnull<TranslationUnitDecl>(D)) {
      return 0;
    }
    auto *ParentDC = D->getDeclContext();

    if (!ParentDC)
      return 0;

    return (TCppScope_t) clang::Decl::castFromDeclContext(
            ParentDC)->getCanonicalDecl();
  }

  namespace {
    CXXRecordDecl *GetScopeFromType(QualType QT) {
      if (auto* Type = QT.getTypePtrOrNull()) {
        Type = Type->getPointeeOrArrayElementType();
        Type = Type->getUnqualifiedDesugaredType();
        return Type->getAsCXXRecordDecl();
      }
      return 0;
    }
  }

  TCppScope_t GetScopeFromType(TCppType_t type)
  {
    QualType QT = QualType::getFromOpaquePtr(type);
    return (TCppScope_t) GetScopeFromType(QT);
  }

  TCppIndex_t GetNumBases(TCppScope_t klass)
  {
    auto *D = (Decl *) klass;

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D))
      return CXXRD->getNumBases();

    return 0;
  }

  TCppScope_t GetBaseClass(TCppScope_t klass, TCppIndex_t ibase)
  {
    auto *D = (Decl *) klass;
    auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D);
    if (!CXXRD || CXXRD->getNumBases() <= ibase) return 0;

    auto type = (CXXRD->bases_begin() + ibase)->getType();
    if (auto RT = llvm::dyn_cast<RecordType>(type)) {
      return (TCppScope_t) RT->getDecl()->getCanonicalDecl();
    } else if (auto TST = llvm::dyn_cast<clang::TemplateSpecializationType>(type)) {
      return (TCppScope_t) TST->getTemplateName()
          .getAsTemplateDecl()->getCanonicalDecl();
    }

    return 0;
  }

  bool IsSubclass(TCppScope_t derived, TCppScope_t base)
  {
    if (derived == base)
      return true;

    auto *derived_D = (clang::Decl *) derived;
    auto *base_D = (clang::Decl *) base;

    if (!derived_D ||
        !base_D    ||
        llvm::isa<TranslationUnitDecl>(derived_D) ||
        llvm::isa<TranslationUnitDecl>(base_D))
        return false;

    if (auto derived_CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(derived_D))
      if (auto base_CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(base_D))
        return derived_CXXRD->isDerivedFrom(base_CXXRD);
    return false;
  }

  intptr_t GetBaseClassOffset(TCppSema_t sema, TCppScope_t derived, TCppScope_t base) {
    if (base == derived)
      return 0;

    auto *DD = (Decl *) derived;
    auto *BD = (Decl *) base;
    auto *S = (Sema *) sema;

    if (auto *DCXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(DD)) {
      if (auto *BCXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(BD)) {
        auto &Cxt = S->getASTContext();
        return (intptr_t) Cxt.getASTRecordLayout(DCXXRD)
            .getBaseClassOffset(BCXXRD).getQuantity();
      }
    }

    return (intptr_t) -1;
  }

  std::vector<TCppFunction_t> GetClassMethods(TCppScope_t klass)
  {
    auto *D = (clang::Decl *) klass;

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D)) {
      std::vector<TCppFunction_t> methods;
      for (auto it = CXXRD->method_begin(), end = CXXRD->method_end();
              it != end; it++) {
        methods.push_back((TCppFunction_t) *it);
      }
      return methods;
    }
    return {};
  }

  std::vector<TCppFunction_t> GetFunctionsUsingName(
        TCppSema_t sema, TCppScope_t scope, const std::string& name)
  {
    auto *D = (Decl *) scope;
    std::vector<TCppFunction_t> funcs;
    llvm::StringRef Name(name);
    auto *S = (Sema *) sema;
    DeclarationName DName = &S->Context.Idents.get(name);
    clang::LookupResult R(*S,
                          DName,
                          SourceLocation(),
                          Sema::LookupOrdinaryName,
                          Sema::ForVisibleRedeclaration);

    cling::utils::Lookup::Named(S, R, Decl::castToDeclContext(D));

    if (R.empty())
      return funcs;

    R.resolveKind();

    for (LookupResult::iterator Res = R.begin(), ResEnd = R.end();
         Res != ResEnd;
         ++Res) {
      if (llvm::isa<FunctionDecl>(*Res)) {
        funcs.push_back((TCppFunction_t) *Res);
      }
    }

    return funcs;
  }

  TCppType_t GetFunctionReturnType(TCppFunction_t func)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<clang::FunctionDecl>(D)) {
        return FD->getReturnType().getAsOpaquePtr();
    }

    return 0;
  }

  TCppIndex_t GetFunctionNumArgs(TCppFunction_t func)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      return FD->getNumParams();
    }
    return 0;
  }

  TCppIndex_t GetFunctionRequiredArgs(TCppFunction_t func)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl> (D)) {
      return FD->getMinRequiredArguments();
    }
    return 0;
  }

  TCppType_t GetFunctionArgType(TCppFunction_t func, TCppIndex_t iarg)
  {
    auto *D = (clang::Decl *) func;

    if (auto *FD = llvm::dyn_cast_or_null<clang::FunctionDecl>(D)) {
        if (iarg < FD->getNumParams()) {
            auto *PVD = FD->getParamDecl(iarg);
            return PVD->getOriginalType().getAsOpaquePtr();
        }
    }

    return 0;
  }

  void get_function_params(std::stringstream &ss, FunctionDecl *FD,
          bool show_formal_args = false, TCppIndex_t max_args = -1)
  {
    ss << "(";
    if (max_args == (size_t) -1) max_args = FD->param_size();
    max_args = std::min(max_args, FD->param_size());
    size_t i = 0;
    auto &Ctxt = FD->getASTContext();
    for (auto PI = FD->param_begin(), end = FD->param_end();
              PI != end; PI++) {
      if (i >= max_args)
          break;
      ss << (*PI)->getType().getAsString();
      if (show_formal_args) {
        ss << " " << (*PI)->getNameAsString();
        if ((*PI)->hasDefaultArg()) {
          ss << " = ";
          raw_os_ostream def_arg_os(ss);
          (*PI)->getDefaultArg()->printPretty(def_arg_os, nullptr,
                  PrintingPolicy(Ctxt.getLangOpts()));
        }
      }
      if (++i < max_args) {
        ss << ", ";
      }
    }
    ss << ")";
  }

  std::string GetFunctionSignature(
          TCppFunction_t func, bool show_formal_args, TCppIndex_t max_args)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      std::stringstream sig;

      sig << FD->getReturnType().getAsString()
          << (FD->getReturnType()->isPointerType() ? "" : " ");
      get_function_params(sig, FD, show_formal_args, max_args);
      return sig.str();
    }
    return "<unknown>";
  }

  std::string GetFunctionPrototype(TCppFunction_t func, bool show_formal_args)
  {
    auto *D = (clang::Decl *) func;
    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      std::stringstream proto;

      proto << FD->getReturnType().getAsString()
            << (FD->getReturnType()->isPointerType() ? "" : " ");
      proto << FD->getQualifiedNameAsString();
      get_function_params(proto, FD, show_formal_args);
      return proto.str();
    }
    return "<unknown>";
  }

  bool IsTemplatedFunction(TCppFunction_t func)
  {
    auto *D = (Decl *) func;
    if (llvm::isa_and_nonnull<FunctionTemplateDecl>(D)) {
      return true;
    }

    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      auto TK = FD->getTemplatedKind();
      return TK == FunctionDecl::TemplatedKind::
                   TK_FunctionTemplateSpecialization
            || TK == FunctionDecl::TemplatedKind::
                     TK_DependentFunctionTemplateSpecialization
            || TK == FunctionDecl::TemplatedKind::TK_FunctionTemplate;
    }

    return false;
  }

  bool ExistsFunctionTemplate(TCppSema_t sema, const std::string& name,
          TCppScope_t parent)
  {
    DeclContext *Within = 0;
    if (parent) {
      auto *D = (Decl *)parent;
      Within = llvm::dyn_cast<DeclContext>(D);
    }

    auto *S = (Sema *) sema;
    auto *ND = cling::utils::Lookup::Named(S, name, Within);

    if (!ND)
      return false;

    return true;
  }

  bool CheckMethodAccess(TCppFunction_t method, AccessSpecifier AS)
  {
    auto *D = (Decl *) method;
    if (auto *CXXMD = llvm::dyn_cast_or_null<CXXMethodDecl>(D)) {
      return CXXMD->getAccess() == AS;
    }

    return false;
  }

  bool IsPublicMethod(TCppFunction_t method)
  {
    return CheckMethodAccess(method, AccessSpecifier::AS_public);
  }

  bool IsProtectedMethod(TCppFunction_t method)
  {
    return CheckMethodAccess(method, AccessSpecifier::AS_protected);
  }

  bool IsPrivateMethod(TCppFunction_t method)
  {
    return CheckMethodAccess(method, AccessSpecifier::AS_private);
  }

  bool IsConstructor(TCppFunction_t method)
  {
    auto *D = (Decl *) method;
    return llvm::isa_and_nonnull<CXXConstructorDecl>(D);
  }

  bool IsDestructor(TCppFunction_t method)
  {
    auto *D = (Decl *) method;
    return llvm::isa_and_nonnull<CXXDestructorDecl>(D);
  }

  bool IsStaticMethod(TCppFunction_t method)
  {
    auto *D = (Decl *) method;
    if (auto *CXXMD = llvm::dyn_cast_or_null<CXXMethodDecl>(D)) {
      return CXXMD->isStatic();
    }

    return false;
  }

  TCppFuncAddr_t GetFunctionAddress(TInterp_t interp, TCppFunction_t method)
  {
    auto *I = (cling::Interpreter *) interp;
    auto *D = (Decl *) method;

    const auto get_mangled_name = [](FunctionDecl* FD) {
      auto MangleCtxt = FD->getASTContext().createMangleContext();

      if (!MangleCtxt->shouldMangleDeclName(FD)) {
        return FD->getNameInfo().getName().getAsString();
      }
    
      std::string mangled_name;
      llvm::raw_string_ostream ostream(mangled_name);
    
      MangleCtxt->mangleName(FD, ostream);
    
      ostream.flush();
      delete MangleCtxt;
    
      return mangled_name;
    };

    if (auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(D))
      return (TCppFuncAddr_t)I->getAddressOfGlobal(get_mangled_name(FD));

    return 0;
  }

  std::vector<TCppScope_t> GetDatamembers(TCppScope_t scope)
  {
    auto *D = (Decl *) scope;

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(D)) {
      std::vector<TCppScope_t> datamembers;
      for (auto it = CXXRD->field_begin(), end = CXXRD->field_end();
              it != end ; it++) {
        datamembers.push_back((TCppScope_t) *it);
      }

      return datamembers;
    }

    return {};
  }

  TCppType_t GetVariableType(TCppScope_t var)
  {
    auto D = (Decl *) var;

    if (auto DD = llvm::dyn_cast_or_null<DeclaratorDecl>(D)) {
        return DD->getType().getAsOpaquePtr();
    }

    return 0;
  }

  intptr_t GetVariableOffset(TInterp_t interp, TCppScope_t var)
  {
    auto *D = (Decl *) var;
    auto *P = (Decl *) GetParentScope(var);
    auto *I = (cling::Interpreter *) interp;
    auto *S = &I->getCI()->getSema();

    if (P == GetGlobalScope(S) || IsNamespace(P)) {
      if (auto *VD = llvm::dyn_cast_or_null<VarDecl>(D)) {
        auto GD = GlobalDecl(VD);
        std::string mangledName;
        cling::utils::Analyze::maybeMangleDeclName(GD, mangledName);
        auto address = dlsym(/*whole_process=*/0, mangledName.c_str());
        if (!address)
          address = I->getAddressOfGlobal(GD);
        return (intptr_t) address;
      }
    }

    if (auto *CXXRD = llvm::dyn_cast_or_null<CXXRecordDecl>(P)) {
      if (llvm::isa_and_nonnull<VarDecl>(D)) {
        return (intptr_t) I->process(
               (std::string("&") + GetCompleteName(D) + ";").c_str());
      }

      if (auto *FD = llvm::dyn_cast_or_null<FieldDecl>(D)) {
        auto &Cxt = S->getASTContext();
        return (intptr_t) Cxt.toCharUnitsFromBits(Cxt.getASTRecordLayout(CXXRD)
               .getFieldOffset(FD->getFieldIndex())).getQuantity();
      }
    }

    return 0;
  }

  bool CheckVariableAccess(TCppScope_t var, AccessSpecifier AS)
  {
    auto *D = (Decl *) var;
    if (auto *CXXMD = llvm::dyn_cast_or_null<DeclaratorDecl>(D)) {
      return CXXMD->getAccess() == AS;
    }

    return false;
  }

  bool IsPublicVariable(TCppScope_t var)
  {
    return CheckVariableAccess(var, AccessSpecifier::AS_public);
  }

  bool IsProtectedVariable(TCppScope_t var)
  {
    return CheckVariableAccess(var, AccessSpecifier::AS_protected);
  }

  bool IsPrivateVariable(TCppScope_t var)
  {
    return CheckVariableAccess(var, AccessSpecifier::AS_private);
  }

  bool IsStaticVariable(TCppScope_t var)
  {
    auto *D = (Decl *) var;
    if (llvm::isa_and_nonnull<VarDecl>(D)) {
      return true;
    }

    return false;
  }

  bool IsConstVariable(TCppScope_t var)
  {
    auto *D = (clang::Decl *) var;

    if (auto *VD = llvm::dyn_cast_or_null<ValueDecl>(D)) {
      return VD->getType().isConstQualified();
    }

    return false;
  }

  std::string GetTypeAsString(TCppType_t var)
  {
      QualType QT = QualType::getFromOpaquePtr(var);
      return QT.getAsString();
  }

  TCppType_t GetCanonicalType(TCppType_t type)
  {
      QualType QT = QualType::getFromOpaquePtr(type);
      return QT.getCanonicalType().getAsOpaquePtr();
  }

  namespace {
    static QualType findBuiltinType(llvm::StringRef typeName, ASTContext &Context)
    {
      bool issigned = false;
      bool isunsigned = false;
      if (typeName.startswith("signed ")) {
        issigned = true;
        typeName = StringRef(typeName.data()+7, typeName.size()-7);
      }
      if (!issigned && typeName.startswith("unsigned ")) {
        isunsigned = true;
        typeName = StringRef(typeName.data()+9, typeName.size()-9);
      }
      if (typeName.equals("char")) {
        if (isunsigned) return Context.UnsignedCharTy;
        return Context.SignedCharTy;
      }
      if (typeName.equals("short")) {
        if (isunsigned) return Context.UnsignedShortTy;
        return Context.ShortTy;
      }
      if (typeName.equals("int")) {
        if (isunsigned) return Context.UnsignedIntTy;
        return Context.IntTy;
      }
      if (typeName.equals("long")) {
        if (isunsigned) return Context.UnsignedLongTy;
        return Context.LongTy;
      }
      if (typeName.equals("long long")) {
        if (isunsigned) return Context.LongLongTy;
        return Context.UnsignedLongLongTy;
      }
      if (!issigned && !isunsigned) {
        if (typeName.equals("bool")) return Context.BoolTy;
        if (typeName.equals("float")) return Context.FloatTy;
        if (typeName.equals("double")) return Context.DoubleTy;
        if (typeName.equals("long double")) return Context.LongDoubleTy;

        if (typeName.equals("wchar_t")) return Context.WCharTy;
        if (typeName.equals("char16_t")) return Context.Char16Ty;
        if (typeName.equals("char32_t")) return Context.Char32Ty;
      }
      /* Missing
     CanQualType WideCharTy; // Same as WCharTy in C++, integer type in C99.
     CanQualType WIntTy;   // [C99 7.24.1], integer type unchanged by default promotions.
       */
      return QualType();
    }
  }

  TCppType_t GetType(TCppSema_t sema, const std::string &name) {
    auto *S = (Sema *) sema;
    
    auto *ND = cling::utils::Lookup::Named(S, name, 0);

    QualType builtin = findBuiltinType(name, S->getASTContext());
    if (!builtin.isNull())
      return builtin.getAsOpaquePtr();
    
    auto *D = (Decl *) GetScopeFromCompleteName(S, name);
    if (auto *TD = llvm::dyn_cast_or_null<TypeDecl>(D)) {
      return QualType(TD->getTypeForDecl(), 0).getAsOpaquePtr();
    }

    return (TCppType_t) 0;
  }

  TCppType_t GetTypeFromScope(TCppScope_t klass) {
    auto *D = (Decl *) klass;
    if (auto *TD = llvm::dyn_cast_or_null<TypeDecl>(D)) {
      return QualType(TD->getTypeForDecl(), 0).getAsOpaquePtr();
    }

    return (TCppType_t) 0;
  }

  namespace {
    static unsigned long long gWrapperSerial = 0LL;
    static const std::string kIndentString("   ");
    static std::map<const FunctionDecl*, void *> gWrapperStore;

    enum EReferenceType { kNotReference, kLValueReference, kRValueReference };

    void *compile_wrapper(cling::Interpreter* I,
                          const std::string& wrapper_name,
                          const std::string& wrapper,
                          bool withAccessControl = true) {
      printf("%s\n", wrapper.c_str());
      return I->compileFunction(wrapper_name, wrapper, false /*ifUnique*/,
                                withAccessControl);
    }

    void get_type_as_string(QualType QT, std::string& type_name, ASTContext& C,
                            PrintingPolicy Policy) {
      // FIXME: Take the code here
      // https://github.com/root-project/root/blob/550fb2644f3c07d1db72b9b4ddc4eba5a99ddc12/interpreter/cling/lib/Utils/AST.cpp#L316-L350
      // to make hist/histdrawv7/test/histhistdrawv7testUnit work into
      // QualTypeNames.h in clang
      // type_name = clang::TypeName::getFullyQualifiedName(QT, C, Policy);
      cling::utils::Transform::Config Config;
      QT = cling::utils::Transform::GetPartiallyDesugaredType(
          C, QT, Config, /*fullyQualify=*/true);
      QT.getAsStringInternal(type_name, Policy);
    }

    void collect_type_info(const FunctionDecl* FD, QualType& QT,
                           std::ostringstream& typedefbuf,
                           std::ostringstream& callbuf, std::string& type_name,
                           EReferenceType& refType, bool& isPointer,
                           int indent_level, bool forArgument) {
      //
      //  Collect information about type type of a function parameter
      //  needed for building the wrapper function.
      //
      ASTContext& C = FD->getASTContext();
      PrintingPolicy Policy(C.getPrintingPolicy());
      refType = kNotReference;
      if (QT->isRecordType() && forArgument) {
        get_type_as_string(QT, type_name, C, Policy);
        return;
      }
      if (QT->isFunctionPointerType()) {
        std::string fp_typedef_name;
        {
          std::ostringstream nm;
          nm << "FP" << gWrapperSerial++;
          type_name = nm.str();
          raw_string_ostream OS(fp_typedef_name);
          QT.print(OS, Policy, type_name);
          OS.flush();
        }
        for (int i = 0; i < indent_level; ++i) {
          typedefbuf << kIndentString;
        }
        typedefbuf << "typedef " << fp_typedef_name << ";\n";
        return;
      } else if (QT->isMemberPointerType()) {
        std::string mp_typedef_name;
        {
          std::ostringstream nm;
          nm << "MP" << gWrapperSerial++;
          type_name = nm.str();
          raw_string_ostream OS(mp_typedef_name);
          QT.print(OS, Policy, type_name);
          OS.flush();
        }
        for (int i = 0; i < indent_level; ++i) {
          typedefbuf << kIndentString;
        }
        typedefbuf << "typedef " << mp_typedef_name << ";\n";
        return;
      } else if (QT->isPointerType()) {
        isPointer = true;
        QT = cast<clang::PointerType>(QT)->getPointeeType();
      } else if (QT->isReferenceType()) {
        if (QT->isRValueReferenceType())
          refType = kRValueReference;
        else
          refType = kLValueReference;
        QT = cast<ReferenceType>(QT)->getPointeeType();
      }
      // Fall through for the array type to deal with reference/pointer ro array
      // type.
      if (QT->isArrayType()) {
        std::string ar_typedef_name;
        {
          std::ostringstream ar;
          ar << "AR" << gWrapperSerial++;
          type_name = ar.str();
          raw_string_ostream OS(ar_typedef_name);
          QT.print(OS, Policy, type_name);
          OS.flush();
        }
        for (int i = 0; i < indent_level; ++i) {
          typedefbuf << kIndentString;
        }
        typedefbuf << "typedef " << ar_typedef_name << ";\n";
        return;
      }
      get_type_as_string(QT, type_name, C, Policy);
    }

    void make_narg_ctor(const FunctionDecl* FD, const unsigned N,
                        std::ostringstream& typedefbuf,
                        std::ostringstream& callbuf,
                        const std::string& class_name, int indent_level) {
      // Make a code string that follows this pattern:
      //
      // new ClassName(args...)
      //

      callbuf << "new " << class_name << "(";
      for (unsigned i = 0U; i < N; ++i) {
        const ParmVarDecl* PVD = FD->getParamDecl(i);
        QualType Ty = PVD->getType();
        QualType QT = Ty.getCanonicalType();
        std::string type_name;
        EReferenceType refType = kNotReference;
        bool isPointer = false;
        collect_type_info(FD, QT, typedefbuf, callbuf, type_name, refType,
                          isPointer, indent_level, true);
        if (i) {
          callbuf << ',';
          if (i % 2) {
            callbuf << ' ';
          } else {
            callbuf << "\n";
            for (int j = 0; j <= indent_level; ++j) {
              callbuf << kIndentString;
            }
          }
        }
        if (refType != kNotReference) {
          callbuf << "(" << type_name.c_str()
                  << (refType == kLValueReference ? "&" : "&&") << ")*("
                  << type_name.c_str() << "*)args[" << i << "]";
        } else if (isPointer) {
          callbuf << "*(" << type_name.c_str() << "**)args[" << i << "]";
        } else {
          callbuf << "*(" << type_name.c_str() << "*)args[" << i << "]";
        }
      }
      callbuf << ")";
    }

    void make_narg_call(const FunctionDecl* FD, const std::string& return_type,
                        const unsigned N, std::ostringstream& typedefbuf,
                        std::ostringstream& callbuf,
                        const std::string& class_name, int indent_level) {
      //
      // Make a code string that follows this pattern:
      //
      // ((<class>*)obj)-><method>(*(<arg-i-type>*)args[i], ...)
      //

      // Sometimes it's necessary that we cast the function we want to call
      // first to its explicit function type before calling it. This is supposed
      // to prevent that we accidentially ending up in a function that is not
      // the one we're supposed to call here (e.g. because the C++ function
      // lookup decides to take another function that better fits). This method
      // has some problems, e.g. when we call a function with default arguments
      // and we don't provide all arguments, we would fail with this pattern.
      // Same applies with member methods which seem to cause parse failures
      // even when we supply the object parameter. Therefore we only use it in
      // cases where we know it works and set this variable to true when we do.
      bool ShouldCastFunction =
          !isa<CXXMethodDecl>(FD) && N == FD->getNumParams();
      if (ShouldCastFunction) {
        callbuf << "(";
        callbuf << "(";
        callbuf << return_type << " (&)";
        {
          callbuf << "(";
          for (unsigned i = 0U; i < N; ++i) {
            if (i) {
              callbuf << ',';
              if (i % 2) {
                callbuf << ' ';
              } else {
                callbuf << "\n";
                for (int j = 0; j <= indent_level; ++j) {
                  callbuf << kIndentString;
                }
              }
            }
            const ParmVarDecl* PVD = FD->getParamDecl(i);
            QualType Ty = PVD->getType();
            QualType QT = Ty.getCanonicalType();
            std::string arg_type;
            ASTContext& C = FD->getASTContext();
            get_type_as_string(QT, arg_type, C, C.getPrintingPolicy());
            callbuf << arg_type;
          }
          if (FD->isVariadic())
            callbuf << ", ...";
          callbuf << ")";
        }

        callbuf << ")";
      }

      if (const CXXMethodDecl* MD = dyn_cast<CXXMethodDecl>(FD)) {
        // This is a class, struct, or union member.
        if (MD->isConst())
          callbuf << "((const " << class_name << "*)obj)->";
        else
          callbuf << "((" << class_name << "*)obj)->";
      } else if (const NamedDecl* ND =
                     dyn_cast<NamedDecl>(FD->getDeclContext())) {
        // This is a namespace member.
        (void)ND;
        callbuf << class_name << "::";
      }
      //   callbuf << fMethod->Name() << "(";
      {
        std::string name;
        {
          llvm::raw_string_ostream stream(name);
          FD->getNameForDiagnostic(stream,
                                   FD->getASTContext().getPrintingPolicy(),
                                   /*Qualified=*/false);
        }
        callbuf << name;
      }
      if (ShouldCastFunction)
        callbuf << ")";

      callbuf << "(";
      for (unsigned i = 0U; i < N; ++i) {
        const ParmVarDecl* PVD = FD->getParamDecl(i);
        QualType Ty = PVD->getType();
        QualType QT = Ty.getCanonicalType();
        std::string type_name;
        EReferenceType refType = kNotReference;
        bool isPointer = false;
        collect_type_info(FD, QT, typedefbuf, callbuf, type_name, refType,
                          isPointer, indent_level, true);

        if (i) {
          callbuf << ',';
          if (i % 2) {
            callbuf << ' ';
          } else {
            callbuf << "\n";
            for (int j = 0; j <= indent_level; ++j) {
              callbuf << kIndentString;
            }
          }
        }

        if (refType != kNotReference) {
          callbuf << "(" << type_name.c_str()
                  << (refType == kLValueReference ? "&" : "&&") << ")*("
                  << type_name.c_str() << "*)args[" << i << "]";
        } else if (isPointer) {
          callbuf << "*(" << type_name.c_str() << "**)args[" << i << "]";
        } else {
          // pointer falls back to non-pointer case; the argument preserves
          // the "pointerness" (i.e. doesn't reference the value).
          callbuf << "*(" << type_name.c_str() << "*)args[" << i << "]";
        }
      }
      callbuf << ")";
    }

    void make_narg_ctor_with_return(const FunctionDecl* FD, const unsigned N,
                                    const std::string& class_name,
                                    std::ostringstream& buf, int indent_level) {
      // Make a code string that follows this pattern:
      //
      // if (ret) {
      //    (*(ClassName**)ret) = new ClassName(args...);
      // }
      // else {
      //    new ClassName(args...);
      // }
      //
      for (int i = 0; i < indent_level; ++i) {
        buf << kIndentString;
      }
      buf << "if (ret) {\n";
      ++indent_level;
      {
        std::ostringstream typedefbuf;
        std::ostringstream callbuf;
        //
        //  Write the return value assignment part.
        //
        for (int i = 0; i < indent_level; ++i) {
          callbuf << kIndentString;
        }
        callbuf << "(*(" << class_name << "**)ret) = ";
        //
        //  Write the actual new expression.
        //
        make_narg_ctor(FD, N, typedefbuf, callbuf, class_name, indent_level);
        //
        //  End the new expression statement.
        //
        callbuf << ";\n";
        for (int i = 0; i < indent_level; ++i) {
          callbuf << kIndentString;
        }
        callbuf << "return;\n";
        //
        //  Output the whole new expression and return statement.
        //
        buf << typedefbuf.str() << callbuf.str();
      }
      --indent_level;
      for (int i = 0; i < indent_level; ++i) {
        buf << kIndentString;
      }
      buf << "}\n";
      for (int i = 0; i < indent_level; ++i) {
        buf << kIndentString;
      }
      buf << "else {\n";
      ++indent_level;
      {
        std::ostringstream typedefbuf;
        std::ostringstream callbuf;
        for (int i = 0; i < indent_level; ++i) {
          callbuf << kIndentString;
        }
        make_narg_ctor(FD, N, typedefbuf, callbuf, class_name, indent_level);
        callbuf << ";\n";
        for (int i = 0; i < indent_level; ++i) {
          callbuf << kIndentString;
        }
        callbuf << "return;\n";
        buf << typedefbuf.str() << callbuf.str();
      }
      --indent_level;
      for (int i = 0; i < indent_level; ++i) {
        buf << kIndentString;
      }
      buf << "}\n";
    }

    void make_narg_call_with_return(cling::Interpreter* I,
                                    const FunctionDecl* FD, const unsigned N,
                                    const std::string& class_name,
                                    std::ostringstream& buf, int indent_level) {
      // Make a code string that follows this pattern:
      //
      // if (ret) {
      //    new (ret) (return_type) ((class_name*)obj)->func(args...);
      // }
      // else {
      //    (void)(((class_name*)obj)->func(args...));
      // }
      //
      if (const CXXConstructorDecl* CD = dyn_cast<CXXConstructorDecl>(FD)) {
        if (N <= 1 && llvm::isa<UsingShadowDecl>(FD)) {
          auto SpecMemKind = I->getSema().getSpecialMember(CD);
          if ((N == 0 && SpecMemKind == clang::Sema::CXXDefaultConstructor) ||
              (N == 1 && (SpecMemKind == clang::Sema::CXXCopyConstructor ||
                          SpecMemKind == clang::Sema::CXXMoveConstructor))) {
            // Using declarations cannot inject special members; do not call
            // them as such. This might happen by using `Base(Base&, int = 12)`,
            // which is fine to be called as `Derived d(someBase, 42)` but not
            // as copy constructor of `Derived`.
            return;
          }
        }
        make_narg_ctor_with_return(FD, N, class_name, buf, indent_level);
        return;
      }
      QualType QT = FD->getReturnType().getCanonicalType();
      if (QT->isVoidType()) {
        std::ostringstream typedefbuf;
        std::ostringstream callbuf;
        for (int i = 0; i < indent_level; ++i) {
          callbuf << kIndentString;
        }
        make_narg_call(FD, "void", N, typedefbuf, callbuf, class_name,
                       indent_level);
        callbuf << ";\n";
        for (int i = 0; i < indent_level; ++i) {
          callbuf << kIndentString;
        }
        callbuf << "return;\n";
        buf << typedefbuf.str() << callbuf.str();
      } else {
        for (int i = 0; i < indent_level; ++i) {
          buf << kIndentString;
        }

        std::string type_name;
        EReferenceType refType = kNotReference;
        bool isPointer = false;

        buf << "if (ret) {\n";
        ++indent_level;
        {
          std::ostringstream typedefbuf;
          std::ostringstream callbuf;
          //
          //  Write the placement part of the placement new.
          //
          for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
          }
          callbuf << "new (ret) ";
          collect_type_info(FD, QT, typedefbuf, callbuf, type_name, refType,
                            isPointer, indent_level, false);
          //
          //  Write the type part of the placement new.
          //
          callbuf << "(" << type_name.c_str();
          if (refType != kNotReference) {
            callbuf << "*) (&";
            type_name += "&";
          } else if (isPointer) {
            callbuf << "*) (";
            type_name += "*";
          } else {
            callbuf << ") (";
          }
          //
          //  Write the actual function call.
          //
          make_narg_call(FD, type_name, N, typedefbuf, callbuf, class_name,
                         indent_level);
          //
          //  End the placement new.
          //
          callbuf << ");\n";
          for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
          }
          callbuf << "return;\n";
          //
          //  Output the whole placement new expression and return statement.
          //
          buf << typedefbuf.str() << callbuf.str();
        }
        --indent_level;
        for (int i = 0; i < indent_level; ++i) {
          buf << kIndentString;
        }
        buf << "}\n";
        for (int i = 0; i < indent_level; ++i) {
          buf << kIndentString;
        }
        buf << "else {\n";
        ++indent_level;
        {
          std::ostringstream typedefbuf;
          std::ostringstream callbuf;
          for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
          }
          callbuf << "(void)(";
          make_narg_call(FD, type_name, N, typedefbuf, callbuf, class_name,
                         indent_level);
          callbuf << ");\n";
          for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
          }
          callbuf << "return;\n";
          buf << typedefbuf.str() << callbuf.str();
        }
        --indent_level;
        for (int i = 0; i < indent_level; ++i) {
          buf << kIndentString;
        }
        buf << "}\n";
      }
    }

    int get_wrapper_code(cling::Interpreter* I, const FunctionDecl* FD,
                         std::string& wrapper_name, std::string& wrapper) {
      assert(FD && "generate_wrapper called without a function decl!");
      ASTContext& Context = FD->getASTContext();
      PrintingPolicy Policy(Context.getPrintingPolicy());
      //
      //  Get the class or namespace name.
      //
      std::string class_name;
      const clang::DeclContext* DC = FD->getDeclContext();
      if (const TypeDecl* TD = dyn_cast<TypeDecl>(DC)) {
        // This is a class, struct, or union member.
        QualType QT(TD->getTypeForDecl(), 0);
        get_type_as_string(QT, class_name, Context, Policy);
      } else if (const NamedDecl* ND = dyn_cast<NamedDecl>(DC)) {
        // This is a namespace member.
        raw_string_ostream stream(class_name);
        ND->getNameForDiagnostic(stream, Policy, /*Qualified=*/true);
        stream.flush();
      }
      //
      //  Check to make sure that we can
      //  instantiate and codegen this function.
      //
      bool needInstantiation = false;
      const FunctionDecl* Definition = 0;
      if (!FD->isDefined(Definition)) {
        FunctionDecl::TemplatedKind TK = FD->getTemplatedKind();
        switch (TK) {
          case FunctionDecl::TK_NonTemplate: {
            // Ordinary function, not a template specialization.
            // Note: This might be ok, the body might be defined
            //       in a library, and all we have seen is the
            //       header file.
            // llvm::errs() << "TClingCallFunc::make_wrapper" << ":" <<
            //      "Cannot make wrapper for a function which is "
            //      "declared but not defined!";
            // return 0;
          } break;
          case FunctionDecl::TK_FunctionTemplate: {
            // This decl is actually a function template,
            // not a function at all.
            llvm::errs() << "TClingCallFunc::make_wrapper"
                         << ":"
                         << "Cannot make wrapper for a function template!";
            return 0;
          } break;
          case FunctionDecl::TK_MemberSpecialization: {
            // This function is the result of instantiating an ordinary
            // member function of a class template, or of instantiating
            // an ordinary member function of a class member of a class
            // template, or of specializing a member function template
            // of a class template, or of specializing a member function
            // template of a class member of a class template.
            if (!FD->isTemplateInstantiation()) {
              // We are either TSK_Undeclared or
              // TSK_ExplicitSpecialization.
              // Note: This might be ok, the body might be defined
              //       in a library, and all we have seen is the
              //       header file.
              // llvm::errs() << "TClingCallFunc::make_wrapper" << ":" <<
              //      "Cannot make wrapper for a function template "
              //      "explicit specialization which is declared "
              //      "but not defined!";
              // return 0;
              break;
            }
            const FunctionDecl* Pattern = FD->getTemplateInstantiationPattern();
            if (!Pattern) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a member function "
                              "instantiation with no pattern!";
              return 0;
            }
            FunctionDecl::TemplatedKind PTK = Pattern->getTemplatedKind();
            TemplateSpecializationKind PTSK =
                Pattern->getTemplateSpecializationKind();
            if (
                // The pattern is an ordinary member function.
                (PTK == FunctionDecl::TK_NonTemplate) ||
                // The pattern is an explicit specialization, and
                // so is not a template.
                ((PTK != FunctionDecl::TK_FunctionTemplate) &&
                 ((PTSK == TSK_Undeclared) ||
                  (PTSK == TSK_ExplicitSpecialization)))) {
              // Note: This might be ok, the body might be defined
              //       in a library, and all we have seen is the
              //       header file.
              break;
            } else if (!Pattern->hasBody()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a member function "
                              "instantiation with no body!";
              return 0;
            }
            if (FD->isImplicitlyInstantiable()) {
              needInstantiation = true;
            }
          } break;
          case FunctionDecl::TK_FunctionTemplateSpecialization: {
            // This function is the result of instantiating a function
            // template or possibly an explicit specialization of a
            // function template.  Could be a namespace scope function or a
            // member function.
            if (!FD->isTemplateInstantiation()) {
              // We are either TSK_Undeclared or
              // TSK_ExplicitSpecialization.
              // Note: This might be ok, the body might be defined
              //       in a library, and all we have seen is the
              //       header file.
              // llvm::errs() << "TClingCallFunc::make_wrapper" << ":" <<
              //      "Cannot make wrapper for a function template "
              //      "explicit specialization which is declared "
              //      "but not defined!";
              // return 0;
              break;
            }
            const FunctionDecl* Pattern = FD->getTemplateInstantiationPattern();
            if (!Pattern) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a function template"
                              "instantiation with no pattern!";
              return 0;
            }
            FunctionDecl::TemplatedKind PTK = Pattern->getTemplatedKind();
            TemplateSpecializationKind PTSK =
                Pattern->getTemplateSpecializationKind();
            if (
                // The pattern is an ordinary member function.
                (PTK == FunctionDecl::TK_NonTemplate) ||
                // The pattern is an explicit specialization, and
                // so is not a template.
                ((PTK != FunctionDecl::TK_FunctionTemplate) &&
                 ((PTSK == TSK_Undeclared) ||
                  (PTSK == TSK_ExplicitSpecialization)))) {
              // Note: This might be ok, the body might be defined
              //       in a library, and all we have seen is the
              //       header file.
              break;
            }
            if (!Pattern->hasBody()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a function template"
                              "instantiation with no body!";
              return 0;
            }
            if (FD->isImplicitlyInstantiable()) {
              needInstantiation = true;
            }
          } break;
          case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
            // This function is the result of instantiating or
            // specializing a  member function of a class template,
            // or a member function of a class member of a class template,
            // or a member function template of a class template, or a
            // member function template of a class member of a class
            // template where at least some part of the function is
            // dependent on a template argument.
            if (!FD->isTemplateInstantiation()) {
              // We are either TSK_Undeclared or
              // TSK_ExplicitSpecialization.
              // Note: This might be ok, the body might be defined
              //       in a library, and all we have seen is the
              //       header file.
              // llvm::errs() << "TClingCallFunc::make_wrapper" << ":" <<
              //      "Cannot make wrapper for a dependent function "
              //      "template explicit specialization which is declared "
              //      "but not defined!";
              // return 0;
              break;
            }
            const FunctionDecl* Pattern = FD->getTemplateInstantiationPattern();
            if (!Pattern) {
              llvm::errs()
                  << "TClingCallFunc::make_wrapper"
                  << ":"
                  << "Cannot make wrapper for a dependent function template"
                     "instantiation with no pattern!";
              return 0;
            }
            FunctionDecl::TemplatedKind PTK = Pattern->getTemplatedKind();
            TemplateSpecializationKind PTSK =
                Pattern->getTemplateSpecializationKind();
            if (
                // The pattern is an ordinary member function.
                (PTK == FunctionDecl::TK_NonTemplate) ||
                // The pattern is an explicit specialization, and
                // so is not a template.
                ((PTK != FunctionDecl::TK_FunctionTemplate) &&
                 ((PTSK == TSK_Undeclared) ||
                  (PTSK == TSK_ExplicitSpecialization)))) {
              // Note: This might be ok, the body might be defined
              //       in a library, and all we have seen is the
              //       header file.
              break;
            }
            if (!Pattern->hasBody()) {
              llvm::errs()
                  << "TClingCallFunc::make_wrapper"
                  << ":"
                  << "Cannot make wrapper for a dependent function template"
                     "instantiation with no body!";
              return 0;
            }
            if (FD->isImplicitlyInstantiable()) {
              needInstantiation = true;
            }
          } break;
          default: {
            // Will only happen if clang implementation changes.
            // Protect ourselves in case that happens.
            llvm::errs() << "TClingCallFunc::make_wrapper" << ":" <<
                           "Unhandled template kind!";
            return 0;
          } break;
        }
        // We do not set needInstantiation to true in these cases:
        //
        // isInvalidDecl()
        // TSK_Undeclared
        // TSK_ExplicitInstantiationDefinition
        // TSK_ExplicitSpecialization && !getClassScopeSpecializationPattern()
        // TSK_ExplicitInstantiationDeclaration &&
        //    getTemplateInstantiationPattern() &&
        //    PatternDecl->hasBody() &&
        //    !PatternDecl->isInlined()
        //
        // Set it true in these cases:
        //
        // TSK_ImplicitInstantiation
        // TSK_ExplicitInstantiationDeclaration && (!getPatternDecl() ||
        //    !PatternDecl->hasBody() || PatternDecl->isInlined())
        //
      }
      if (needInstantiation) {
        clang::FunctionDecl* FDmod = const_cast<clang::FunctionDecl*>(FD);
        clang::Sema& S = I->getSema();
        // Could trigger deserialization of decls.
        cling::Interpreter::PushTransactionRAII RAII(I);
        S.InstantiateFunctionDefinition(SourceLocation(), FDmod,
                                        /*Recursive=*/true,
                                        /*DefinitionRequired=*/true);
        if (!FD->isDefined(Definition)) {
          llvm::errs() << "TClingCallFunc::make_wrapper"
                       << ":"
                       << "Failed to force template instantiation!";
          return 0;
        }
      }
      if (Definition) {
        FunctionDecl::TemplatedKind TK = Definition->getTemplatedKind();
        switch (TK) {
          case FunctionDecl::TK_NonTemplate: {
            // Ordinary function, not a template specialization.
            if (Definition->isDeleted()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a deleted function!";
              return 0;
            } else if (Definition->isLateTemplateParsed()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a late template parsed "
                              "function!";
              return 0;
            }
            // else if (Definition->isDefaulted()) {
            //   // Might not have a body, but we can still use it.
            //}
            // else {
            //   // Has a body.
            //}
          } break;
          case FunctionDecl::TK_FunctionTemplate: {
            // This decl is actually a function template,
            // not a function at all.
            llvm::errs() << "TClingCallFunc::make_wrapper"
                         << ":"
                         << "Cannot make wrapper for a function template!";
            return 0;
          } break;
          case FunctionDecl::TK_MemberSpecialization: {
            // This function is the result of instantiating an ordinary
            // member function of a class template or of a member class
            // of a class template.
            if (Definition->isDeleted()) {
              llvm::errs()
                  << "TClingCallFunc::make_wrapper"
                  << ":"
                  << "Cannot make wrapper for a deleted member function "
                     "of a specialization!";
              return 0;
            } else if (Definition->isLateTemplateParsed()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a late template parsed "
                              "member function of a specialization!";
              return 0;
            }
            // else if (Definition->isDefaulted()) {
            //   // Might not have a body, but we can still use it.
            //}
            // else {
            //   // Has a body.
            //}
          } break;
          case FunctionDecl::TK_FunctionTemplateSpecialization: {
            // This function is the result of instantiating a function
            // template or possibly an explicit specialization of a
            // function template.  Could be a namespace scope function or a
            // member function.
            if (Definition->isDeleted()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a deleted function "
                              "template specialization!";
              return 0;
            } else if (Definition->isLateTemplateParsed()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a late template parsed "
                              "function template specialization!";
              return 0;
            }
            // else if (Definition->isDefaulted()) {
            //   // Might not have a body, but we can still use it.
            //}
            // else {
            //   // Has a body.
            //}
          } break;
          case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
            // This function is the result of instantiating or
            // specializing a  member function of a class template,
            // or a member function of a class member of a class template,
            // or a member function template of a class template, or a
            // member function template of a class member of a class
            // template where at least some part of the function is
            // dependent on a template argument.
            if (Definition->isDeleted()) {
              llvm::errs()
                  << "TClingCallFunc::make_wrapper"
                  << ":"
                  << "Cannot make wrapper for a deleted dependent function "
                     "template specialization!";
              return 0;
            } else if (Definition->isLateTemplateParsed()) {
              llvm::errs() << "TClingCallFunc::make_wrapper"
                           << ":"
                           << "Cannot make wrapper for a late template parsed "
                              "dependent function template specialization!";
              return 0;
            }
            // else if (Definition->isDefaulted()) {
            //   // Might not have a body, but we can still use it.
            //}
            // else {
            //   // Has a body.
            //}
          } break;
          default: {
            // Will only happen if clang implementation changes.
            // Protect ourselves in case that happens.
            llvm::errs() << "TClingCallFunc::make_wrapper"
                         << ":"
                         << "Unhandled template kind!";
            return 0;
          } break;
        }
      }
      unsigned min_args = FD->getMinRequiredArguments();
      unsigned num_params = FD->getNumParams();
      //
      //  Make the wrapper name.
      //
      {
        std::ostringstream buf;
        buf << "__cf";
        // const NamedDecl* ND = dyn_cast<NamedDecl>(FD);
        // std::string mn;
        // fInterp->maybeMangleDeclName(ND, mn);
        // buf << '_' << mn;
        buf << '_' << gWrapperSerial++;
        wrapper_name = buf.str();
      }
      //
      //  Write the wrapper code.
      // FIXME: this should be synthesized into the AST!
      //
      int indent_level = 0;
      std::ostringstream buf;
      buf << "#pragma clang diagnostic push\n"
             "#pragma clang diagnostic ignored \"-Wformat-security\"\n"
             "__attribute__((used)) "
             "__attribute__((annotate(\"__cling__ptrcheck(off)\")))\n"
             "extern \"C\" void ";
      buf << wrapper_name;
      buf << "(void* obj, int nargs, void** args, void* ret)\n"
             "{\n";
      ++indent_level;
      if (min_args == num_params) {
        // No parameters with defaults.
        make_narg_call_with_return(I, FD, num_params, class_name, buf,
                                   indent_level);
      } else {
        // We need one function call clause compiled for every
        // possible number of arguments per call.
        for (unsigned N = min_args; N <= num_params; ++N) {
          for (int i = 0; i < indent_level; ++i) {
            buf << kIndentString;
          }
          buf << "if (nargs == " << N << ") {\n";
          ++indent_level;
          make_narg_call_with_return(I, FD, N, class_name, buf, indent_level);
          --indent_level;
          for (int i = 0; i < indent_level; ++i) {
            buf << kIndentString;
          }
          buf << "}\n";
        }
      }
      --indent_level;
      buf << "}\n"
             "#pragma clang diagnostic pop";
      wrapper = buf.str();
      return 1;
    }

    CallFuncWrapper_t make_wrapper(cling::Interpreter* I,
                                   const FunctionDecl* FD) {
      std::string wrapper_name;
      std::string wrapper_code;

      if (get_wrapper_code(I, FD, wrapper_name, wrapper_code) == 0)
        return 0;

      //
      //   Compile the wrapper code.
      //
      void *wrapper = compile_wrapper(I, wrapper_name, wrapper_code);
      if (wrapper) {
        gWrapperStore.insert(std::make_pair(FD, wrapper));
      } else {
        llvm::errs() << "TClingCallFunc::make_wrapper"
                     << ":"
                     << "Failed to compile\n  ==== SOURCE BEGIN ====\n%s\n  "
                        "==== SOURCE END ====",
            wrapper_code.c_str();
      }
      return (CallFuncWrapper_t)wrapper;
    }
  } // namespace

  CallFuncWrapper_t GetFunctionCallWrapper(TInterp_t interp,
                                           TCppFunction_t func) {
    auto* I = (cling::Interpreter*)interp;
    auto* D = (clang::Decl*)func;

    if (auto* FD = llvm::dyn_cast_or_null<clang::FunctionDecl>(D)) {
      CallFuncWrapper_t wrapper;
      auto R = gWrapperStore.find(FD);
      if (R != gWrapperStore.end()) {
        wrapper = (CallFuncWrapper_t) R->second;
      } else {
        wrapper = make_wrapper(I, FD);
      }

      return wrapper;
    }

    return 0;
  }

  void AddSearchPath(TInterp_t interp, const char *dir, bool isUser,
                     bool prepend) {
    auto* I = (cling::Interpreter*)interp;

    I->getDynamicLibraryManager()->addSearchPath(dir, isUser, prepend);
  }

  void AddIncludePath(TInterp_t interp, const char *dir) {
    auto* I = (cling::Interpreter*)interp;

    I->AddIncludePath(dir);
  }

  TCppIndex_t Declare(TInterp_t interp, const char *code) {
    auto* I = (cling::Interpreter*)interp;

    return I->declare(code);
  }

  const std::string LookupLibrary(TInterp_t interp, const char *lib_name) {
    auto* I = (cling::Interpreter*)interp;

    return I->getDynamicLibraryManager()->lookupLibrary(lib_name);
  }

  bool LoadLibrary(TInterp_t interp, const char *lib_name) {
    auto* I = (cling::Interpreter*)interp;
    cling::Interpreter::CompilationResult res =
      I->loadLibrary(lib_name, /* lookup */ true);
    
    return res == cling::Interpreter::kSuccess;
  }

  TCppScope_t InstantiateClassTemplate(TInterp_t interp, const char *tmpl_name) {
    auto* I = (cling::Interpreter*)interp;

    static unsigned counter = 0;
    std::stringstream ss;
    
    ss << "auto _t" << counter++ << " = " << tmpl_name << "();";
    printf("%s\n", ss.str().c_str());
    Transaction *T = nullptr;
    auto x = I->declare(ss.str(), &T);
    if (x == Interpreter::CompilationResult::kSuccess) {
      for (auto D = T->decls_begin(); D != T->decls_end(); D++) {
        if (auto *VD = llvm::dyn_cast_or_null<VarDecl>(D->m_DGR.getSingleDecl())) {
          auto *scope = GetScopeFromType(VD->getType());
          if (scope) {
            return (TCppScope_t) scope;
          }
        }
      }
    } else {
      return 0;
    }
  }
} // end namespace InterOp

} // end namespace cling

