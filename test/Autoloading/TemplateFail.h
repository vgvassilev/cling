namespace test {
  template<bool B, class T, class F>
  struct conditional { typedef T type; };
 
  template<class T, class F>
  struct conditional<false, T, F> { typedef F type; };
  
  template <typename _Tp> using example = typename conditional<true,int,float>::type;
}