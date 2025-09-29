#ifndef COMPAT_H
#define COMPAT_H

#include <memory>
#include <mutex>

// C++14 std::make_unique compatibility for GCC 4.8.5
#if __cplusplus < 201402L

namespace std {
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

    // shared_mutex compatibility (simplified implementation using mutex)
    class shared_mutex {
    private:
        std::mutex mtx_;
    public:
        void lock() { mtx_.lock(); }
        void unlock() { mtx_.unlock(); }
        bool try_lock() { return mtx_.try_lock(); }

        // For shared locking, we'll use exclusive locking (less optimal but compatible)
        void lock_shared() { mtx_.lock(); }
        void unlock_shared() { mtx_.unlock(); }
        bool try_lock_shared() { return mtx_.try_lock(); }
    };

    // shared_lock compatibility
    template<typename Mutex>
    class shared_lock {
    private:
        Mutex* mtx_;
        bool owns_lock_;
    public:
        explicit shared_lock(Mutex& m) : mtx_(&m), owns_lock_(true) {
            mtx_->lock_shared();
        }

        ~shared_lock() {
            if (owns_lock_) {
                mtx_->unlock_shared();
            }
        }

        shared_lock(const shared_lock&) = delete;
        shared_lock& operator=(const shared_lock&) = delete;
    };
}

#endif // __cplusplus < 201402L

#endif // COMPAT_H