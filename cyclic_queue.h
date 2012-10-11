#pragma once
#include "common/data_wrapper.h"

// to *.cpp
#include <cstddef>


namespace ipc
{

struct message
{
    typedef size_t id_t;

    #pragma pack(push, 1)
    struct header_t
    {
        id_t        id;
        size_t      size;
        ptrdiff_t   next;

        header_t(id_t id, size_t size)
            : id    (id)
            , size  (size)
            , next  (0)
        {
        }
    };
    #pragma pack(pop)

    static size_t   size   (bytes_ptr data) { return data->size() + sizeof(header_t); }
    static message* dispose(void* where)    { return static_cast<message*>(where); }

    void init(id_t id, bytes_ptr send_data)
    {
        hdr_ = header_t(id, send_data->size());
        memcpy(data(), &(*send_data)[0], send_data->size());
    }

    header_t&       header()        { return hdr_; }
    header_t const& header() const  { return hdr_; }

    char*       data  ()        { return reinterpret_cast<char*>      (this) + sizeof(header_t); }
    char const* data  () const  { return reinterpret_cast<const char*>(this) + sizeof(header_t); }

private:
    header_t hdr_ ;
};

struct cyclic_queue
{
    typedef message::id_t id_t;

    struct iterator
    {
        iterator(message* msg = 0)
            : msg_(msg)
        {}

        iterator& operator++()
        {
            msg_ += msg_->header().next;
            return *this;
        }

        message& operator* () { return *msg_; }
        message* operator->() { return msg_ ; }

        bool operator==(iterator const& other) const { return other.msg_ == msg_ ; }
        bool operator!=(iterator const& other) const { return !operator==(other); }

    private:
        message* msg_;
    };

    cyclic_queue(void* base, size_t size)
        : base_(message::dispose(base))
        , head_(base_)
        , tail_(base_)
        , size_(size)

        , used_space_(0)
    {
        tail_->header().next = (message*)0 - tail_; // 0 - is the end
    }

    bool push(message::id_t id, bytes_ptr data)
    {
        message* next = next_node();
        size_t new_used_size = used_space_ + data->size();

        if (next + message::size(data) > base_ + size_)
        {
            next = base_;
            new_used_size += base_ + size_ - next; // unused place in the end
        }

        if (new_used_size <= size_)
        {
            next->init(id, data);

            tail_->header().next = next - tail_;
            tail_ = next;

            used_space_ = new_used_size;

            return true;
        }

        return false;
    }

    void pop()
    {
        if (empty())
            throw std::runtime_error("nothing to pop from cyclic_queue");

        if (head_->header().next < 0) // the end of the buffer
            used_space_ -= base_ + size_ - head_;
        else
            used_space_ -= head_->header().size;

        head_ += head_->header().next;
    }

    message& top () const
    {
        if (empty())
            throw std::runtime_error("no top in cyclic_queue, it's empty");

        return *head_;
    }

    bool empty() const
    {
        return head_ == tail_;
    }

    void swap(cyclic_queue& other)
    {
        std::swap(other.base_, base_);
        std::swap(other.head_, head_);
        std::swap(other.tail_, tail_);
        std::swap(other.size_, size_);
        std::swap(other.used_space_, used_space_);
    }

    void clear()
    {
        cyclic_queue tmp(base_, size_);
        swap(tmp);
    }

    iterator begin()
    {
        return iterator(head_);
    }

    iterator end()
    {
        return iterator();
    }

private:
    message* next_node() const
    {
        if (empty())
            return tail_;

        return tail_ + tail_->header().size;
    }

private:
    message* base_;
    message* head_;
    message* tail_;
    size_t   size_;

private:
    size_t used_space_;
};


} //namespace ipc
