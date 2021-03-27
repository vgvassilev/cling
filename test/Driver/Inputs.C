//RUN: %cling "limits" "std::numeric_limits<int>::is_signed" "int i = 0;" "i" | %filecheck %s

//CHECK: (const {{_B|b}}ool) true
//CHECK: (int) 0
