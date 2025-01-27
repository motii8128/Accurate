#include "motiodom/motiodom.hpp"

namespace motiodom
{
    MotiOdom::MotiOdom(const rclcpp::NodeOptions& option) : Node("MotiOdom", option)
    {
        rclcpp::QoS qos_settings = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();
        imu_subscriber_ = this->create_subscription<sensor_msgs::msg::Imu>(
            "/imu",
            qos_settings,
            std::bind(&MotiOdom::imu_callback, this, _1)
        );

        ydlidar_publisher_ = this->create_publisher<sensor_msgs::msg::PointCloud>("/lidar_scan", rclcpp::SystemDefaultsQoS());

        odom_publisher_ = this->create_publisher<nav_msgs::msg::Odometry>("/odom", rclcpp::SystemDefaultsQoS());

        occupancy_grid_publisher_ = this->create_publisher<nav_msgs::msg::OccupancyGrid>("/map", rclcpp::SystemDefaultsQoS());


        RCLCPP_INFO(this->get_logger(), "Get Parameters");
        this->declare_parameter("enable_reverse", false);
        this->get_parameter("enable_reverse", enable_reverse_);
        this->declare_parameter("icp_max_iter", 10);
        this->get_parameter("icp_max_iter", icp_max_iter_);
        this->declare_parameter("icp_threshold", 0.01);
        this->get_parameter("icp_threshold", icp_threshold_);

        imu_posture_ = Quat(1.0, 0.0, 0.0, 0.0);
        imu_posture_euler_ = Vec3(0.0, 0.0, 0.0);
        set_source_ = false;


        RCLCPP_INFO(this->get_logger(), "Initialize IMU EKF.");
        imu_ekf_ = std::make_shared<ImuPostureEKF>();

        RCLCPP_INFO(this->get_logger(), "Initialize PointCloud ICP");
        icp_ = std::make_shared<ICP>(icp_max_iter_, icp_threshold_);

        RCLCPP_INFO(this->get_logger(), "Initialize YDLidarDriver");
        ydlidar_ = std::make_shared<YDLidarDriver>(230400, enable_reverse_);


        if(!ydlidar_->startLidar())
        {
            RCLCPP_ERROR(this->get_logger(), "Failed to initialize YD Lidar.");
            RCLCPP_ERROR(this->get_logger(), "LIDAR_ERROR : %s", ydlidar_->getError());
        }
        else
        {
            RCLCPP_INFO(this->get_logger(), "Initialize YD Lidar. pitch angle : %lf", ydlidar_->getPitchAngle());

            timer_ = this->create_wall_timer(10ms, std::bind(&MotiOdom::timer_callback, this));
            RCLCPP_INFO(this->get_logger(), "Start MotiOdom!!");
        }
    }

    void MotiOdom::imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg)
    {
        auto current_time = this->now();

        if(prev_imu_callback_time.nanoseconds() > 0)
        {
            rclcpp::Duration delta_time = current_time - prev_imu_callback_time;
            auto dt = delta_time.seconds();

            const auto angular_velocity = Vec3(
                degree2radian(msg->angular_velocity.x),
                degree2radian(msg->angular_velocity.y),
                degree2radian(msg->angular_velocity.z)
            );

            const auto linear_accel =Vec3(
                msg->linear_acceleration.x,
                msg->linear_acceleration.y,
                msg->linear_acceleration.z
            );

            const auto posture = imu_ekf_->estimate(angular_velocity, linear_accel, dt);

            imu_posture_ = euler2quat(posture);
            imu_posture_euler_ = posture;
        }

        prev_imu_callback_time = current_time;
    }

    void MotiOdom::timer_callback()
    {
        if(ydlidar_->Scan())
        {
            auto odom = nav_msgs::msg::Odometry();
            odom.header.frame_id = "map";
            odom.child_frame_id = "base_link";
            odom.pose.pose.orientation.w = imu_posture_.w();
            odom.pose.pose.orientation.x = imu_posture_.x();
            odom.pose.pose.orientation.y = imu_posture_.y();
            odom.pose.pose.orientation.z = imu_posture_.z();
            const auto scanPoints = ydlidar_->getScanPoints();
            const auto rosPointCloudMsg = toROSMsg(scanPoints);

            if(!set_source_)
            {
                icp_->setSource(scanPoints);
                set_source_ = true;
            }

            const auto t = icp_->integrate(scanPoints, imu_posture_euler_.z()/2.0);
            odom.pose.pose.position.x = t.x();
            odom.pose.pose.position.y = t.y();
            // RCLCPP_INFO(this->get_logger(), "x:%lf,y:%lf", t.x(), t.y());

            odom_publisher_->publish(odom);
            ydlidar_publisher_->publish(rosPointCloudMsg);
        }
        else
        {
            RCLCPP_ERROR(this->get_logger(), "Failed to Scan");
            ydlidar_->closeLidar();
            RCLCPP_ERROR(this->get_logger(), "ShutDown YDLidar");
        }
    }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(motiodom::MotiOdom)