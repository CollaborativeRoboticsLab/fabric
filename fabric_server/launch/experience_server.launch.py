#!/usr/bin/env python3

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import PathJoinSubstitution
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description() -> LaunchDescription:
    package_name = "experience_server"
    share_dir = get_package_share_directory(package_name)
    default_params = os.path.join(share_dir, "config", "experience_server.yaml")
    supervisor_launch_path = os.path.join(
        get_package_share_directory('supervisor_server'),
        'launch',
        'supervisor_server.launch.py',
    )

    params_arg = DeclareLaunchArgument(
        "params_file",
        default_value=default_params,
        description="YAML file with parameters for the experience server.",
    )

    consume_feedback_arg = DeclareLaunchArgument(
        "consume_feedback",
        default_value='true',
        description="Whether the experience server should consume saved user_feedback.json files on startup.",
    )

    rebuild_arg = DeclareLaunchArgument(
        "rebuild",
            default_value='false',
        description="Whether the experience server should rebuild persisted core knowledge on startup.",
    )

    user_feedback_root_dir_arg = DeclareLaunchArgument(
        "user_feedback_root_dir",
        default_value='',
        description="Optional root directory for per-execution user_feedback.json files.",
    )

    dispatch_terminal_success_only_arg = DeclareLaunchArgument(
        "dispatch_terminal_success_only",
        default_value='false',
        description="Only return selected plans when the scorer predicts terminal success.",
    )

    start_capabilities_arg = DeclareLaunchArgument(
        "start_capabilities",
        default_value='true',
        description="Whether to start the capabilities2 server in this launch.",
    )

    capabilities_params_arg = DeclareLaunchArgument(
        "capabilities_params_file",
        default_value=PathJoinSubstitution([
            FindPackageShare('capabilities2_server'),
            'config',
            'capabilities.yaml',
        ]),
        description="YAML file with parameters for the capabilities2 server.",
    )

    supervisor_params_arg = DeclareLaunchArgument(
        "supervisor_params_file",
        default_value=PathJoinSubstitution([
            FindPackageShare('supervisor_server'),
            'config',
            'supervisor_server_params.yaml',
        ]),
        description="YAML file with parameters for the supervisor server.",
    )

    capabilities_node = Node(
        package='capabilities2_server',
        executable='capabilities2_server_node',
        name='capabilities',
        output='screen',
        parameters=[LaunchConfiguration("capabilities_params_file")],
        arguments=['--ros-args', '--log-level', 'info'],
        condition=IfCondition(LaunchConfiguration("start_capabilities"))
    )

    supervisor_stack = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(supervisor_launch_path),
        launch_arguments={
            'params_file': LaunchConfiguration("supervisor_params_file"),
        }.items(),
    )

    server_node = Node(
        package=package_name,
        executable="experience_server_node",
        name="experience_server",
        output="screen",
        parameters=[
            LaunchConfiguration("params_file"),
            {
                "rebuild": LaunchConfiguration("rebuild"),
                "consume_feedback": LaunchConfiguration("consume_feedback"),
                "user_feedback_root_dir": LaunchConfiguration("user_feedback_root_dir"),
                "dispatch_terminal_success_only": LaunchConfiguration("dispatch_terminal_success_only"),
            },
        ],
    )

    return LaunchDescription([
        params_arg,
        consume_feedback_arg,
        rebuild_arg,
        user_feedback_root_dir_arg,
        dispatch_terminal_success_only_arg,
        start_capabilities_arg,
        capabilities_params_arg,
        supervisor_params_arg,
        capabilities_node,
        supervisor_stack,
        server_node,
    ])