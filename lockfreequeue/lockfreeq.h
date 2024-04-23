#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <atomic>
#include <emmintrin.h>
#define ATOMIC_QUEUE_LIKELY(expr) __builtin_expect(static_cast<bool>(expr), 1)
#define ATOMIC_QUEUE_UNLIKELY(expr) __builtin_expect(static_cast<bool>(expr), 0)
#define ATOMIC_QUEUE_NOINLINE __attribute__((noinline))
typedef enum memory_order
    {
      memory_order_relaxed,
      memory_order_consume,
      memory_order_acquire,
      memory_order_release,
      memory_order_acq_rel,
      memory_order_seq_cst
    } memory_order;
auto constexpr A = std::memory_order_acquire;
auto constexpr R = std::memory_order_release;
auto constexpr X = std::memory_order_relaxed;
auto constexpr C = std::memory_order_seq_cst;
auto constexpr AR = std::memory_order_acq_rel;


static inline void spin_loop_pause() noexcept {
    _mm_pause();
}
constexpr int CACHE_LINE_SIZE = 64;

template<size_t elements_per_cache_line> struct GetCacheLineIndexBits { static int constexpr value = 0; };
template<> struct GetCacheLineIndexBits<256> { static int constexpr value = 8; };
template<> struct GetCacheLineIndexBits<128> { static int constexpr value = 7; };
template<> struct GetCacheLineIndexBits< 64> { static int constexpr value = 6; };
template<> struct GetCacheLineIndexBits< 32> { static int constexpr value = 5; };
template<> struct GetCacheLineIndexBits< 16> { static int constexpr value = 4; };
template<> struct GetCacheLineIndexBits<  8> { static int constexpr value = 3; };
template<> struct GetCacheLineIndexBits<  4> { static int constexpr value = 2; };
template<> struct GetCacheLineIndexBits<  2> { static int constexpr value = 1; };


template<unsigned array_size, size_t elements_per_cache_line>
struct GetIndexShuffleBits {
    static int constexpr bits = GetCacheLineIndexBits<elements_per_cache_line>::value;
    static unsigned constexpr min_size = 1u << (bits * 2);
    static int constexpr value = array_size < min_size ? 0 : bits;
};

template<class T>
constexpr T decrement(T x) noexcept {
    return x - 1;
}

template<class T>
constexpr T increment(T x) noexcept {
    return x + 1;
}

template<class T>
constexpr T or_equal(T x, unsigned u) noexcept {
    return x | x >> u;
}

template<class T, class... Args>
constexpr T or_equal(T x, unsigned u, Args... rest) noexcept {
    return or_equal(or_equal(x, u), rest...);
}

constexpr uint32_t round_up_to_power_of_2(uint32_t a) noexcept {
    return increment(or_equal(decrement(a), 1, 2, 4, 8, 16));
}

constexpr uint64_t round_up_to_power_of_2(uint64_t a) noexcept {
    return increment(or_equal(decrement(a), 1, 2, 4, 8, 16, 32));
}


template<int BITS>
constexpr unsigned remap_index(unsigned index) noexcept {
    unsigned constexpr mix_mask{(1u << BITS) - 1};
    unsigned const mix{(index ^ (index >> BITS)) & mix_mask};
    return index ^ mix ^ (mix << BITS);
}

template<>
constexpr unsigned remap_index<0>(unsigned index) noexcept {
    return index;
}

template<int BITS, class T>
constexpr T& map(T* elements, unsigned index) noexcept {
    return elements[remap_index<BITS>(index)];
}


template <class Derived>
class AtomicQueue {
protected:
    // Put these on different cache lines to avoid false sharing between readers and writers.
    alignas(CACHE_LINE_SIZE) std::atomic<unsigned> head_ = {};
    alignas(CACHE_LINE_SIZE) std::atomic<unsigned> tail_ = {};

    // The special member functions are not thread-safe.

    AtomicQueue() noexcept = default;

    AtomicQueue(AtomicQueue const& b) noexcept
        : head_(b.head_.load(X))
        , tail_(b.tail_.load(X)) {}

    AtomicQueue& operator=(AtomicQueue const& b) noexcept {
        head_.store(b.head_.load(X), X);
        tail_.store(b.tail_.load(X), X);
        return *this;
    }

    void swap(AtomicQueue& b) noexcept {
        unsigned h = head_.load(X);
        unsigned t = tail_.load(X);
        head_.store(b.head_.load(X), X);
        tail_.store(b.tail_.load(X), X);
        b.head_.store(h, X);
        b.tail_.store(t, X);
    }



    enum State : unsigned char { EMPTY, STORING, STORED, LOADING };

    template<class T>
    static T do_pop_any(std::atomic<unsigned char>& state, T& q_element) noexcept {

        for(;;) {
            unsigned char expected = STORED;
            if(ATOMIC_QUEUE_LIKELY(state.compare_exchange_weak(expected, LOADING, A, X))) {
                T element{std::move(q_element)};
                state.store(EMPTY, R);
                return element;
            }
            do
                spin_loop_pause();
            while(Derived::maximize_throughput_ && state.load(X) != STORED);
        }

    }

    template<class U, class T>
    static void do_push_any(U&& element, std::atomic<unsigned char>& state, T& q_element) noexcept {
        for(;;) {
            unsigned char expected = EMPTY;
            if(ATOMIC_QUEUE_LIKELY(state.compare_exchange_weak(expected, STORING, A, X))) {
                q_element = std::forward<U>(element);
                state.store(STORED, R);
                return;
            }
            do
                spin_loop_pause();
            while(Derived::maximize_throughput_ && state.load(X) != STORED);  
        }

    }

public:
    template<class T>
    bool try_push(T&& element) noexcept {
        auto head = head_.load(X);
        do {
            if(static_cast<int>(head - tail_.load(X)) >= static_cast<int>(static_cast<Derived&>(*this).size_))
                return false;
        } while(ATOMIC_QUEUE_UNLIKELY(!head_.compare_exchange_weak(head, head + 1, X, X))); // This loop is not FIFO.
        

        static_cast<Derived&>(*this).do_push(std::forward<T>(element), head);
        return true;
    }

    template<class T>
    bool try_pop(T& element) noexcept {
        auto tail = tail_.load(X);
        do {
            if(static_cast<int>(head_.load(X) - tail) <= 0)
                return false;
        } while(ATOMIC_QUEUE_UNLIKELY(!tail_.compare_exchange_weak(tail, tail + 1, X, X))); // This loop is not FIFO.


        element = static_cast<Derived&>(*this).do_pop(tail);
        return true;
    }

    template<class T>
    void push(T&& element) noexcept {
        unsigned head;
        constexpr auto memory_order =   std::memory_order_relaxed;
        head = head_.fetch_add(1, memory_order);

        static_cast<Derived&>(*this).do_push(std::forward<T>(element), head);
    }

    auto pop() noexcept {
        unsigned tail;
        constexpr auto memory_order =  std::memory_order_relaxed;
        tail = tail_.fetch_add(1, memory_order); 

        return static_cast<Derived&>(*this).do_pop(tail);
    }

    bool was_empty() const noexcept {
        return !was_size();
    }

    bool was_full() const noexcept {
        return was_size() >= static_cast<int>(static_cast<Derived const&>(*this).size_);
    }

    unsigned was_size() const noexcept {
        // tail_ can be greater than head_ because of consumers doing pop, rather that try_pop, when the queue is empty.
        return std::max(static_cast<int>(head_.load(X) - tail_.load(X)), 0);
    }

    unsigned capacity() const noexcept {
        return static_cast<Derived const&>(*this).size_;
    }
};
template<class T, unsigned SIZE, bool MAXIMIZE_THROUGHPUT = true>
class AtomicQueue2 : public AtomicQueue<AtomicQueue2<T, SIZE, MAXIMIZE_THROUGHPUT>> {
    using Base = AtomicQueue<AtomicQueue2<T, SIZE>>;
    using State = typename Base::State;
    friend Base;

    static constexpr unsigned size_ = round_up_to_power_of_2(SIZE) ;
    static constexpr int SHUFFLE_BITS = GetIndexShuffleBits< size_, CACHE_LINE_SIZE / sizeof(State)>::value;
    static constexpr bool maximize_throughput_ = MAXIMIZE_THROUGHPUT;

    alignas(CACHE_LINE_SIZE) std::atomic<unsigned char> states_[size_] = {};
    alignas(CACHE_LINE_SIZE) T elements_[size_] = {};

    T do_pop(unsigned tail) noexcept {
        unsigned index = remap_index<SHUFFLE_BITS>(tail % size_);
        return Base::template do_pop_any(states_[index], elements_[index]);
    }

    template<class U>
    void do_push(U&& element, unsigned head) noexcept {
        unsigned index = remap_index<SHUFFLE_BITS>(head % size_);
        Base::template do_push_any(std::forward<U>(element), states_[index], elements_[index]);
    }

public:
    using value_type = T;

    AtomicQueue2() noexcept = default;
    AtomicQueue2(AtomicQueue2 const&) = delete;
    AtomicQueue2& operator=(AtomicQueue2 const&) = delete;
};