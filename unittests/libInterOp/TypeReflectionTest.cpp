#include "Utils.h"

#include "cling/Interpreter/InterOp.h"

#include "gtest/gtest.h"

TEST(TypeReflectionTest, GetTypeAsString) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    namespace N {
    class C {};
    struct S {};
    }

    N::C c;

    N::S s;

    int i;
  )";

  GetAllTopLevelDecls(code, Decls);
  QualType QT1 = llvm::dyn_cast<VarDecl>(Decls[1])->getType();
  QualType QT2 = llvm::dyn_cast<VarDecl>(Decls[2])->getType();
  QualType QT3 = llvm::dyn_cast<VarDecl>(Decls[3])->getType();
  EXPECT_EQ(InterOp::GetTypeAsString(QT1.getAsOpaquePtr()),
          "N::C");
  EXPECT_EQ(InterOp::GetTypeAsString(QT2.getAsOpaquePtr()),
          "N::S");
  EXPECT_EQ(InterOp::GetTypeAsString(QT3.getAsOpaquePtr()), "int");
}

TEST(TypeReflectionTest, GetSizeOfType) {
  std::vector<Decl *> Decls;
  std::string code =  R"(
    struct S {
      int a;
      double b;
    };

    char ch;
    int n;
    double d;
    S s;
    )";

  GetAllTopLevelDecls(code, Decls);
  Sema *S = &Interp->getCI()->getSema();

  EXPECT_EQ(InterOp::GetSizeOfType(S, InterOp::GetVariableType(Decls[1])), 1);
  EXPECT_EQ(InterOp::GetSizeOfType(S, InterOp::GetVariableType(Decls[2])), 4);
  EXPECT_EQ(InterOp::GetSizeOfType(S, InterOp::GetVariableType(Decls[3])), 8);
  EXPECT_EQ(InterOp::GetSizeOfType(S, InterOp::GetVariableType(Decls[4])), 16);
}

TEST(TypeReflectionTest, GetCanonicalType) {
  std::vector<Decl *> Decls;
  std::string code =  R"(
    typedef int I;
    typedef double D;

    I n;
    D d;
    )";

  GetAllTopLevelDecls(code, Decls);
  Sema *S = &Interp->getCI()->getSema();

  auto D2 = InterOp::GetVariableType(Decls[2]);
  auto D3 = InterOp::GetVariableType(Decls[3]);

  EXPECT_EQ(InterOp::GetTypeAsString(D2), "I");
  EXPECT_EQ(InterOp::GetTypeAsString(InterOp::GetCanonicalType(D2)), "int");
  EXPECT_EQ(InterOp::GetTypeAsString(D3), "D");
  EXPECT_EQ(InterOp::GetTypeAsString(InterOp::GetCanonicalType(D3)), "double");
}

TEST(TypeReflectionTest, GetType) {
  Interp.reset();
  Interp = createInterpreter();
  Sema *S = &Interp->getCI()->getSema();

  std::string code =  R"(
    #include <string>
    )";
  
  Interp->declare(code);

  EXPECT_EQ(InterOp::GetTypeAsString(InterOp::GetType(S, "int")), "int");
  EXPECT_EQ(InterOp::GetTypeAsString(InterOp::GetType(S, "double")), "double");
  EXPECT_EQ(InterOp::GetTypeAsString(InterOp::GetType(S, "std::string")), "std::string");
}

TEST(TypeReflectionTest, GetTypeFromScope) {
  std::vector<Decl *> Decls;

  std::string code =  R"(
    class C {};
    struct S {};
    )";
  
  GetAllTopLevelDecls(code, Decls);

  EXPECT_EQ(InterOp::GetTypeAsString(InterOp::GetTypeFromScope(Decls[0])), "class C");
  EXPECT_EQ(InterOp::GetTypeAsString(InterOp::GetTypeFromScope(Decls[1])), "struct S");
}