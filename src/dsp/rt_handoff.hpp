#pragma once

#include <atomic>
#include <memory>

// Hands objects from the UI thread to the audio thread without either waiting. The UI
// publishes a new object; the audio thread picks it up at its next acquire() and passes the
// one it replaces back, which the UI frees at its next publish() or collect(). Nothing is
// allocated or freed on the audio thread. Once published, an object belongs to the audio
// thread: use a const T for snapshots it only reads. One UI thread, one audio thread.
// (A template, so it lives in this header.)
template <typename T>
class RtHandoff {
public:
    RtHandoff() = default;
    RtHandoff(const RtHandoff&) = delete;
    RtHandoff& operator=(const RtHandoff&) = delete;
    // Only once the audio thread is done with this object (the graph retires modules that way).
    ~RtHandoff() {
        delete pending_.load();
        delete retired_.load();
        delete current_;
    }

    // UI thread.
    void publish(std::unique_ptr<T> next) {
        collect();
        delete pending_.exchange(next.release(), std::memory_order_acq_rel);
    }
    void collect() { delete retired_.exchange(nullptr, std::memory_order_acq_rel); }

    // Audio thread: the latest object, or nullptr before the first publish(). Keeps the one it
    // has until the UI collected the previous one, so it never has to free anything.
    T* acquire() {
        if (!retired_.load(std::memory_order_acquire)) {
            if (T* next = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
                retired_.store(current_, std::memory_order_release);
                current_ = next;
            }
        }
        return current_;
    }

private:
    std::atomic<T*> pending_{nullptr};
    std::atomic<T*> retired_{nullptr};
    T* current_ = nullptr; // audio thread
};
