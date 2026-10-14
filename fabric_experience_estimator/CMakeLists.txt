cmake_minimum_required(VERSION 3.8)
project(fabric_experience_estimator)


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

include_directories(
  include
)

add_library(${PROJECT_NAME} SHARED
  src/estimator.cpp
)

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  pluginlib
  fabric_base
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

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)

  ament_add_gtest(test_estimator_plugins test/test_estimator_plugins.cpp)
  if(TARGET test_estimator_plugins)
    ament_target_dependencies(test_estimator_plugins
      fabric_base
      rclcpp
    )
    target_link_libraries(test_estimator_plugins ${PROJECT_NAME})
  endif()

  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  set(ament_cmake_uncrustify_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
