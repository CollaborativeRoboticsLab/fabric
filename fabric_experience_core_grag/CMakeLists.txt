cmake_minimum_required(VERSION 3.8)
project(fabric_experience_core_grag)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(fabric_base REQUIRED)
find_package(pluginlib REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(prompt_msgs REQUIRED)

find_package(PkgConfig)
pkg_check_modules(UUID REQUIRED uuid)

# Find SQLite3
find_package(SQLite3)

include_directories(
  include
  ${UUID_INCLUDE_DIRS}
)

add_library(${PROJECT_NAME} SHARED
  src/core.cpp
)

target_link_libraries(${PROJECT_NAME}
  Eigen3::Eigen
  SQLite::SQLite3
  ${UUID_LIBRARIES}
  nlohmann_json::nlohmann_json
)

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  pluginlib
  fabric_base
  prompt_msgs
)

pluginlib_export_plugin_description_file(fabric_base plugins.xml)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)

  ament_add_gtest(test_core_graphrag_utils test/test_core_graphrag_utils.cpp)
  if(TARGET test_core_graphrag_utils)
    ament_target_dependencies(test_core_graphrag_utils
      rclcpp
      fabric_base
      prompt_msgs
    )
    target_link_libraries(test_core_graphrag_utils
      ${PROJECT_NAME}
      Eigen3::Eigen
      SQLite::SQLite3
      ${UUID_LIBRARIES}
      nlohmann_json::nlohmann_json
    )
  endif()

  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  set(ament_cmake_uncrustify_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_package()
