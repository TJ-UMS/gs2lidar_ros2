#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <tf2_ros/transform_broadcaster.h>
#include "CYdLidar.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>

using namespace std;
using namespace ydlidar;

#if defined(_MSC_VER)
#pragma comment(lib, "ydlidar_driver.lib")
#endif

#define DEFAULT_TIMEOUT 2000

class LanserScannerNode : public rclcpp::Node
{
public:
    LanserScannerNode(int argc, char **argv) : Node("lanser_scanner")
    {
        ydlidar::init(argc, argv);
        std::map<std::string, std::string> ports =
            ydlidar::YDlidarDriver::lidarPortList();
        std::map<std::string, std::string>::iterator it;
        int id = 0;
        for (it = ports.begin(); it != ports.end(); it++)
        {
            printf("%d. %s\n", id, it->first.c_str());
            id++;
        }
        // 创建发布者
        publisher_ = create_publisher<sensor_msgs::msg::LaserScan>("/scan", 10);

        // 创建定时器
        // timer_ = create_wall_timer(500ms, std::bind(&LanserScannerNode::onTimer, this));

        port = "/dev/ttyUSB0";

        // 创建串口
        result_t ret = initRecvPort(&_serial, port, 921600);
        if (!IS_OK(ret))
        {
            fprintf(stderr, "Failed to initialize the receiving port!\n");
            fflush(stderr);
            // return -1;
        }

        initLanser();
        serialReader();
    }

private:
    result_t initRecvPort(serial::Serial **_serial, std::string port_name, int baudrate)
    {
        if (!(*_serial))
        {
            *_serial = new serial::Serial(port_name, baudrate,
                                          serial::Timeout::simpleTimeout(DEFAULT_TIMEOUT));
        }

        if ((*_serial)->isOpen())
        {
            return RESULT_OK;
        }
        if (!(*_serial)->open())
        {
            return RESULT_FAIL;
        }

        return RESULT_OK;
    }
    void serialReader()
    {
        while (ret && ydlidar::ok())
        {
            bool hardError;
            LaserScan scan;
            uint8_t *header = reinterpret_cast<uint8_t *>(&scan);
            size_t size = sizeof(scan);

            if (laser.doProcessSimple(scan, hardError))
            {
                fprintf(stdout, "Scan received[%lu]: %u ranges is [%f]Hz\n",
                        scan.stamp,
                        (unsigned int)scan.points.size(),
                        1.0 / scan.config.scan_time);
                fflush(stdout);
                auto scan_msg = std::make_shared<sensor_msgs::msg::LaserScan>();
                scan_msg->header.stamp.sec = RCL_NS_TO_S(scan.stamp);
                scan_msg->header.stamp.nanosec = scan.stamp - RCL_S_TO_NS(scan_msg->header.stamp.sec);
                scan_msg->header.frame_id = "laser_frame";

                scan_msg->angle_min = -0.9163;
                scan_msg->angle_max = 0.9163;
                scan_msg->angle_increment = 0.0105;
                scan_msg->scan_time = scan.config.scan_time;
                scan_msg->time_increment = scan.config.time_increment;
                scan_msg->range_min = 0.001;
                scan_msg->range_max = 1;
                scan_msg->ranges.resize(scan.points.size());
                scan_msg->intensities.resize(scan.points.size());
                // 0 - 80 左相机 0 到52.5   81 - 159 右相机  -52.5 到0
                for (size_t i = 81; i < scan.points.size(); i++)
                {

                    scan_msg->ranges[i - 81] = scan.points[i].range / 1000;
                    scan_msg->intensities[i - 81] = scan.points[i].intensity / 1000;
                }
                for (size_t i = 0; i <= 80; i++)
                {

                    scan_msg->ranges[i + 80] = scan.points[i].range / 1000;
                    scan_msg->intensities[i + 80] = scan.points[i].intensity / 1000;
                }

                publisher_->publish(*scan_msg);
            }
        }
    }
    void initLanser()
    {
        laser.setSerialPort(port);
        //<! lidar baudrate
        laser.setSerialBaudrate(921600);

        //<! fixed angle resolution
        laser.setFixedResolution(false);
        //<! rotate 180
        laser.setReversion(false); // rotate 180
        //<! Counterclockwise
        laser.setInverted(false);     // ccw
        laser.setAutoReconnect(true); // hot plug
        //<! one-way communication
        laser.setSingleChannel(false);

        //<! tof lidar
        laser.setLidarType(isTOFLidar ? TYPE_TOF : TYPE_TRIANGLE);
        // unit: °
        laser.setMaxAngle(180);
        laser.setMinAngle(-180);

        // unit: m
        laser.setMinRange(30);
        laser.setMaxRange(1000);

        // unit: Hz
        laser.setScanFrequency(frequency);
        std::vector<float> ignore_array;
        ignore_array.clear();
        laser.setIgnoreArray(ignore_array);
        bool isIntensity = true;
        laser.setIntensity(isIntensity);

        ret = laser.initialize();

        if (ret)
        {
            ret = laser.turnOn();
        }
    }

    rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr publisher_;
    serial::Serial *_serial = nullptr;
    CYdLidar laser;
    std::string port;
    float frequency = 8.0;
    bool ret = false;
};
void tfSend(rclcpp::Node::SharedPtr node_ptr)
{
    // Define the transform parameters
    double x = 0.0;
    double y = 0.0;
    double z = 0.02;
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    double qw = 1.0;

    std::string parent_frame_id = "base_link";
    std::string child_frame_id = "laser_frame";

    // Create a transform broadcaster
    tf2_ros::TransformBroadcaster broadcaster(node_ptr);

    // Define the transform message
    geometry_msgs::msg::TransformStamped transform;
    transform.header.stamp = node_ptr->now();
    transform.header.frame_id = parent_frame_id;
    transform.child_frame_id = child_frame_id;

    // Set the translation and rotation of the transform
    transform.transform.translation.x = x;
    transform.transform.translation.y = y;
    transform.transform.translation.z = z;
    transform.transform.rotation.w = qw;
    transform.transform.rotation.x = qx;
    transform.transform.rotation.y = qy;
    transform.transform.rotation.z = qz;
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<LanserScannerNode>(argc, argv);
    tfSend(node);
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}