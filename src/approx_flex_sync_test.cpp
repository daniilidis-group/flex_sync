/* -*-c++-*--------------------------------------------------------------------
 * 2018 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#include "flex_sync/approx_sync.h"
#include "flex_sync/test_data.h"
#include "flex_sync/TestMsg1.h"
#include "flex_sync/TestMsg2.h"
#include "flex_sync/TestMsg3.h"

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <std_msgs/Time.h>

using TestMsg1 = flex_sync::TestMsg1;
using TestMsg2 = flex_sync::TestMsg2;
using TestMsg3 = flex_sync::TestMsg3;

typedef boost::shared_ptr<const TestMsg1> ConstPtr1;
typedef boost::shared_ptr<const TestMsg2> ConstPtr2;
typedef boost::shared_ptr<const TestMsg3> ConstPtr3;

template<class T>
static void print_vec(const std::vector<boost::shared_ptr<const T>> &v) {
  if (v.empty()) {
    std::cout << "callback vector empty!" << std::endl;
  } else {
    for (size_t i = 0; i < v.size(); i++) {
      std::cout << " " << i << " header: " << v[i]->header.stamp << std::endl;
    }
  }
  
}
static void callback1(const std::vector<ConstPtr1> &p1) {
  std::cout << "callback1: " << std::endl;
  print_vec(p1);
}

static void callback2(const std::vector<ConstPtr1> &p1,
                      const std::vector<ConstPtr2> &p2) {
  std::cout << "callback2: " << std::endl;
  print_vec(p1);
  print_vec(p2);
}

static void callback3(const std::vector<ConstPtr1> &p1,
                      const std::vector<ConstPtr2> &p2,
                      const std::vector<ConstPtr3> &p3) {
  if (p1.empty()) {
    std::cout << "got empty callback!" << std::endl;
  } else {
  }
  std::cout << "got callback3:"
            << " " << p1[0]->header.stamp
            << " " << p2[0]->header.stamp
            << " " << p3[0]->header.stamp << std::endl;
}

static void callback3_0(const std::vector<ConstPtr1> &p1,
                      const std::vector<ConstPtr2> &p2,
                      const std::vector<ConstPtr3> &p3) {
  std::cout << "got callback3_0:"
            << " " << p1[0]->header.stamp
            << " " << p2[0]->header.stamp << std::endl;
}

static void callback3s(const std::vector<ConstPtr1> &p1,
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
  ros::init(argc, argv, "approx_flex_sync_test");
  ros::NodeHandle pnh("~");

  try {
    //ros::Time t0 = ros::Time::now();
    // test Sync
    vector<vector<string>> topics(1);
    topics[0].push_back("/topic/foo1");
    boost::shared_ptr<TestMsg1> msg(new TestMsg1());
    std::cout << "creating GeneralSync<TestMsg1> " << std::endl;
    flex_sync::GeneralSync<TestMsg1> sync(topics, callback1, 10);
    sync.process<boost::shared_ptr<const TestMsg1>>(topics[0][0], msg);

    // test Sync2
    topics.push_back(vector<string>());
    topics[1].push_back("/topics/foo2");

    topics.clear();
    topics.resize(2);
    topics[0].push_back("left_fisheye");
    topics[0].push_back("right_fisheye");
    topics[1].push_back("left_tof");
    topics[1].push_back("right_tof");
    std::cout << "creating GeneralSync<TestMsg1, TestMsg2> " << std::endl;
    flex_sync::GeneralSync<TestMsg1, TestMsg2> sync2(topics, callback2, 10);
    flex_sync::TestData db2(topics, "../test_data");
    db2.play(&sync2);

    /*
    boost::shared_ptr<TestMsg1> msg1(new TestMsg1());
    boost::shared_ptr<TestMsg2> msg2(new TestMsg2());
    msg1->header.stamp = t0;
    msg2->header.stamp = t0 + ros::Duration(0.5);
    sync2.process<boost::shared_ptr<const TestMsg1>>(topics[0][0], msg1);
    sync2.process<boost::shared_ptr<const TestMsg2>>(topics[1][0], msg2);
    msg1.reset(new TestMsg1());
    msg2.reset(new TestMsg2());
    msg1->header.stamp = t0 + ros::Duration(1.0);
    msg2->header.stamp = t0 + ros::Duration(1.0);
    sync2.process<boost::shared_ptr<const TestMsg1>>(topics[0][0], msg1);
    sync2.process<boost::shared_ptr<const TestMsg2>>(topics[1][0], msg2);
    */
    
    /*
    // test Sync3
    topics.push_back(vector<string>());
    topics[2].push_back("foo3");
    flex_sync::Sync<TestMsg1, TestMsg2, TestMsg3> sync3(topics, callback3);
    boost::shared_ptr<TestMsg3> msg3(new TestMsg3());
    msg3->header.stamp = msg2->header.stamp;
    sync3.process(topics[0][0], msg1);
    sync3.process(topics[1][0], msg2);
    sync3.process(topics[2][0], msg3);

    // now test Sync3 w/o traffic on 3rd channel.
    topics[2].clear(); // erase all topics for 3rd channel
    flex_sync::Sync<TestMsg1, TestMsg2, TestMsg3>
      sync3_0(topics, callback3_0);
    boost::shared_ptr<TestMsg3> msg3_0(new TestMsg3());
    sync3_0.process(topics[0][0], msg1);
    sync3_0.process(topics[1][0], msg2);

    // test only if it compiles!
    flex_sync::SubscribingSync<TestMsg1, TestMsg2, TestMsg3>
      ssink(pnh, topics, callback3s);
    */
    ros::spin();
  } catch (const std::exception& e) {
    ROS_ERROR("%s: %s", pnh.getNamespace().c_str(), e.what());
  }
}
