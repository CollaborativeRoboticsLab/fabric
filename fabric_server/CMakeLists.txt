cmake_minimum_required(VERSION 3.8)
project(fabric_server)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(bondcpp REQUIRED)
find_package(capabilities2_msgs REQUIRED)
find_package(fabric_base REQUIRED)
find_package(fabric_msgs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_srvs REQUIRED)
find_package(supervisor_msgs REQUIRED)
find_package(tinyxml2_vendor REQUIRED)
find_package(TinyXML2 REQUIRED)
find_package(yaml-cpp REQUIRED)

pkg_check_modules(UUID REQUIRED uuid)

include_directories(
  include
  ${UUID_INCLUDE_DIRS}
)

ament_python_install_package(${PROJECT_NAME})

set(FABRIC_SERVER_DEPENDENCIES
  ament_index_cpp
  bondcpp
  capabilities2_msgs
  fabric_base
  fabric_msgs
  pluginlib
  rclcpp
  rclcpp_action
  rclcpp_components
  std_srvs
  supervisor_msgs
  TinyXML2
)

set(FABRIC_SERVER_LINK_LIBRARIES
  yaml-cpp
  ${UUID_LIBRARIES}
)

add_executable(${PROJECT_NAME}
  src/server_node.cpp
)

ament_target_dependencies(${PROJECT_NAME}
  ${FABRIC_SERVER_DEPENDENCIES}
)

target_link_libraries(${PROJECT_NAME}
  ${FABRIC_SERVER_LINK_LIBRARIES}
)

add_library(${PROJECT_NAME}_comp SHARED
  src/server_component.cpp
)

ament_target_dependencies(${PROJECT_NAME}_comp
  ${FABRIC_SERVER_DEPENDENCIES}
)

target_link_libraries(${PROJECT_NAME}_comp
  ${FABRIC_SERVER_LINK_LIBRARIES}
)

rclcpp_components_register_node(${PROJECT_NAME}_comp
  PLUGIN "fabric::Fabric"
  EXECUTABLE fabric_component
)

install(TARGETS
  ${PROJECT_NAME}
  ${PROJECT_NAME}_comp
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)

install(DIRECTORY plans
  DESTINATION share/${PROJECT_NAME}
)

install(PROGRAMS
  scripts/export_core_graph_debug.py
  scripts/export_prompt_decision_support.py
  scripts/export_reasoning_outcomes.py
  scripts/export_retrieval_prompt_audit.py
  scripts/export_terminal_success_analysis.py
  scripts/get_scoring_epoch.py
  scripts/get_plan_score_explanation.py
  scripts/ingest_plan_feedback.py
  scripts/replay_plan_score.py
  scripts/record_plan_feedback.py
  scripts/record_scoring_epoch.py
  scripts/summarize_planner_prompt_audit.py
  scripts/verify_plan_score_explanation.py
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  find_package(ament_lint_auto REQUIRED)

  ament_add_gtest(test_score_explanation_adapter test/test_score_explanation_adapter.cpp)
  if(TARGET test_score_explanation_adapter)
    ament_target_dependencies(test_score_explanation_adapter
      fabric_base
      fabric_msgs
      rclcpp
    )
  endif()

  ament_add_pytest_test(test_export_retrieval_prompt_audit
    test/test_export_retrieval_prompt_audit.py
  )

  ament_add_pytest_test(test_export_prompt_decision_support
    test/test_export_prompt_decision_support.py
  )

  ament_add_pytest_test(test_summarize_planner_prompt_audit
    test/test_summarize_planner_prompt_audit.py
  )

  ament_add_pytest_test(test_export_reasoning_outcomes
    test/test_export_reasoning_outcomes.py
  )

  ament_add_pytest_test(test_export_terminal_success_analysis
    test/test_export_terminal_success_analysis.py
  )

  ament_add_pytest_test(test_verify_plan_score_explanation
    test/test_verify_plan_score_explanation.py
  )

  ament_add_pytest_test(test_python_entrypoints_smoke
    test/test_python_entrypoints_smoke.py
  )

  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  set(ament_cmake_flake8_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/setup.cfg")
  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_comp)
ament_package()

