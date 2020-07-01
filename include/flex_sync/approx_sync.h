/* -*-c++-*--------------------------------------------------------------------
 * 2020 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#ifndef FLEX_SYNC_APPROX_SYNC_H
#define FLEX_SYNC_APPROX_SYNC_H

#include <ros/ros.h>
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <iostream>
#include <utility>
#include <tuple>
/*
 * Class for synchronizing across variable number of messages
 */


namespace flex_sync {

  //
  template <typename MsgType>
  using TimeToTypePtrMap = std::map<ros::Time, boost::shared_ptr<MsgType>>;
  //
  template <typename MsgType>
  struct TopicMap: public std::map<std::string, TimeToTypePtrMap<MsgType>> {
  };
  template <typename ... MsgTypes>
  class GeneralSync {
    // the signature of the callback function depends on the MsgTypes template
    // parameter.
    typedef std::tuple<std::vector<boost::shared_ptr<const MsgTypes>> ...> CallbackArg;
    typedef std::function<void(const std::vector<boost::shared_ptr<const MsgTypes>>& ...)> Callback;
    typedef std::tuple<TopicMap<const MsgTypes>...> TupleOfMaps;
    
  public:
    template<std::size_t I = 0, typename FuncT, typename... Tp>
    inline typename std::enable_if<I == sizeof...(Tp), int>::type
    for_each(std::tuple<Tp...> &, FuncT) // Unused arg needs no name
      { return 0; } // do nothing

    template<std::size_t I = 0, typename FuncT, typename... Tp>
    inline typename std::enable_if<I < sizeof...(Tp), int>::type
    for_each(std::tuple<Tp...>& t, FuncT f)  {
      std::cout << "operating on I = " << I << std::endl;
      const int rv = f.template operate<I>(this);
      std::cout << "rv: " << rv << std::endl;
      return (rv + for_each<I + 1, FuncT, Tp...>(t, f));
    }

    GeneralSync(const std::vector<std::vector<std::string>> &topics,
                Callback cb) : topics_(topics), cb_(cb) {
      totalNumCallback_ = for_each(maps_, MapInitializer());
      std::cout << "total num cb args: " << totalNumCallback_ << std::endl;
    }

    struct MapInitializer {
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) const
        {
          const int n_topic = sync->topics_[I].size();
          std::cout << "initializing type: " << I << " with " << n_topic << " topics " << std::endl;
          std::get<I>(sync->cba_).resize(n_topic);
          for (const auto &topic: sync->topics_[I]) {          
            std::get<I>(sync->maps_)[topic] = typename std::tuple_element<I, TupleOfMaps>::type::mapped_type();
          };
          return (n_topic);
        }
    };

    struct MapUpdater {
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) const
        {
          int num_cb_vals_found = 0;
          for (size_t topic_idx = 0; topic_idx < sync->topics_[I].size(); topic_idx++) {
            const std::string &topic = sync->topics_[I][topic_idx];
            if (!std::get<I>(sync->maps_)[topic].empty()) {
              std::get<I>(sync->cba_)[topic_idx] =
                std::get<I>(sync->maps_)[topic].rbegin()->second;
              num_cb_vals_found++;
            }
          }
          return (num_cb_vals_found);
        }
    };

    template<typename MsgPtrT>
    void process(const std::string &topic, const MsgPtrT &msg) {
      typedef TopicMap<typename MsgPtrT::element_type const> MapT;
      MapT &m = std::get<MapT>(maps_);
      m[topic] = TimeToTypePtrMap<typename MsgPtrT::element_type const>();
      m[topic][msg->header.stamp] = msg;
      std::cout << "added message: " << topic << " " << msg->header.stamp << std::endl;
      update();
    }

    void update() {
      CallbackArg cba;
      int num_good = for_each(maps_, MapUpdater());
      // deliver callback
      // this requires C++17
      std::cout << " num good: " << num_good << std::endl;
      if (num_good == totalNumCallback_) {
        std::apply([this](auto &&... args) { cb_(args...); }, cba_);
      }
    };
  private:
    TupleOfMaps maps_;
    std::vector<std::vector<std::string>> topics_;
    Callback cb_;
    CallbackArg cba_;
    int totalNumCallback_{0};
  };
}


#endif // FLEX_SYNC_APPROX_SYNC_H
