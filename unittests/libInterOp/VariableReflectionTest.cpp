
#include "Utils.h"

#include "cling/Interpreter/InterOp.h"

#include "gtest/gtest.h"


TEST(VariableReflectionTest, GetDatamembers) {
  std::vector<Decl*> Decls;
  std::string code = R"(
    class C {
    public:
      int a;
      static int b;
    private:
      int c;
      static int d;
    protected:
      int e;
      static int f;
    };
    )";

  GetAllTopLevelDecls(code, Decls);
  auto datamembers = InterOp::GetDatamembers(Decls[0]);

  EXPECT_EQ(InterOp::GetCompleteName(datamembers[0]), "C::a");
  EXPECT_EQ(InterOp::GetCompleteName(datamembers[1]), "C::c");
  EXPECT_EQ(InterOp::GetCompleteName(datamembers[2]), "C::e");
  EXPECT_EQ(datamembers.size(), 3);
}

TEST(VariableReflectionTest, GetVariableTypeAsString) {
  std::vector<Decl*> Decls;
  std::string code = R"(
    class C {};

    template<typename T>
    class E {};

    int a;
    char b;
    C c;
    C *d;
    E<int> e;
    E<int> *f;
    )";

  GetAllTopLevelDecls(code, Decls);

  EXPECT_EQ(InterOp::GetVariableTypeAsString(Decls[2]), "int");
  EXPECT_EQ(InterOp::GetVariableTypeAsString(Decls[3]), "char");
  EXPECT_EQ(InterOp::GetVariableTypeAsString(Decls[4]), "class C");
  EXPECT_EQ(InterOp::GetVariableTypeAsString(Decls[5]), "class C *");
  EXPECT_EQ(InterOp::GetVariableTypeAsString(Decls[6]), "E<int>");
  EXPECT_EQ(InterOp::GetVariableTypeAsString(Decls[7]), "E<int> *");
}

TEST(VariableReflectionTest, GetVariableOffset) {
  std::vector<Decl *> Decls;
  std::string code = R"(
    int a;
    class C {
    public:
      int a;
      double b;
      int *c;
      int d;
    };
    )";

  class {
  public:
    int a;
    double b;
    int *c;
    int d;
  } c;


  GetAllTopLevelDecls(code, Decls);
  auto datamembers = InterOp::GetDatamembers(Decls[1]);

  EXPECT_FALSE(InterOp::GetVariableOffset(Interp.get(), Decls[0])
          == (intptr_t) 0);

  EXPECT_EQ(InterOp::GetVariableOffset(Interp.get(), datamembers[0]),
          0);
  EXPECT_EQ(InterOp::GetVariableOffset(Interp.get(), datamembers[1]),
          ((unsigned long) &(c.b)) - ((unsigned long) &(c.a)));
  EXPECT_EQ(InterOp::GetVariableOffset(Interp.get(), datamembers[2]),
          ((unsigned long) &(c.c)) - ((unsigned long) &(c.a)));
  EXPECT_EQ(InterOp::GetVariableOffset(Interp.get(), datamembers[3]),
          ((unsigned long) &(c.d)) - ((unsigned long) &(c.a)));
}

TEST(VariableReflectionTest, IsPublicVariable) {
  std::vector<Decl *> Decls, SubDecls;
  std::string code = R"(
    class C {
    public:
      int a;
    private:
      int b;
    protected:
      int c;
    };
    )";

  GetAllTopLevelDecls(code, Decls);
  GetAllSubDecls(Decls[0], SubDecls);

  EXPECT_TRUE(InterOp::IsPublicVariable(SubDecls[2]));
  EXPECT_FALSE(InterOp::IsPublicVariable(SubDecls[4]));
  EXPECT_FALSE(InterOp::IsPublicVariable(SubDecls[6]));
}

TEST(VariableReflectionTest, IsProtectedVariable) {
  std::vector<Decl *> Decls, SubDecls;
  std::string code = R"(
    class C {
    public:
      int a;
    private:
      int b;
    protected:
      int c;
    };
    )";

  GetAllTopLevelDecls(code, Decls);
  GetAllSubDecls(Decls[0], SubDecls);

  EXPECT_FALSE(InterOp::IsProtectedVariable(SubDecls[2]));
  EXPECT_FALSE(InterOp::IsProtectedVariable(SubDecls[4]));
  EXPECT_TRUE(InterOp::IsProtectedVariable(SubDecls[6]));
}

TEST(VariableReflectionTest, IsPrivateVariable) {
  std::vector<Decl *> Decls, SubDecls;
  std::string code = R"(
    class C {
    public:
      int a;
    private:
      int b;
    protected:
      int c;
    };
    )";

  GetAllTopLevelDecls(code, Decls);
  GetAllSubDecls(Decls[0], SubDecls);

  EXPECT_FALSE(InterOp::IsPrivateVariable(SubDecls[2]));
  EXPECT_TRUE(InterOp::IsPrivateVariable(SubDecls[4]));
  EXPECT_FALSE(InterOp::IsPrivateVariable(SubDecls[6]));
}
