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

  // https://stackoverflow.com/questions/18063451/get-index-of-a-tuple-elements-type
  template <class T, class Tuple>
  struct Index;

  template <class T, class... Types>
  struct Index<T, std::tuple<T, Types...>> {
    static const std::size_t value = 0;
  };

  template <class T, class U, class... Types>
  struct Index<T, std::tuple<U, Types...>> {
    static const std::size_t value = 1 + Index<T, std::tuple<Types...>>::value;
  };

  //
  template <typename MsgType>
  using TopicDeque = std::deque<boost::shared_ptr<MsgType>>;
  template <typename MsgType>
  using TopicVec = std::vector<boost::shared_ptr<MsgType>>;
  template <typename MsgType>
  struct TopicInfo {
    TopicDeque<MsgType> deque;
    TopicVec<MsgType> past;
    ros::Duration inter_message_lower_bound{ros::Duration(0)};
    uint32_t  num_virtual_moves{0};
  };
  // TODO: learn how to do this correctly, w/o inheritance
  template <typename MsgType>
  struct TopicInfoVector: public std::vector<TopicInfo<MsgType>> {
  };
  // TypeInfo holds all deques and maps
  // for a particular message type
  template <typename MsgType>
  struct TypeInfo {
    TopicInfoVector<MsgType>   topic_info;
    std::vector<bool>          has_dropped_messages;
    std::map<std::string, int> topic_to_index;
  };
  template <typename ... MsgTypes>
  class GeneralSync {
    // the signature of the callback function depends on the MsgTypes template
    // parameter.
    typedef std::tuple<std::vector<boost::shared_ptr<const MsgTypes>> ...> CallbackArg;
    typedef std::function<void(const std::vector<boost::shared_ptr<const MsgTypes>>& ...)> Callback;
    typedef std::tuple<TypeInfo<const MsgTypes>...> TupleOfTypeInfo;
    
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
      totalNumCallback_ = for_each(type_infos_, TopicInfoInitializer());
      std::cout << "total num cb args: " << totalNumCallback_ << std::endl;
    }

    struct TopicInfoInitializer {
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) const
        {
          const int n_topic = sync->topics_[I].size();
          std::cout << "initializing type: " << I << " with " << n_topic << " topics " << std::endl;
          std::get<I>(sync->candidate_).resize(n_topic);
          const size_t num_topics = sync->topics_[I].size();
          auto &type_info = std::get<I>(sync->type_infos_);
          type_info.topic_info.resize(num_topics);
          sync->tot_num_deques_ += num_topics;
          // make map between topic string and index for
          // lookup when data arrives
          for (int t_idx = 0; t_idx < (int) sync->topics_[I].size(); t_idx++) {
            type_info.topic_to_index[sync->topics_[I][t_idx]] = t_idx;
          }
          type_info.has_dropped_messages.resize(num_topics, false);
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
            auto &deque = std::get<I>(sync->type_infos_)[topic].deque;
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
      typedef TypeInfo<typename MsgPtrT::element_type const> TypeInfoT;
      //constexpr std::size_t idx = Index<TypeInfoT, TupleOfTypeInfo>::value;
      TypeInfoT &ti = std::get<TypeInfoT>(type_infos_);
      // m[topic] = TimeToTypePtrMap<typename MsgPtrT::element_type const>();
      auto topic_it = ti.topic_to_index.find(topic);
      if (topic_it == ti.topic_to_index.end()) {
        std::cerr << "flex_sync: invalid topic for type " << topic << std::endl;
        return;
      }
      const auto &stamp = msg->header.stamp;
      auto &topic_info = ti.topic_info[topic_it->second];
      topic_info.deque.push_back(msg);
      std::cout << "added message: " << topic << " time: " << stamp << std::endl;
      if (topic_info.deque.size() == 1ul) {
        ++num_non_empty_deques_;
        if (num_non_empty_deques_ == tot_num_deques_) {
          update(); // all deques have messages, go for it
        }
      }
      if (topic_info.deque.size() + topic_info.past.size() > queue_size_) {
        std::cout << "PROBLEM: queue overflow!!!" << std::endl;
        assert(0);
      }
    }

    struct CandidateMaker {
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) const {
          int num_cb_vals_found = 0;
          auto &type_info = std::get<I>(sync->type_infos_);
          for (size_t i = 0; i < type_info.topic_info.size(); i++) {
            auto &ti = type_info.topic_info[i];
            auto &deque = ti.deque;
            std::get<I>(sync->candidate_)[i] = deque.front();
            num_cb_vals_found++;
          }
          return (num_cb_vals_found);
        }
    };

    void makeCandidate() {
      (void) for_each(type_infos_, CandidateMaker());
    }

    void fake_update() {
      std::cout << "fake update!" << std::endl;
      return;
      CallbackArg cba;
      int num_good = for_each(type_infos_, MapUpdater());
      // deliver callback
      // this requires C++17
      std::cout << " num good: " << num_good << std::endl;
      if (num_good == totalNumCallback_) {
        std::apply([this](auto &&... args) { cb_(args...); }, candidate_);
      }
    };

    struct FullIndex {
      FullIndex(int32_t tp = -1, int32_t tc = -1): type(tp), topic(tc) {};
      bool operator==(const FullIndex &a) {
        return (type == a.type && topic == a.topic);
      };
      bool isValid() const { return(type != -1 && topic != -1); };
      int32_t type;
      int32_t topic;
    };
 
    class DroppedMessageUpdater {
    public:
      DroppedMessageUpdater(const FullIndex &end):
        end_index_(end), has_dropped_messages_(false) {};
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        auto &type_info = std::get<I>(sync->type_infos_);
        for (int j = 0; j < (int)type_info.has_dropped_messages.size(); j++) {
          if (!((I == end_index_.type) && (j == end_index_.topic))) {
            // No dropped message could have been better to use than
            // the ones we have, so it becomes ok to use this topic
            // as pivot in the future
            type_info.has_dropped_messages[j] = false;
          } else {
            // capture whether the end_index has dropped messages
            has_dropped_messages_ = type_info.has_dropped_messages[j];
          }
        }
        return (0);
      }
      bool endIndexHasDroppedMessages() const {
        return (has_dropped_messages_);
      }
    private:
      FullIndex end_index_;
      bool      has_dropped_messages_{false};
    };

    class CandidateBoundaryFinder {
    public:
      CandidateBoundaryFinder(bool end) :
        end_(end) {
        time_ = end_ ? ros::Time(0, 1) : ros::Time(std::numeric_limits< uint32_t >::max(), 999999999);
      };
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        int num_deques_found = 0;
        int topicIdx = 0;
        const auto &type_info = std::get<I>(sync->type_infos_);
        for (const auto &ti: type_info.topic_info) {
          const auto &deque = ti.deque;
          if (deque.empty()) {
            std::cerr << "ERROR: deque " << I << " cannot be empty!" << std::endl;
            ROS_ASSERT(!deque.empty());
            break;
          }
          const auto &m = deque.front();
          if ((m->header.stamp < time_) ^ end_) {
            time_ = m->header.stamp;
            index_.type = I;
            index_.topic = topicIdx;
          }
          topicIdx++;
          num_deques_found++;
        }
        return (num_deques_found);
      }
      const FullIndex &getIndex() const { return (index_); }
      ros::Time getTime() const { return (time_); }
      
    private:
      FullIndex index_;
      ros::Time time_;
      bool  end_;
    };

    void getCandidateBoundary(FullIndex *index,
                              ros::Time *time, bool end) {
      CandidateBoundaryFinder cbf(end);
      const int num_deques = for_each(type_infos_, cbf);
      *index = cbf.getIndex();
      *time  = cbf.getTime();
      std::cout << "cand bound num_deques: " << num_deques << std::endl;
    }
    // Assumes: all deques are non empty
    // Returns: the oldest message on the deques
    void getCandidateStart(FullIndex *start_index,
                           ros::Time *start_time)  {
      return getCandidateBoundary(start_index, start_time, false);
    }

    // Assumes: all deques are non empty
    // Returns: the latest message among the heads of the deques,
    // i.e. the minimum time to end an interval started at
    // getCandidateStart_index()
    void getCandidateEnd(FullIndex *end_index, ros::Time *end_time) {
      return getCandidateBoundary(end_index, end_time, true);
    }

    class VirtualCandidateBoundaryFinder {
    public:
      VirtualCandidateBoundaryFinder() :
        start_time_(ros::Time(std::numeric_limits< uint32_t >::max(),
                              999999999)),
        end_time_(ros::Time(0, 1)) {
      };

      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        int num_deques_found = 0;
        const auto &type_info = std::get<I>(sync->type_infos_);
        assert(sync->pivot_.isValid());
        for (size_t topicIdx = 0; topicIdx < type_info.topic_info.size();
             topicIdx++) {
          const auto &ti = type_info.topic_info[topicIdx];
          const auto &deque = ti.deque;
          const auto &past = ti.deque;
          // get virtual time
          ros::Time virtual_time;
          if (deque.empty()) {
            assert(!past.empty()); // Because we have a candidate
            const ros::Time last_msg_time = past.back()->header.stamp;
            const ros::Time msg_time_lower_bound = last_msg_time +
              ti.inter_message_lower_bound;
            virtual_time = std::max(msg_time_lower_bound, sync->pivot_time_);
          } else {
            virtual_time = deque.front()->header.stamp;
          }
          if (virtual_time < start_time_) {
            start_time_ = virtual_time;
            start_index_ = FullIndex(I, topicIdx);
          }
          if (virtual_time > end_time_) {
            end_time_ = virtual_time;
            end_index_ = FullIndex(I, topicIdx);
          }
          num_deques_found++;
        }
        return (num_deques_found);
      }
      ros::Time getStartTime() const { return (start_time_); }
      ros::Time getEndTime() const { return (end_time_); }
      FullIndex getStartIndex() const { return (start_index_); }
      FullIndex getEndIndex() const { return (end_index_); }
    private:
      FullIndex start_index_;
      ros::Time start_time_;
      FullIndex end_index_;
      ros::Time end_time_;
    };

    void getVirtualCandidateBoundary(FullIndex *start_index,
                                     ros::Time *start_time,
                                     FullIndex *end_index,
                                     ros::Time *end_time) {
      VirtualCandidateBoundaryFinder vcbf;
      (void) for_each(type_infos_, vcbf);
      *start_index = vcbf.getStartIndex();
      *start_time  = vcbf.getStartTime();
      *end_index = vcbf.getEndIndex();
      *end_time  = vcbf.getEndTime();
    }

    class DequeFrontDeleter {
    public:
      DequeFrontDeleter(const FullIndex &index): index_(index) {};
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        auto &type_info = std::get<I>(sync->type_infos_);
        if (I == index_.type) {
          auto &deque = type_info.topic_info[index_.topic].deque;
          assert(!deque.empty());
          deque.pop_front();
          if (deque.empty()) {
            --sync->num_non_empty_deques_;
          }
        }
        return (0);
      }
    private:
      FullIndex index_;
    };
  
    void dequeDeleteFront(const FullIndex &index) {
      DequeFrontDeleter dfd(index);
      (void) for_each(type_infos_, dfd);
    }

    class DequeMoverFrontToPast {
    public:
      DequeMoverFrontToPast(const FullIndex &index,
                            bool updateVirtualMoves):
        index_(index), update_virtual_moves_(updateVirtualMoves) {};
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        auto &type_info = std::get<I>(sync->type_infos_);
        if (I == index_.type) {
          auto &ti = type_info.topic_info[index_.topic];
          auto &deque = ti.deque;
          auto &past = ti.past;
          assert(!deque.empty());
          past.push_back(deque.front());
          deque.pop_front();
          if (deque.empty()) {
            --(sync->num_non_empty_deques_);
          }
          if (update_virtual_moves_) {
            ti.num_virtual_moves++;
          }
        }
        return (0);
      }
    private:
      FullIndex index_;
      bool      update_virtual_moves_{false};
    };
  
    // Assumes that deque number <index> is non empty
    void dequeMoveFrontToPast(const FullIndex &index) {
      DequeMoverFrontToPast dmfp(index, false);
      (void) for_each(type_infos_, dmfp);
    }

    class RecoverAndDelete {
    public:
      RecoverAndDelete() {};
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        auto &type_info = std::get<I>(sync->type_infos_);
        for (auto &ti: type_info.topic_info) {
          auto &deque = ti.deque;
          auto &past  = ti.past;
          while (!past.empty()) {
            deque.push_front(past.back());
            past.pop_back();
          }
          assert (!deque.empty());
          deque.pop_front();
          if (!deque.empty()) {
            ++(sync->num_non_empty_deques_);
          }
        }
        return (0);
      }
    };

    void recoverAndDelete() {
      RecoverAndDelete rnd;
      (void) for_each(type_infos_, rnd);
    }

    class ResetNumVirtualMoves {
    public:
      ResetNumVirtualMoves() {};
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        auto &type_info = std::get<I>(sync->type_infos_);
        for (auto &ti: type_info.topic_info) {
          ti.num_virtual_moves = 0;
        }
        return (0);
      }
    };

    class Recover {
    public:
      Recover() {};
      template<std::size_t I>
      int operate(GeneralSync<MsgTypes ...> *sync) {
        auto &type_info = std::get<I>(sync->type_infos_);
        for (auto &ti: type_info.topic_info) {
          for (uint32_t n = ti.num_virtual_moves; n != 0; n--) {
            ti.deque.push_front(ti.past.back());
            ti.past.pop_back();
          }
          if (!ti.deque.empty()) {
            sync->num_non_empty_deques_++;
          }
        }
        return (0);
      }
    };


    // Assumes: all deques are non empty now
    void publishCandidate() {
      printf("Publishing candidate\n");
      std::apply([this](auto &&... args) { cb_(args...); }, candidate_);
      // candidate_ = Tuple(); no needed
      pivot_ = FullIndex(); // reset to invalid
      // Recover hidden messages, and delete the ones
      // corresponding to the candidate
      num_non_empty_deques_ = 0; // We will recompute it from scratch
      recoverAndDelete();
    }

    void update()  {
      FullIndex index;
      ros::Time t;
      // get start time
      getCandidateStart(&index, &t);

      // While no deque is empty
      while (num_non_empty_deques_ == tot_num_deques_) {
        // Find the start and end of the current interval
        FullIndex end_index, start_index;
        getCandidateEnd(&end_index, &end_time_);
        getCandidateStart(&start_index, &start_time_);
        DroppedMessageUpdater dmu(end_index);
        (void) for_each(type_infos_, dmu);
        if (!pivot_.isValid()) {
          // We do not have a candidate
          // INVARIANT: the past_ vectors are empty
          // INVARIANT: (candidate_ has no filled members)
          if (end_time_ - start_time_ > max_interval_duration_) {
            // This interval is too big to be a valid candidate,
            // move to the next
            dequeDeleteFront(start_index);
            continue;
          }
          if (dmu.endIndexHasDroppedMessages()) {
            // The topic that would become pivot has dropped messages,
            // so it is not a good pivot
            dequeDeleteFront(start_index);
            continue;
          }
          // This is a valid candidate, and we don't have any, so take it
          makeCandidate();
          candidate_start_ = start_time_;
          candidate_end_ = end_time_;
          pivot_ = end_index;
          pivot_time_ = end_time_;
          dequeMoveFrontToPast(start_index);
        }  else {
          // We already have a candidate
          // Is this one better than the current candidate?
          // INVARIANT: has_dropped_messages_ is all false
          if ((end_time_ - candidate_end_) * (1 + age_penalty_) >=
              (start_time_ - candidate_start_)) {
            // This is not a better candidate, move to the next
            dequeMoveFrontToPast(start_index);
          }  else {
            // This is a better candidate
            makeCandidate();
            candidate_start_ = start_time_;
            candidate_end_ = end_time_;
            dequeMoveFrontToPast(start_index);
          }
        }
        // INVARIANT: we have a candidate and pivot
        ROS_ASSERT(pivot_.isValid());
        //printf("start_index == %d, pivot_ == %d\n", start_index, pivot_);
        if (start_index == pivot_) {
          // TODO: replace with start_time == pivot_time_
          // We have exhausted all possible candidates for this pivot,
          // we now can output the best one
          publishCandidate();
        } else if ((end_time_ - candidate_end_) * (1 + age_penalty_)
                   >= (pivot_time_ - candidate_start_)) {
          // We have not exhausted all candidates,
          // but this candidate is already provably optimal
          // Indeed, any future candidate must contain the interval
          // [pivot_time_ end_time], which is already too big.
          // Note: this case is subsumed by the next, but it may
          // save some unnecessary work and
          // it makes things (a little) easier to understand
          publishCandidate();
        } else if (num_non_empty_deques_ < tot_num_deques_)  {
          uint32_t num_non_empty_deques_before_virtual_search =
            num_non_empty_deques_;
          (void) for_each(type_infos_, ResetNumVirtualMoves());
          while (1) {
            ros::Time end_time, start_time;
            FullIndex end_index, start_index;
            getVirtualCandidateBoundary(&start_index, &start_time,
                                        &end_index, &end_time);
            if ((end_time - candidate_end_) * (1 + age_penalty_) >=
                (pivot_time_ - candidate_start_)) {
              // We have proved optimality
              // As above, any future candidate must contain the interval
              // [pivot_time_ end_time], which is already too big.
              publishCandidate();  // This cleans virtual moves as a byproduct
              break;  // From the while(1) loop only
            }
            if ((end_time - candidate_end_) * (1 + age_penalty_)
                < (start_time - candidate_start_))  {
              // We cannot prove optimality
              // Indeed, we have a virtual (i.e. optimistic) candidate
              // that is better than the current candidate
              // Cleanup the virtual search:
              num_non_empty_deques_ = 0; // We will recompute it from scratch
              (void) for_each(type_infos_, Recover());
              // unused variable warning stopper
              (void)num_non_empty_deques_before_virtual_search;
              assert(num_non_empty_deques_before_virtual_search ==
                     num_non_empty_deques_);
              break;
            }
            // Note: we cannot reach this point with
            // start_index == pivot_ since in that case we would have
            // start_time == pivot_time, in which case the two tests
            // above are the negation of each other, so that one must be true.
            // Therefore the while loop always terminates.
            assert(start_index != pivot_);
            assert(start_time < pivot_time_);
            // move front to past and update num_virtual_moves
            (void) for_each(type_infos_,
                            DequeMoverFrontToPast(start_index, true));
            dequeMoveFrontToPast(start_index);
          } // while(1)
        }
      } // while(num_non_empty_deques_ == (uint32_t)RealTypeCount::value)
    } // end of update()
 
  private:
    inline static const FullIndex NO_PIVOT;
    TupleOfTypeInfo type_infos_;
    
    std::vector<std::vector<std::string>> topics_;
    Callback cb_;
    CallbackArg candidate_;
    int totalNumCallback_{0};
    int num_non_empty_deques_{0};
    int tot_num_deques_{0};
    size_t queue_size_;
    ros::Time start_time_;
    ros::Time end_time_;
    FullIndex pivot_;
    ros::Time pivot_time_;
    ros::Time candidate_start_;
    ros::Time candidate_end_;
    ros::Duration max_interval_duration_{ros::DURATION_MAX}; // TODO: actually
    double age_penalty_{0.1};
  };
}


#endif // FLEX_SYNC_APPROX_SYNC_H
