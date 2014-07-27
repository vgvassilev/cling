namespace test { //implicit instantiation
  template<bool B, class T, class F>
  struct conditional { typedef T type; };
 
  template<class T, class F>
  struct conditional<false, T, F> { typedef F type; };
  
  template <typename _Tp> using example = typename conditional<true,int,float>::type;
}//end namespace test

namespace test { //nested name specifier
  class HasSubType {
  public:
    class SubType {};
  };
  HasSubType::SubType FunctionUsingSubtype(HasSubType::SubType s){return s;}
  extern HasSubType::SubType variable;//locale::id id
  
}//end namespace test

