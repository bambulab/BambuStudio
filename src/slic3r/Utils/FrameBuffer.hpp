#ifndef slic3r_Utils_FrameBuffer_hpp_
#define slic3r_Utils_FrameBuffer_hpp_

#include <deque>
#include <mutex>
#include <utility>
#include <boost/log/trivial.hpp>


namespace Slic3r {
namespace Utils {

template<typename T>
class FixedOverwriteBuffer {
public:
    explicit FixedOverwriteBuffer(size_t capacity) : m_capacity(capacity), m_is_ready_to_read(false) {

    }

    void enqueue(T &&value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        enqueue_impl(std::forward<T>(value));
    }

    void enqueue(const T &value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        enqueue_impl(value);
    }

    bool can_read() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_is_ready_to_read && !m_deque.empty();
    }

    bool try_dequeue(T &out_value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_is_ready_to_read || m_deque.empty()) {
            return false;
        }
        out_value = std::move(m_deque.front());
        m_deque.pop_front();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_deque.size();
    }

    size_t capacity() const {
        return m_capacity;
    }

    bool is_full() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_deque.size() == m_capacity;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deque.clear();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deque.clear();
        m_is_ready_to_read = false;
    }

    bool set_capacity(size_t new_capacity) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (new_capacity == 0) {
            return false;
        }
        if (new_capacity == m_capacity) {
            return true;
        }
        m_capacity = new_capacity;
        return true;
    }

private:
    template<typename U>
    void enqueue_impl(U &&value) {
        if (m_deque.size() >= m_capacity) {
            BOOST_LOG_TRIVIAL(warning) << "Framebuffer: drop frame, deque size = " << m_deque.size();
            m_deque.pop_front();
        }
        m_deque.push_back(std::forward<U>(value));
        if (m_deque.size() == m_capacity) {
            m_is_ready_to_read = true;
        }
    }

    std::deque<T>      m_deque;
    size_t                m_capacity;
    mutable std::mutex m_mutex;
    bool               m_is_ready_to_read;
};

} // namespace Utils
} // namespace Slic3r

#endif // slic3r_Utils_FrameBuffer_hpp_
