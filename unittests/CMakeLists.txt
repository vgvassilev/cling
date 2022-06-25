add_custom_target(ClingUnitTests)
set_target_properties(ClingUnitTests PROPERTIES FOLDER "Cling tests")

# add_clang_unittest(test_dirname file1.cpp file2.cpp)
#
# Will compile the list of files together and link against the clang
# Produces a binary named 'basename(test_dirname)'.
function(add_cling_unittest test_dirname)
  add_unittest(ClingUnitTests ${test_dirname} ${ARGN})
endfunction()

add_subdirectory(libInterOp)
