#pragma once

#include <atomic>
#include <memory>

namespace tes {
namespace execution {

// 无锁单生产者单消费者队列
template<typename T>
class LockFreeQueue {
public:
    struct Node {
        std::atomic<T*> data;
        std::atomic<Node*> next;
        
        Node() : data(nullptr), next(nullptr) {}
    };
    
    LockFreeQueue() {
        Node* dummy = new Node;
        head_.store(dummy);
        tail_.store(dummy);
    }
    
    ~LockFreeQueue() {
        while (Node* const old_head = head_.load()) {
            head_.store(old_head->next);
            delete old_head;
        }
    }
    
    void enqueue(T item) {
        Node* new_node = new Node;
        T* data = new T(std::move(item));
        new_node->data.store(data);
        
        Node* prev_tail = tail_.exchange(new_node);
        prev_tail->next.store(new_node);
    }
    
    bool dequeue(T& result) {
        Node* head = head_.load();
        Node* next = head->next.load();
        
        if (next == nullptr) {
            return false;
        }
        
        T* data = next->data.load();
        if (data == nullptr) {
            return false;
        }
        
        result = *data;
        delete data;
        head_.store(next);
        delete head;
        
        return true;
    }
    
    bool empty() const {
        Node* head = head_.load();
        Node* next = head->next.load();
        return next == nullptr;
    }
    
    size_t size() const {
        size_t count = 0;
        Node* current = head_.load()->next.load();
        while (current != nullptr) {
            count++;
            current = current->next.load();
        }
        return count;
    }
    
private:
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    
    // 禁止拷贝和赋值
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
};

// 多生产者多消费者无锁队列
template<typename T>
class MPMCLockFreeQueue {
public:
    struct Node {
        std::atomic<T*> data;
        std::atomic<Node*> next;
        
        Node() : data(nullptr), next(nullptr) {}
    };
    
    MPMCLockFreeQueue() {
        Node* dummy = new Node;
        head_.store(dummy);
        tail_.store(dummy);
    }
    
    ~MPMCLockFreeQueue() {
        while (Node* const old_head = head_.load()) {
            head_.store(old_head->next);
            delete old_head;
        }
    }
    
    void enqueue(T item) {
        Node* new_node = new Node;
        T* data = new T(std::move(item));
        new_node->data.store(data);
        
        Node* prev_tail = tail_.exchange(new_node);
        prev_tail->next.store(new_node);
    }
    
    bool dequeue(T& result) {
        Node* head = head_.load();
        
        while (true) {
            Node* next = head->next.load();
            if (next == nullptr) {
                return false;
            }
            
            if (head_.compare_exchange_weak(head, next)) {
                T* data = next->data.load();
                if (data != nullptr) {
                    result = *data;
                    delete data;
                    delete head;
                    return true;
                }
                delete head;
                head = head_.load();
            }
        }
    }
    
    bool empty() const {
        Node* head = head_.load();
        Node* next = head->next.load();
        return next == nullptr;
    }
    
    size_t size() const {
        size_t count = 0;
        Node* current = head_.load()->next.load();
        while (current != nullptr) {
            count++;
            current = current->next.load();
        }
        return count;
    }
    
private:
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    
    // 禁止拷贝和赋值
    MPMCLockFreeQueue(const MPMCLockFreeQueue&) = delete;
    MPMCLockFreeQueue& operator=(const MPMCLockFreeQueue&) = delete;
};

} // namespace execution
} // namespace tes