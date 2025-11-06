#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

extern "C" {
#include <libavutil/frame.h>
}

#include <queue>
#include <mutex>
#include <condition_variable>

class FrameQueue {
public:
    FrameQueue(size_t max_size = 3) : max_size_(max_size) {}
    
    ~FrameQueue() {
        clear();
    }
    
    bool push(AVFrame* frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Drop frames if queue is full (for low latency)
        if (frames_.size() >= max_size_) {
            // Drop the oldest frame
            AVFrame* old_frame = frames_.front();
            frames_.pop();
            av_frame_free(&old_frame);
        }
        
        // Clone the frame
        AVFrame* cloned_frame = av_frame_alloc();
        if (!cloned_frame || av_frame_ref(cloned_frame, frame) < 0) {
            av_frame_free(&cloned_frame);
            return false;
        }
        
        frames_.push(cloned_frame);
        condition_.notify_one();
        return true;
    }
    
    AVFrame* pop(int timeout_ms = 10) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (condition_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                               [this] { return !frames_.empty(); })) {
            AVFrame* frame = frames_.front();
            frames_.pop();
            return frame;
        }
        
        return nullptr;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!frames_.empty()) {
            AVFrame* frame = frames_.front();
            frames_.pop();
            av_frame_free(&frame);
        }
    }
    
    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_.size();
    }

private:
    std::queue<AVFrame*> frames_;
    std::mutex mutex_;
    std::condition_variable condition_;
    size_t max_size_;
};

#endif // FRAMEQUEUE_H