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
    
    Sync(const vector<string> &topics, const Callback &callback) :
      topics_(topics),  callback_(callback)  {
      // initialize time-to-message maps for each topic
      for (const auto &topic: topics_) {
        msgMap_[topic] = map<Time, ConstPtr>();
      }
    }
    void process(const std::string &topic, const ConstPtr &msg) {
      const Time &t = msg->header.stamp;
      // store message in a per-topic queue
      msgMap_[topic][t] = msg;
      // update the map that counts how many
      // messages we've received for that time slot
      auto it = update_count(t, &msgCount_);
      if (it->second == (int) (topics_.size())) {
        // got a full set of messages for that time slot
        currentTime_ = t;
        // publishMessages also cleans out old messages from the queues
        publishMessages(t);
        // clean out old entries from the message counting map
        it++;
        msgCount_.erase(msgCount_.begin(), it);
      }
    }
    const Time &getCurrentTime() const { return (currentTime_); }

  private:
    typedef map<string, map<Time, ConstPtr>> MsgMap;
    typedef map<Time, int> CountMap;
    void publishMessages(const Time &t) {
      vector<ConstPtr> mvec = make_vec(t, topics_, &msgMap_);
      callback_(mvec);
    }
    
    vector<string>  topics_;
    Time            currentTime_{0.0};
    MsgMap          msgMap_;
    CountMap        msgCount_;
    Callback        callback_;
  };
}

#endif
