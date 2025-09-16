#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>

// code adapted from Dmitry Vyukov. https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue

namespace RACoherence {

template<typename T, size_t S>
class spmc_bounded_queue
{
public:
  spmc_bounded_queue()
  {
    assert((S >= 2) &&
      ((S & (S - 1)) == 0));
    for (size_t i = 0; i != S; i += 1)
      buffer_[i].sequence_.store(i, std::memory_order_relaxed);
    enqueue_pos_.store(0, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
  }
  bool enqueue(T const& data)
  {
    cell_t* cell;
    size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    for (;;)
    {
      cell = &buffer_[pos & buffer_mask_];
      size_t seq = 
        cell->sequence_.load(std::memory_order_acquire);
      intptr_t dif = (intptr_t)seq - (intptr_t)pos;
      if (dif == 0)
      {
        //if (enqueue_pos_.compare_exchange_weak
        //    (pos, pos + 1, std::memory_order_relaxed))
        //  break;
        enqueue_pos_.store(pos + 1, std::memory_order_relaxed);
        break;
      }
      else if (dif < 0)
        return false;
      else
        pos = enqueue_pos_.load(std::memory_order_relaxed);
    }
    cell->data_ = data;
    cell->sequence_.store(pos + 1, std::memory_order_release);
    return true;
  }
  bool dequeue(T& data)
  {
    cell_t* cell;
    size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    for (;;)
    {
      cell = &buffer_[pos & buffer_mask_];
      size_t seq = 
        cell->sequence_.load(std::memory_order_acquire);
      intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
      if (dif == 0)
      {
        if (dequeue_pos_.compare_exchange_weak
            (pos, pos + 1, std::memory_order_relaxed))
          break;
      }
      else if (dif < 0)
        return false;
      else
        pos = dequeue_pos_.load(std::memory_order_relaxed);
    }
    data = cell->data_;
    cell->sequence_.store
      (pos + buffer_mask_ + 1, std::memory_order_release);
    return true;
  }

private:
  struct cell_t
  {
    std::atomic<size_t>   sequence_;
    T                     data_;
  };
  static size_t const     cacheline_size = 64;
  typedef char            cacheline_pad_t [cacheline_size];
  cacheline_pad_t         pad0_;
  cell_t                  buffer_[S];
  size_t const            buffer_mask_ = S - 1;
  cacheline_pad_t         pad1_;
  std::atomic<size_t>     enqueue_pos_;
  cacheline_pad_t         pad2_;
  std::atomic<size_t>     dequeue_pos_;
  cacheline_pad_t         pad3_;
  spmc_bounded_queue(spmc_bounded_queue const&);
  void operator = (spmc_bounded_queue const&);
};

} // RACoherence
