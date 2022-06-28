
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/InterOp.h"

#include "clang/Config/config.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include "gtest/gtest.h"

using namespace cling;
using namespace clang;
using namespace llvm;

// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// GetMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement GetMainExecutable
// without being given the address of a function in the main executable).
std::string GetExecutablePath(const char *Argv0, void *MainAddr) {
  return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

static std::string MakeResourcesPath() {
  // Dir is bin/ or lib/, depending on where BinaryPath is.
  void *MainAddr = (void *)(intptr_t)GetExecutablePath;
  std::string BinaryPath = GetExecutablePath(/*Argv0=*/nullptr, MainAddr);

  // build/tools/clang/unittests/Interpreter/Executable -> build/
  llvm::StringRef Dir = llvm::sys::path::parent_path(BinaryPath);

  Dir = llvm::sys::path::parent_path(Dir);
  Dir = llvm::sys::path::parent_path(Dir);
  Dir = llvm::sys::path::parent_path(Dir);
  Dir = llvm::sys::path::parent_path(Dir);
  //Dir = llvm::sys::path::parent_path(Dir);
  llvm::SmallString<128> P(Dir);
  llvm::sys::path::append(P, llvm::Twine("lib") + CLANG_LIBDIR_SUFFIX, "clang",
                          CLANG_VERSION_STRING);

  return std::string(P.str());
}

static std::unique_ptr<Interpreter> createInterpreter() {
  std::string MainExecutableName =
    llvm::sys::fs::getMainExecutable(nullptr, nullptr);
  std::string ResourceDir = MakeResourcesPath();
  std::vector<const char *> ClingArgv = {"-resource-dir", ResourceDir.c_str()};
  ClingArgv.insert(ClingArgv.begin(), MainExecutableName.c_str());
  return llvm::make_unique<Interpreter>(ClingArgv.size(), &ClingArgv[0]);
}

std::unique_ptr<Interpreter> Interp;

static void GetAllTopLevelDecls(const std::string& code, std::vector<Decl*>& Decls) {
  Interp = createInterpreter();
  Transaction *T = nullptr;
  Interp->declare(code, &T);
  for (auto DCI = T->decls_begin(), E = T->decls_end(); DCI != E; ++DCI) {
    if (DCI->m_Call != Transaction::kCCIHandleTopLevelDecl)
      continue;
    assert(DCI->m_DGR.isSingleDecl());
    Decls.push_back(DCI->m_DGR.getSingleDecl());
  }
}

static void GetAllSubDecls(Decl *D, std::vector<Decl*>& SubDecls) {
  DeclContext *DC = Decl::castToDeclContext(D);
  for (auto DCI = DC->decls_begin(), E = DC->decls_end(); DCI != E; ++DCI) {
    SubDecls.push_back(*DCI);
  }
}

// Check that the CharInfo table has been constructed reasonably.
TEST(ScopeReflectionTest, IsNamespace) {
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls("namespace N {} class C{}; int I;", Decls);
  EXPECT_TRUE(InterOp::IsNamespace(Decls[0]));
  EXPECT_FALSE(InterOp::IsNamespace(Decls[1]));
  EXPECT_FALSE(InterOp::IsNamespace(Decls[2]));
}

TEST(ScopeReflectionTest, IsComplete) {
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls("namespace N {} class C{}; int I; struct S; enum E : int; union U{};",
                      Decls);
  EXPECT_TRUE(InterOp::IsComplete(Decls[0]));
  EXPECT_TRUE(InterOp::IsComplete(Decls[1]));
  EXPECT_TRUE(InterOp::IsComplete(Decls[2]));
  EXPECT_FALSE(InterOp::IsComplete(Decls[3]));
  EXPECT_FALSE(InterOp::IsComplete(Decls[4]));
  EXPECT_TRUE(InterOp::IsComplete(Decls[5]));
}

TEST(ScopeReflectionTest, SizeOf) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N {} class C{}; int I; struct S;
                        enum E : int; union U{}; class Size4{int i;};
                        struct Size16 {short a; double b;};
                       )";
  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(InterOp::SizeOf(Decls[0]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[1]), (size_t)1);
  EXPECT_EQ(InterOp::SizeOf(Decls[2]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[3]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[4]), (size_t)0);
  EXPECT_EQ(InterOp::SizeOf(Decls[5]), (size_t)1);
  EXPECT_EQ(InterOp::SizeOf(Decls[6]), (size_t)4);
  EXPECT_EQ(InterOp::SizeOf(Decls[7]), (size_t)16);
}

TEST(ScopeReflectionTest, IsBuiltin) {
  // static std::set<std::string> g_builtins =
  // {"bool", "char", "signed char", "unsigned char", "wchar_t", "short", "unsigned short",
  //  "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long",
  //  "float", "double", "long double", "void"}

  ASTContext &C = Interp->getCI()->getASTContext();
  EXPECT_TRUE(InterOp::IsBuiltin(C.BoolTy.getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.CharTy.getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.SignedCharTy.getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.VoidTy.getAsOpaquePtr()));
  // ...

  // complex
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.FloatTy).getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.DoubleTy).getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.LongDoubleTy).getAsOpaquePtr()));
  EXPECT_TRUE(InterOp::IsBuiltin(C.getComplexType(C.Float128Ty).getAsOpaquePtr()));

  // std::complex
  std::vector<Decl*> Decls;
  Interp->declare("#include <complex>");
  Sema &S = Interp->getCI()->getSema();
  auto lookup = S.getStdNamespace()->lookup(&C.Idents.get("complex"));
  auto *CTD = cast<ClassTemplateDecl>(lookup.front());
  for (ClassTemplateSpecializationDecl *CTSD : CTD->specializations())
    EXPECT_TRUE(InterOp::IsBuiltin(C.getTypeDeclType(CTSD).getAsOpaquePtr()));
}

TEST(ScopeReflectionTest, IsTemplate) {
  std::vector<Decl *> Decls;
  std::string code = R"(template<typename T>
                        class A{};

                        class C{
                          template<typename T>
                          int func(T t) {
                            return 0;
                          }
                        };

                        template<typename T>
                        T f(T t) {
                          return t;
                        }

                        void g() {} )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_TRUE(InterOp::IsTemplate(Decls[0]));
  EXPECT_FALSE(InterOp::IsTemplate(Decls[1]));
  EXPECT_TRUE(InterOp::IsTemplate(Decls[2]));
  EXPECT_FALSE(InterOp::IsTemplate(Decls[3]));
}

TEST(ScopeReflectionTest, IsAbstract) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    class A {};

    class B {
      virtual int f() = 0;
    };
  )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_FALSE(InterOp::IsAbstract(Decls[0]));
  EXPECT_TRUE(InterOp::IsAbstract(Decls[1]));
}

TEST(ScopeReflectionTest, IsEnum) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    enum Switch {
      OFF,
      ON
    };

    Switch s = Switch::OFF;

    int i = Switch::ON;
  )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_TRUE(InterOp::IsEnum(Decls[0]));
  EXPECT_FALSE(InterOp::IsEnum(Decls[1]));
  EXPECT_FALSE(InterOp::IsEnum(Decls[2]));
}

TEST(ScopeReflectionTest, IsVariable) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    int i;

    class C {
    public:
      int a;
      static int b;
    };
  )";

  GetAllTopLevelDecls(code, Decls);
  EXPECT_TRUE(InterOp::IsVariable(Decls[0]));
  EXPECT_FALSE(InterOp::IsVariable(Decls[1]));

  std::vector<Decl *> SubDecls;
  GetAllSubDecls(Decls[1], SubDecls);
  EXPECT_FALSE(InterOp::IsVariable(SubDecls[0]));
  EXPECT_FALSE(InterOp::IsVariable(SubDecls[1]));
  EXPECT_FALSE(InterOp::IsVariable(SubDecls[2]));
  EXPECT_TRUE(InterOp::IsVariable(SubDecls[3]));
}

TEST(ScopeReflectionTest, GetName) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N {} class C{}; int I; struct S;
                        enum E : int; union U{}; class Size4{int i;};
                        struct Size16 {short a; double b;};
                       )";
  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(InterOp::GetName(Decls[0]), "N");
  EXPECT_EQ(InterOp::GetName(Decls[1]), "C");
  EXPECT_EQ(InterOp::GetName(Decls[2]), "I");
  EXPECT_EQ(InterOp::GetName(Decls[3]), "S");
  EXPECT_EQ(InterOp::GetName(Decls[4]), "E");
  EXPECT_EQ(InterOp::GetName(Decls[5]), "U");
  EXPECT_EQ(InterOp::GetName(Decls[6]), "Size4");
  EXPECT_EQ(InterOp::GetName(Decls[7]), "Size16");
}

TEST(ScopeReflectionTest, GetUsingNamespaces) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    namespace abc {

    class C {};

    }
    using namespace std;
    using namespace abc;

    using I = int;
  )";

  GetAllTopLevelDecls(code, Decls);
  std::vector<void *> usingNamespaces;
  usingNamespaces = InterOp::GetUsingNamespaces(
          Decls[0]->getASTContext().getTranslationUnitDecl());

  EXPECT_EQ(InterOp::GetName(usingNamespaces[0]), "runtime");
  EXPECT_EQ(InterOp::GetName(usingNamespaces[1]), "std");
  EXPECT_EQ(InterOp::GetName(usingNamespaces[2]), "abc");
}
