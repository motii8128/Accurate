#ifndef REALSENSE_UTILS_HPP_
#define REALSENSE_UTILS_HPP_

#include <librealsense2/rs.hpp>
#include <opencv2/opencv.hpp>

namespace rs_d455_ros2
{
    struct DeviceInfo
    {
        std::string name;
        std::string firmware_version;
        std::string usb_type;
        std::string product_line;
    };

    class RealSense
    {
        public:
        RealSense();
        ~RealSense();

        void getColorFrame(cv::Mat& color_image);
        DeviceInfo getDeviceInfo();

        private:
        rs2::context ctx_;
        rs2::pipeline pipe_;
        rs2::device_list devices_;
        const uint8_t width_ = 640;
        const uint8_t height_ = 480;
        const uint8_t fps_ = 30;
    };

}

#endif