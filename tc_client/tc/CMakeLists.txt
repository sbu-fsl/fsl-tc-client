add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

add_definitions(-std=c++11)

set(tc_SRC
  tc_impl.c
)

set(GTEST "$ENV{GOOGLE_TEST}")

include_directories(
  "${GTEST}"
  "${GTEST}/include"
)

set(GMOCK "$ENV{GOOGLE_MOCK}")
include_directories(
  "${GMOCK}"
  "${GMOCK}/include"
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
include_directories("${CMAKE_SOURCE_DIR}")

add_library(gtest "${GTEST}/src/gtest-all.cc")
add_library(gtest_main "${GTEST}/src/gtest_main.cc")
add_library(gmock "${GMOCK}/src/gmock-all.cc")
add_library(gmock_main "${GMOCK}/src/gmock_main.cc")

set(test_LIB
  pthread
  gtest
  gtest_main
  gmock
  gmock_main
)

function (add_unittest TestName LibName)
  add_executable(${TestName} "${CMAKE_CURRENT_SOURCE_DIR}/${TestName}.cpp")
  target_link_libraries(${TestName} ${base_LIBS} ${boost_LIBS} ${test_LIB} ${LibName})
  add_test(NAME ${TestName} COMMAND ${TestName})
endfunction (add_unittest)

add_library(tc_impl STATIC ${tc_SRC})
target_link_libraries(tc_impl tc_impl_posix log config_parsing tc_util)

add_unittest(tc_test tc_impl)