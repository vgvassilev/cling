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

TEST(TypeReflectionTest, IsEnumType) {
  std::vector<Decl *> Decls, SubDecls0, SubDecls1;
  std::string code =  R"(
    enum class E {
      a,
      b
    };

    enum F {
      c,
      d
    };

    E e;
    F f;

    auto g = E::a;
    auto h = c;
    )";

  GetAllTopLevelDecls(code, Decls);

  EXPECT_TRUE(InterOp::IsEnumType(InterOp::GetVariableType(Decls[2])));
  EXPECT_TRUE(InterOp::IsEnumType(InterOp::GetVariableType(Decls[3])));
  EXPECT_TRUE(InterOp::IsEnumType(InterOp::GetVariableType(Decls[4])));
  EXPECT_TRUE(InterOp::IsEnumType(InterOp::GetVariableType(Decls[5])));
}
