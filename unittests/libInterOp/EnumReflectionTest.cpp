#include "Utils.h"

#include "cling/Interpreter/InterOp.h"

#include "gtest/gtest.h"

TEST(ScopeReflectionTest, IsEnum) {
  std::vector<Decl *> Decls, SubDecls;
  std::string code = R"(
    enum Switch {
      OFF,
      ON
    };

    Switch s = Switch::OFF;

    int i = Switch::ON;
  )";

  GetAllTopLevelDecls(code, Decls);
  GetAllSubDecls(Decls[0], SubDecls);
  EXPECT_TRUE(InterOp::IsEnum(Decls[0]));
  EXPECT_FALSE(InterOp::IsEnum(Decls[1]));
  EXPECT_FALSE(InterOp::IsEnum(Decls[2]));
  EXPECT_TRUE(InterOp::IsEnum(SubDecls[0]));
  EXPECT_TRUE(InterOp::IsEnum(SubDecls[1]));
}

TEST(EnumReflectionTest, IsEnumType) {
  std::vector<Decl *> Decls;
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
