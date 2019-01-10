/* -*-c++-*--------------------------------------------------------------------
 * 2018 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#include "flex_sync/sync.h"

#include <ros/ros.h>
#include <std_msgs/Header.h>

struct TestMsg1 {
  std_msgs::Header header;
};

struct TestMsg2 {
  std_msgs::Header header;
};

struct TestMsg3 {
  std_msgs::Header header;
};

typedef boost::shared_ptr<TestMsg1 const> ConstPtr1;
typedef boost::shared_ptr<TestMsg2 const> ConstPtr2;
typedef boost::shared_ptr<TestMsg3 const> ConstPtr3;

static void callback1(const std::vector<ConstPtr1> &p1) {
  std::cout << "got callback1: " << p1[0]->header.stamp << std::endl;
}

static void callback2(const std::vector<ConstPtr1> &p1,
                      const std::vector<ConstPtr2> &p2) {
  std::cout << "got callback2: " << p1[0]->header.stamp
            << " " << p2[0]->header.stamp << std::endl;
}

static void callback3(const std::vector<ConstPtr1> &p1,
                      const std::vector<ConstPtr2> &p2,
                      const std::vector<ConstPtr3> &p3) {
  std::cout << "got callback3:"
            << " " << p1[0]->header.stamp
            << " " << p2[0]->header.stamp
            << " " << p3[0]->header.stamp << std::endl;
}

using std::vector;
using std::string;

int main(int argc, char** argv) {
  ros::init(argc, argv, "flex_sync_test");
  ros::NodeHandle pnh("~");

  try {
    ros::Time t0 = ros::Time::now();
    // test Sync
    vector<vector<string>> topics(1);
    topics[0].push_back("foo1");
    boost::shared_ptr<TestMsg1> msg(new TestMsg1());
    flex_sync::Sync<TestMsg1> sync(topics, callback1);
    sync.process(topics[0][0], msg);

    // test Sync2
    topics.push_back(vector<string>());
    topics[1].push_back("foo2");
    
    flex_sync::Sync<TestMsg1, TestMsg2> sync2(topics, callback2);
    boost::shared_ptr<TestMsg1> msg1(new TestMsg1());
    boost::shared_ptr<TestMsg2> msg2(new TestMsg2());
    msg1->header.stamp = t0;
    msg2->header.stamp = t0 + ros::Duration(0.5);
    sync2.process(topics[0][0], msg1);
    sync2.process(topics[1][0], msg2);
    msg1.reset(new TestMsg1());
    msg2.reset(new TestMsg2());
    msg1->header.stamp = t0 + ros::Duration(1.0);
    msg2->header.stamp = t0 + ros::Duration(1.0);
    sync2.process(topics[0][0], msg1);
    sync2.process(topics[1][0], msg2);

    // test Sync3
    topics.push_back(vector<string>());
    topics[2].push_back("foo3");
    flex_sync::Sync<TestMsg1, TestMsg2, TestMsg3> sync3(topics, callback3);
    boost::shared_ptr<TestMsg3> msg3(new TestMsg3());
    msg3->header.stamp = msg2->header.stamp;
    sync3.process(topics[0][0], msg1);
    sync3.process(topics[1][0], msg2);
    sync3.process(topics[2][0], msg3);
    
    ros::spin();
  } catch (const std::exception& e) {
    ROS_ERROR("%s: %s", pnh.getNamespace().c_str(), e.what());
  }
}