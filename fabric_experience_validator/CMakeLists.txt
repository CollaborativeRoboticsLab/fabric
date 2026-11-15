cmake_minimum_required(VERSION 3.8)
project(fabric_experience_validator)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(fabric_base REQUIRED)
find_package(pluginlib REQUIRED)
find_package(prompt_msgs REQUIRED)
find_package(rclcpp REQUIRED)

include_directories(
  include
)

add_library(${PROJECT_NAME} SHARED
  src/validator.cpp
)

ament_target_dependencies(${PROJECT_NAME}
  fabric_base
  pluginlib
  prompt_msgs
  rclcpp
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

  ament_add_gtest(test_validation_plugins test/test_validation_plugins.cpp)
  if(TARGET test_validation_plugins)
    ament_target_dependencies(test_validation_plugins
      fabric_base
      prompt_msgs
      rclcpp
    )
    target_link_libraries(test_validation_plugins ${PROJECT_NAME})
  endif()

  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  set(ament_cmake_uncrustify_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()