#pragma once
#include "common/data_wrapper.h"

// to *.cpp

#include <cstddef>


namespace
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

    static size_t   message::size   (bytes_ptr data) { return data->size + sizeof(header_t); }
    static message* message::dispose(void* where)    { return static_cast<message*>(where); }

    void message::init(id_t id, bytes_ptr send_data)
    {
        hdr_ = header(id, send_data->size());
        memcpy(data(), &(*send_data)[0], send_data->size());
    }

    header_t&   header()    { return hdr_; }
    char*       data  ()    { return reinterpret_cast<char*>(this) + sizeof(header_t); }

private:
    header_t hdr_ ;
};

}

namespace ipc
{
    struct cyclic_queue
    {
        cyclic_queue(const void* base, size_t size)
            : base_(message::dispose(base))
            , head_(message::dispose(base))
            , tail_(message::dispose(base))
            , size_(size)
        {
        }

        bool push(message::id_t id, bytes_ptr data)
        {
            message* end = end();
            size_t new_used_size = used_size_ + data->size();

            if (end + message::size(data) > base_ + size_)
            {
                end = base_;
                new_used_size += base_ + size_ - end; // unused place in the end
            }

            if (new_used_size <= size_)
            {
                end->init(id, data);

                tail_->header().next = end - tail_;
                tail_ = end;

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

        bytes_ptr top () const
        {
            if (empty())
                throw std::runtime_error("no top in cyclic_queue, it's empty");
        }

        bool empty() const
        {
            return head_ == tail_;
        }

    private:
        message* end() const
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
