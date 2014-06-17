namespace test {
template <bool B, class T, class F> struct __attribute__((annotate("TemplateFail.h")))  conditional;
;
template <typename _Tp> using example = typename conditional<true, int, float>::type;
}
;
