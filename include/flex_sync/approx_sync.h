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
#include <deque>
/*
 * Class for synchronizing across variable number of messages
 */


namespace flex_sync {

  //
  template <typename MsgType>
  using TypeDeque = std::deque<boost::shared_ptr<MsgType>>;
  template <typename MsgType>
  using TypeVec = std::vector<boost::shared_ptr<MsgType>>;
  template <typename MsgType>
  struct TypeInfo {
    TypeDeque<MsgType> deque;
    TypeVec<MsgType> past;
  };
  //
  template <typename MsgType>
  struct TopicMap: public std::map<std::string, TypeInfo<MsgType>> {
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
                Callback cb, size_t queueSize) :
      topics_(topics), cb_(cb), queue_size_(queueSize) {
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
            sync->tot_num_deques_++;
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
            auto &deque = std::get<I>(sync->maps_)[topic].deque;
            if (deque.empty()) {
              std::get<I>(sync->cba_)[topic_idx] = deque.back();
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
      // m[topic] = TimeToTypePtrMap<typename MsgPtrT::element_type const>();
      auto topic_it = m.find(topic);
      if (topic_it == m.end()) {
        std::cerr << "flex_sync: invalid topic for type " << topic << std::endl;
        return;
      }
      const auto &stamp = msg->header.stamp;
      auto &topicInfo = topic_it->second;
      topicInfo.deque.push_back(msg);
      std::cout << "added message: " << topic << " time: " << stamp << std::endl;
      if (topicInfo.deque.size() == 1ul) {
        ++num_non_empty_deques_;
        if (num_non_empty_deques_ == tot_num_deques_) {
          update(); // all deques have messages, go for it
        }
      }
      if (topicInfo.deque.size() + topicInfo.past.size() > queue_size_) {
        std::cout << "PROBLEM: queue overflow!!!" << std::endl;
        assert(0);
      }
    }

    void fake_update() {
      std::cout << "fake update!" << std::endl;
      return;
      CallbackArg cba;
      int num_good = for_each(maps_, MapUpdater());
      // deliver callback
      // this requires C++17
      std::cout << " num good: " << num_good << std::endl;
      if (num_good == totalNumCallback_) {
        std::apply([this](auto &&... args) { cb_(args...); }, cba_);
      }
    };

    class CandidateBoundaryFinder {
    public:
      CandidateBoundaryFinder(bool end) :
        typeIndex_(-1), topicIndex_(-1), end_(end) {
        time_ = end_ ? ros::Time(0, 1) : ros::Time(std::numeric_limits< uint32_t >::max(), 999999999);
      };
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        int num_deques_found = 0;
        int topicIdx = 0;
        const auto &topicMap = std::get<I>(sync->maps_);
        for (size_t topic_idx = 0; topic_idx < sync->topics_[I].size(); topic_idx++) {
          const std::string &topic = sync->topics_[I][topic_idx];
          const auto map_it = topicMap.find(topic);
          if (map_it == topicMap.end()) {
            std::cerr << "ERROR: topic " << topic << " not found for type " << I << std::endl;
            ROS_ASSERT(map_it != topicMap.end());
          }
          const auto &deque = map_it->second.deque;
          if (deque.empty()) {
            std::cerr << "ERROR: deque " << I << " cannot be empty!" << std::endl;
            ROS_ASSERT(!deque.empty());
            break;
          }
          const auto &m = deque.front();
          if ((m->header.stamp < time_) ^ end_) {
            time_ = m->header.stamp;
            typeIndex_ = I;
            topicIndex_ = topicIdx;
          }
          topicIdx++;
          num_deques_found++;
        }
        return (num_deques_found);
      }
      int32_t getTypeIndex() const { return (typeIndex_); }
      int32_t getTopicIndex() const { return (topicIndex_); }
      ros::Time getTime() const { return (time_); }
      
    private:
      int32_t typeIndex_;
      int32_t topicIndex_;
      ros::Time time_;
      bool  end_;
    };

    void getCandidateBoundary(uint32_t *index, ros::Time *time, bool end) {
      CandidateBoundaryFinder cbf(end);
      const int num_deques = for_each(maps_, cbf);
      *index = (uint32_t) cbf.getTopicIndex();
      *time  = cbf.getTime();
      std::cout << "cand bound num_deques: " << num_deques << std::endl;
    }


    void update()  {
      uint32_t index;
      ros::Time t;
      // get start time
      getCandidateBoundary(&index, &t, false);
/*      
      // While no deque is empty
      while (num_non_empty_deques_ == tot_num_deques) {
        // Find the start and end of the current interval
        ros::Time end_time, start_time;
        uint32_t end_index, start_index;
        getCandidateEnd(end_index, end_time);
        getCandidateStart(start_index, start_time);
        for (uint32_t i = 0; i < (uint32_t)RealTypeCount::value; i++)  {
          if (i != end_index)  {
            // No dropped message could have been better to use than the ones we have,
            // so it becomes ok to use this topic as pivot in the future
            has_dropped_messages_[i] = false;
          }
        }
        if (pivot_ == NO_PIVOT) {
          // We do not have a candidate
          // INVARIANT: the past_ vectors are empty
          // INVARIANT: (candidate_ has no filled members)
          if (end_time - start_time > max_interval_duration_) {
            // This interval is too big to be a valid candidate, move to the next
            dequeDeleteFront(start_index);
            continue;
          }
          if (has_dropped_messages_[end_index]) {
            // The topic that would become pivot has dropped messages, so it is not a good pivot
            dequeDeleteFront(start_index);
            continue;
          }
          // This is a valid candidate, and we don't have any, so take it
          makeCandidate();
          candidate_start_ = start_time;
          candidate_end_ = end_time;
          pivot_ = end_index;
          pivot_time_ = end_time;
          dequeMoveFrontToPast(start_index);
        }  else {
          // We already have a candidate
          // Is this one better than the current candidate?
          // INVARIANT: has_dropped_messages_ is all false
          if ((end_time - candidate_end_) * (1 + age_penalty_) >= (start_time - candidate_start_)) {
            // This is not a better candidate, move to the next
            dequeMoveFrontToPast(start_index);
          }  else {
            // This is a better candidate
            makeCandidate();
            candidate_start_ = start_time;
            candidate_end_ = end_time;
            dequeMoveFrontToPast(start_index);
            // Keep the same pivot (and pivot time)
          }
        }
        // INVARIANT: we have a candidate and pivot
        ROS_ASSERT(pivot_ != NO_PIVOT);
        //printf("start_index == %d, pivot_ == %d\n", start_index, pivot_);
        if (start_index == pivot_) { // TODO: replace with start_time == pivot_time_
          // We have exhausted all possible candidates for this pivot, we now can output the best one
          publishCandidate();
        } else if ((end_time - candidate_end_) * (1 + age_penalty_) >= (pivot_time_ - candidate_start_)) {
          // We have not exhausted all candidates, but this candidate is already provably optimal
          // Indeed, any future candidate must contain the interval [pivot_time_ end_time], which
          // is already too big.
          // Note: this case is subsumed by the next, but it may save some unnecessary work and
          //       it makes things (a little) easier to understand
          publishCandidate();
        }  else if (num_non_empty_deques_ < (uint32_t)RealTypeCount::value)  {
          uint32_t num_non_empty_deques_before_virtual_search = num_non_empty_deques_;

          // Before giving up, use the rate bounds, if provided, to further try to prove optimality
          std::vector<int> num_virtual_moves(9,0);
          while (1) {
            ros::Time end_time, start_time;
            uint32_t end_index, start_index;
            getVirtualCandidateEnd(end_index, end_time);
            getVirtualCandidateStart(start_index, start_time);
            if ((end_time - candidate_end_) * (1 + age_penalty_) >= (pivot_time_ - candidate_start_)) {
              // We have proved optimality
              // As above, any future candidate must contain the interval [pivot_time_ end_time], which
              // is already too big.
              publishCandidate();  // This cleans up the virtual moves as a byproduct
              break;  // From the while(1) loop only
            }
            if ((end_time - candidate_end_) * (1 + age_penalty_) < (start_time - candidate_start_))  {
              // We cannot prove optimality
              // Indeed, we have a virtual (i.e. optimistic) candidate that is better than the current
              // candidate
              // Cleanup the virtual search:
              num_non_empty_deques_ = 0; // We will recompute it from scratch
              recover<0>(num_virtual_moves[0]);
              recover<1>(num_virtual_moves[1]);
              recover<2>(num_virtual_moves[2]);
              recover<3>(num_virtual_moves[3]);
              recover<4>(num_virtual_moves[4]);
              recover<5>(num_virtual_moves[5]);
              recover<6>(num_virtual_moves[6]);
              recover<7>(num_virtual_moves[7]);
              recover<8>(num_virtual_moves[8]);
              (void)num_non_empty_deques_before_virtual_search; // unused variable warning stopper
              ROS_ASSERT(num_non_empty_deques_before_virtual_search == num_non_empty_deques_);
              break;
            }
            // Note: we cannot reach this point with start_index == pivot_ since in that case we would
            //       have start_time == pivot_time, in which case the two tests above are the negation
            //       of each other, so that one must be true. Therefore the while loop always terminates.
            ROS_ASSERT(start_index != pivot_);
            ROS_ASSERT(start_time < pivot_time_);
            dequeMoveFrontToPast(start_index);
            num_virtual_moves[start_index]++;
          } // while(1)
        }
        } // while(num_non_empty_deques_ == (uint32_t)RealTypeCount::value)
*/
        }
 
  private:
    TupleOfMaps maps_;
    std::vector<std::vector<std::string>> topics_;
    Callback cb_;
    CallbackArg cba_;
    int totalNumCallback_{0};
    int num_non_empty_deques_{0};
    int tot_num_deques_{0};
    size_t queue_size_;
  };
}


#endif // FLEX_SYNC_APPROX_SYNC_H
