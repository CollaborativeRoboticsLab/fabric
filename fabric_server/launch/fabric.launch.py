import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import ExecuteProcess
from launch.actions import IncludeLaunchDescription
from launch.actions import RegisterEventHandler
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessExit
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch.substitutions import PythonExpression
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    plan_file_name = LaunchConfiguration('filename')
    plan_file_path = LaunchConfiguration('plan_file_path')
    fabric_config = LaunchConfiguration('fabric_config')
    capabilities_config = LaunchConfiguration('capabilities_config')
    start_experience_stack = LaunchConfiguration('start_experience_stack')
    experience_only = LaunchConfiguration('experience_only')
    experience_params_file = LaunchConfiguration('experience_params_file')
    supervisor_params_file = LaunchConfiguration('supervisor_params_file')
    start_prompt_tools = LaunchConfiguration('start_prompt_tools')
    prompt_tools_params_file = LaunchConfiguration('prompt_tools_params_file')

    experience_stack_enabled = PythonExpression([
        '"', start_experience_stack, '" == "true" or "', experience_only, '" == "true"'
    ])
    fabric_with_experience_enabled = PythonExpression([
        '"', start_experience_stack, '" == "true" and "', experience_only, '" != "true"'
    ])
    fabric_direct_enabled = PythonExpression([
        '"', start_experience_stack, '" != "true" and "', experience_only, '" != "true"'
    ])

    declare_plan_file_name = DeclareLaunchArgument(
        'filename',
        default_value='default.xml',
        description='Name of the plan file (used if plan_file_path is not overridden)'
    )

    declare_plan_file_path = DeclareLaunchArgument(
        'plan_file_path',
        default_value=PathJoinSubstitution([
            FindPackageShare('fabric_server'),
            'plans',
            plan_file_name,
        ]),
        description='Full path to the plan XML'
    )

    declare_fabric_config = DeclareLaunchArgument(
        'fabric_config',
        default_value=PathJoinSubstitution([
            FindPackageShare('fabric_server'),
            'config',
            'fabric.yaml',
        ]),
        description='Path to fabric.yaml'
    )

    declare_capabilities_config = DeclareLaunchArgument(
        'capabilities_config',
        default_value=PathJoinSubstitution([
            FindPackageShare('capabilities2_server'),
            'config',
            'capabilities.yaml',
        ]),
        description='Path to capabilities.yaml'
    )

    declare_start_experience_stack = DeclareLaunchArgument(
        'start_experience_stack',
        default_value='false',
        description='Whether to start the experience and supervisor stack alongside fabric'
    )

    declare_experience_only = DeclareLaunchArgument(
        'experience_only',
        default_value='false',
        description='Whether to start Experience, rebuild CoreGraphRag, and skip starting fabric'
    )

    declare_start_prompt_tools = DeclareLaunchArgument(
        'start_prompt_tools',
        default_value='true',
        description='Whether to start prompt_bridge so prompt-based plan generation is available; set false to disable it'
    )

    declare_experience_params_file = DeclareLaunchArgument(
        'experience_params_file',
        default_value=PathJoinSubstitution([
            FindPackageShare('experience_server'),
            'config',
            'experience_server.yaml',
        ]),
        description='Path to the experience server parameter file'
    )

    declare_supervisor_params_file = DeclareLaunchArgument(
        'supervisor_params_file',
        default_value=PathJoinSubstitution([
            FindPackageShare('supervisor_server'),
            'config',
            'supervisor_server_params.yaml',
        ]),
        description='Path to the supervisor server parameter file'
    )

    declare_prompt_tools_params_file = DeclareLaunchArgument(
        'prompt_tools_params_file',
        default_value=PathJoinSubstitution([
            FindPackageShare('prompt_bridge'),
            'config',
            'prompt_bridge.yaml',
        ]),
        description='Path to the prompt_bridge parameter file'
    )

    capabilities2 = Node(
            package='capabilities2_server',
            namespace='',
            executable='capabilities2_server_node',
            name='capabilities',
            output='screen',
            parameters=[capabilities_config],
            arguments=['--ros-args', '--log-level', 'info']
        )

    fabric = Node(
            package='fabric_server',
            namespace='',
            executable='fabric_server',
            name='fabric_server',
            output='screen',
            parameters=[fabric_config, 
                        {"plan_file_path": plan_file_path}]
        )

    experience_launch_path = os.path.join(
        get_package_share_directory('experience_server'),
        'launch',
        'experience_server.launch.py',
    )

    prompt_tools = Node(
            package='prompt_bridge',
            namespace='',
            executable='prompt_bridge_node',
            name='prompt_bridge',
            output='screen',
            parameters=[prompt_tools_params_file],
            arguments=['--ros-args', '--log-level', 'info'],
            condition=IfCondition(start_prompt_tools)
        )

    wait_for_readiness_script = os.path.join(
        get_package_share_directory('fabric_server'),
        'launch',
        'wait_for_readiness.py',
    )

    wait_for_capabilities_ready = ExecuteProcess(
        cmd=[
            'python3',
            wait_for_readiness_script,
            '--mode',
            'trigger',
            '--service-name',
            '/capabilities/ready',
        ],
        output='screen',
        condition=IfCondition(experience_stack_enabled),
    )

    experience_stack = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(experience_launch_path),
        launch_arguments={
            'params_file': experience_params_file,
            'supervisor_params_file': supervisor_params_file,
            'start_capabilities': 'false',
            'rebuild': experience_only,
        }.items(),
        condition=IfCondition(experience_stack_enabled),
    )

    wait_for_experience_ready = ExecuteProcess(
        cmd=[
            'python3',
            wait_for_readiness_script,
            '--mode',
            'experience',
            '--service-name',
            '/experience/ready',
        ],
        output='screen',
        condition=IfCondition(fabric_with_experience_enabled),
    )

    wait_for_experience_after_capabilities = RegisterEventHandler(
        OnProcessExit(
            target_action=wait_for_capabilities_ready,
            on_exit=[wait_for_experience_ready],
        )
    )

    fabric_with_experience = Node(
        package='fabric_server',
        namespace='',
        executable='fabric_server',
        name='fabric_server',
        output='screen',
        parameters=[fabric_config,
                    {"plan_file_path": plan_file_path,
                     "require_experience_ready": True}],
        condition=IfCondition(fabric_with_experience_enabled)
    )

    start_fabric_after_capabilities = RegisterEventHandler(
        OnProcessExit(
            target_action=wait_for_capabilities_ready,
            on_exit=[fabric_with_experience],
        )
    )

    fabric_direct = Node(
            package='fabric_server',
            namespace='',
            executable='fabric_server',
            name='fabric_server',
            output='screen',
            parameters=[fabric_config,
                        {"plan_file_path": plan_file_path}],
            condition=IfCondition(fabric_direct_enabled)
        )

    return LaunchDescription([
        declare_plan_file_name,
        declare_plan_file_path,
        declare_fabric_config,
        declare_capabilities_config,
        declare_start_experience_stack,
        declare_experience_only,
        declare_start_prompt_tools,
        declare_experience_params_file,
        declare_supervisor_params_file,
        declare_prompt_tools_params_file,
        capabilities2,
        prompt_tools,
        experience_stack,
        wait_for_capabilities_ready,
        wait_for_experience_after_capabilities,
        start_fabric_after_capabilities,
        fabric_direct,
    ])
