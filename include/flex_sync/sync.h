/* -*-c++-*--------------------------------------------------------------------
 * 2018 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#ifndef FLEX_SYNC_SYNC_H
#define FLEX_SYNC_SYNC_H

#include "flex_sync/sync_utils.h"
#include <ros/ros.h>
#include <functional>
#include <boost/shared_ptr.hpp>
#include <string>
#include <vector>
#include <map>
#include <mutex>


/*
 * Class for synchronizing across a single message type
 */

namespace flex_sync {
  template <class T>
  class Sync {
    using string = std::string;
    using Time   = ros::Time;
    template<class F> using vector = std::vector<F>;
    template<class F, class K> using map = std::map<F, K>;
  public:
    typedef boost::shared_ptr<T const> ConstPtr;
    typedef std::function<void(const vector<ConstPtr> &)> Callback;
    typedef map<Time, ConstPtr> MsgQueue;
    typedef map<string, MsgQueue> MsgMap;
    typedef map<Time, int> CountMap;
    
    Sync(const vector<string> &topics, const Callback &callback,
         unsigned int maxQueueSize = 5) :
      callback_(callback), maxQueueSize_(maxQueueSize)  {
      // initialize time-to-message maps for each topic
      for (const auto &topic: topics) {
        addTopic(topic);
      }
    }
    void setMaxQueueSize(int qs) {
      maxQueueSize_ = qs;
    }
    bool addTopic(const std::string &topic) {
      if (msgMap_.count(topic) == 0) {
        msgMap_[topic] = map<Time, ConstPtr>();
        topics_.push_back(topic);
        return (true);
      }
      ROS_WARN_STREAM("duplicate sync topic added: " << topic);
      return (false);
    }
    void process(const std::string &topic, const ConstPtr &msg) {
      std::unique_lock<std::mutex> lock(mutex_);
      const Time &t = msg->header.stamp;
      auto &q   = msgMap_[topic];
      auto qit = q.find(t);
      // store message in a per-topic queue
      if (qit != q.end()) {
        ROS_WARN_STREAM("duplicate on topic " << topic << " ignored, t=" << t);
        return;
      }
      if (q.size() >= maxQueueSize_) {
        auto it = q.begin();
        decrease_count(it->first, &msgCount_);
        q.erase(it);
      }
      q.insert(typename MsgQueue::value_type(t, msg));
      // update the map that counts how many
      // messages we've received for that time slot
      auto it = update_count(t, &msgCount_);
      if (it->second > (int) topics_.size()) {
        ROS_WARN_STREAM("too many messages in queue: " << it->second);
      }
      if (it->second >= (int) topics_.size()) {
        // got a full set of messages for that time slot
        currentTime_ = t;
        // publishMessages also cleans out old messages from the queues
        publishMessages(t);
        // clean out old entries from the message counting map
        it++;
        msgCount_.erase(msgCount_.begin(), it);
      }
    }
    const Time &getCurrentTime() const {
      std::unique_lock<std::mutex> lock(mutex_);
      return (currentTime_);
    }

  private:
    void publishMessages(const Time &t) {
      vector<ConstPtr> mvec = make_vec(t, topics_, &msgMap_);
      callback_(mvec);
    }

    unsigned int    maxQueueSize_{0};
    vector<string>  topics_;
    Time            currentTime_{0.0};
    MsgMap          msgMap_;
    CountMap        msgCount_;
    Callback        callback_;
    std::mutex      mutex_;
  };
}

#endif
