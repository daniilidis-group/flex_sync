/* -*-c++-*--------------------------------------------------------------------
 * 2018 Bernd Pfrommer bernd.pfrommer@gmail.com
 */
#include "flex_sync/approx_sync.h"

#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>

#include <vector>
#include <string>

using std::vector;
using std::string;

void callback(
  const std::vector<sensor_msgs::Image::ConstPtr> &im,
  const std::vector<sensor_msgs::CameraInfo::ConstPtr> &ci) {
 
  std::cout << "callback: " << std::endl <<
    " " << im[0]->header.stamp << " " << ci[0]->header.stamp << std::endl << 
    " " << im[1]->header.stamp << " " << ci[1]->header.stamp << std::endl <<
    " " << im[2]->header.stamp << " " << ci[2]->header.stamp << std::endl;
}

int main(int argc, char** argv) {
  ros::init(argc, argv, "approx_sync_test");
  ros::NodeHandle pnh("~");

  try {
    const vector<vector<string>> topics =
      {{"/left_tof/stream/1/mono8",
       "/right_tof/stream/1/mono8",
        "/t265/fisheye1/image_raw"},
       {"/left_tof/depth/camera_info",
        "/right_tof/depth/camera_info",
        "/t265/fisheye1/camera_info"}};
    vector<string> bag_topics;
    for (const auto &tv: topics) {
      bag_topics.insert(bag_topics.end(), tv.begin(), tv.end());
    }
    
    // Use time synchronizer to make sure we get properly synchronized images
    rosbag::Bag bag;
    std::string bagName;
    pnh.param<std::string>("bag", bagName, "test.bag");

    bag.open(bagName, rosbag::bagmode::Read);
    rosbag::View view(bag, rosbag::TopicQuery(bag_topics));
    
    flex_sync::GeneralSync<sensor_msgs::Image, sensor_msgs::CameraInfo>
      sync2(topics, callback, 25);

    uint32_t cnt(0);
    for (rosbag::MessageInstance m: view) {
      sensor_msgs::Image::ConstPtr img = m.instantiate<sensor_msgs::Image>();
      if (img) {
        std::cout << img->header.stamp << " " << m.getTopic() << std::endl;
        sync2.process(m.getTopic(), img);
      } else {
        sensor_msgs::CameraInfo::ConstPtr cinfo =
          m.instantiate<sensor_msgs::CameraInfo>();
        if (cinfo) {
          std::cout << cinfo->header.stamp << " " << m.getTopic() << std::endl;
          sync2.process(m.getTopic(), cinfo);
        }
      }
      if (++cnt % 1000 == 0) {
        ROS_INFO("played %10u messages", cnt);
      }
    }
    //ros::spin();
  } catch (const std::exception& e) {
    ROS_ERROR("%s: %s", pnh.getNamespace().c_str(), e.what());
  }
  ROS_INFO("test complete");
  return (0);
}

