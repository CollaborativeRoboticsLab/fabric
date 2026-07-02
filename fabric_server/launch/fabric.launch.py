from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    plan_file_name = LaunchConfiguration('filename')
    plan_file_path = LaunchConfiguration('plan_file_path')
    fabric_config = LaunchConfiguration('fabric_config')

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

    fabric = Node(
            package='fabric_server',
            namespace='',
            executable='fabric_server',
            name='fabric_server',
            output='screen',
            parameters=[fabric_config, 
                        {"plan_file_path": plan_file_path}]
        )
    
    return LaunchDescription([
        declare_plan_file_name,
        declare_plan_file_path,
        declare_fabric_config,
        fabric
    ])
