/* -*-c++-*--------------------------------------------------------------------
 * 2018 Bernd Pfrommer bernd.pfrommer@gmail.com
 */
#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>

#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>

// sneaky class to gain access to the signalMessage() method
// taken from the ros cookbook
template <class M>
class BagSubscriber : public message_filters::SimpleFilter<M> {
public:
  void newMessage(const boost::shared_ptr<M const> &msg)  {
    message_filters::SimpleFilter<M>::signalMessage(msg);
  }
};

// test callback
void callback(
  const sensor_msgs::Image::ConstPtr &msg0,
  const sensor_msgs::Image::ConstPtr &msg1,
  const sensor_msgs::CameraInfo::ConstPtr &msg2,
  const sensor_msgs::CameraInfo::ConstPtr &msg3,
  const sensor_msgs::Image::ConstPtr &msg4,
  const sensor_msgs::CameraInfo::ConstPtr &msg5) {
  std::cout << msg0->header.stamp << " got callback!" << std::endl;
}

int main(int argc, char** argv) {
  ros::init(argc, argv, "message_filters_test");
  ros::NodeHandle pnh("~");

  try {
    const std::vector<std::string> topics =
      {"/left_tof/stream/1/mono8",
       "/right_tof/stream/1/mono8",
       "/t265/fisheye1/image_raw",
       "/left_tof/depth/camera_info",
       "/right_tof/depth/camera_info",
       "/t265/fisheye1/camera_info"};
    
    // Use time synchronizer to make sure we get properly synchronized images
    rosbag::Bag bag;
    bag.open("test.bag", rosbag::bagmode::Read);
    rosbag::View view(bag, rosbag::TopicQuery(topics));
    // image subscribers
    std::map<std::string, std::shared_ptr<BagSubscriber<sensor_msgs::Image>>> img_sub;
    for (int i = 0; i < 3; i++) {
      img_sub[topics[i]] = std::make_shared<BagSubscriber<sensor_msgs::Image>>();
    }
    // camerainfo subscribers
    std::map<std::string, std::shared_ptr<BagSubscriber<sensor_msgs::CameraInfo>>> caminfo_sub;
    for (int i = 3; i < (int)topics.size(); i++) {
      caminfo_sub[topics[i]] = std::make_shared<BagSubscriber<sensor_msgs::CameraInfo>>();
    }
    // synchronizer
    message_filters::TimeSynchronizer<
      sensor_msgs::Image, sensor_msgs::Image,
      sensor_msgs::CameraInfo, sensor_msgs::CameraInfo,
      sensor_msgs::Image, sensor_msgs::CameraInfo>
      sync(*img_sub[topics[0]], *img_sub[topics[1]], *caminfo_sub[topics[3]], *caminfo_sub[topics[4]],
           *img_sub[topics[2]], *caminfo_sub[topics[5]],  25);
    sync.registerCallback(boost::bind(&callback, _1, _2, _3, _4, _5, _6));

    for (rosbag::MessageInstance m: view) {
      sensor_msgs::Image::ConstPtr img = m.instantiate<sensor_msgs::Image>();
      if (img) {
        img_sub[m.getTopic()]->newMessage(img);
      } else {
        sensor_msgs::CameraInfo::ConstPtr cinfo = m.instantiate<sensor_msgs::CameraInfo>();
        if (cinfo) {
          caminfo_sub[m.getTopic()]->newMessage(cinfo);
        }
      }
    }
    ros::spin();
  } catch (const std::exception& e) {
    ROS_ERROR("%s: %s", pnh.getNamespace().c_str(), e.what());
  }
}

