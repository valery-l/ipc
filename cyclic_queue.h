#pragma once
#include "common/data_wrapper.h"

// to *.cpp
#include <cstddef>


namespace ipc
{

#pragma pack(push, 1)
struct message
{
    typedef size_t id_t;
    const static size_t npos = size_t(-1);

    static size_t   size   (data_wrap::bytes_ptr data)  { return data_wrap::size(data) + sizeof(message); }
    static message* dispose(void* where)                { return static_cast<message*>(where); }

    void init(id_t id, data_wrap::bytes_ptr send_data)
    {
        id_         = id;
        data_size_  = send_data->size();
        next_       = npos;

        memcpy(data(), data_wrap::data(send_data), data_wrap::size(send_data));
    }

    id_t&   id       ()       { return id_; }
    size_t  msg_size () const { return data_size_ + sizeof(*this); }
    size_t  data_size() const { return data_size_; }
    size_t& next     ()       { return next_; }

    char*       data  ()        { return reinterpret_cast<char*>      (this) + sizeof(*this); }
    char const* data  () const  { return reinterpret_cast<const char*>(this) + sizeof(*this); }

private:
    id_t    id_;
    size_t  data_size_;
    size_t  next_; // offset from base
};
#pragma pack(pop)

struct cyclic_queue
{
    typedef message::id_t id_t;

    struct iterator
    {
        iterator(char* base = 0, size_t offset = message::npos)
            : base_  (base  )
            , offset_(offset)
        {}

        iterator& operator++()
        {
            offset_ = (*this)->next();
            return *this;
        }

        message* operator->() { return message::dispose(base_ + offset_); }
        message& operator* () { return *operator->(); }

        bool operator==(iterator const& other) const { return other.offset_ == offset_; }
        bool operator!=(iterator const& other) const { return !operator==(other); }

    private:
        char*   base_  ;
        size_t  offset_;
    };

    cyclic_queue(void* base, size_t size, bool owns)
        : base_(static_cast<char*>(base))
        , hdr_ (new (base_ + size - sizeof(header_t)) header_t(size, owns))
    {
    }

    void push(message::id_t id, data_wrap::bytes_ptr data)
    {
        size_t next          = next_offset();
        size_t new_used_size = hdr_->used_space + message::size(data);

        if (next + message::size(data) > hdr_->size)
        {
            next = 0;
            new_used_size += hdr_->size - next; // unused place in the end
        }

        VerifyMsg(new_used_size <= hdr_->size, "no more place to push msg");

        if (!empty())
            tail()->next() = next;

        message::dispose(base_ + next)->init(id, data);

        hdr_->used_space = new_used_size;
        hdr_->tail       = next;
    }

    void pop()
    {
        VerifyMsg(!empty(), "nothing to pop from cyclic_queue");

        size_t next = head()->next();

        if (next == message::npos) // last message
            hdr_->used_space = 0;
        else if (next < hdr_->head) // the end of the buffer
            hdr_->used_space -= hdr_->size - hdr_->head;
        else
            hdr_->used_space -= next - hdr_->head;

        hdr_->head = (hdr_->used_space == 0) ? 0 : next;
    }

    message& top() const
    {
        VerifyMsg(!empty(), "no top, cyclic_queue is empty");
        return *head();
    }

    bool empty() const
    {
        return hdr_->used_space == 0;
    }

    void clear()
    {
        hdr_->head       = 0;
        hdr_->tail       = 0;
        hdr_->used_space = 0;
    }

    iterator begin()
    {
        return empty() ? iterator() : iterator(base_, hdr_->head);
    }

    iterator end()
    {
        return iterator();
    }

    bool is_valid () const
    {
        return !hdr_->failed;
    }

    void set_invalid()
    {
        hdr_->failed = true;
    }

private:
    message* head() const
    {
        Verify(!empty() || hdr_->head  == 0);
        return message::dispose(base_ + hdr_->head);
    }

    message* tail() const
    {
        Verify(!empty());
        return message::dispose(base_ + hdr_->tail);
    }

private:
    size_t next_offset()
    {
        if (empty())
            return hdr_->head;

        return hdr_->tail + tail()->msg_size();
    }

private:
    #pragma pack(push, 1)
    struct header_t
    {
        header_t(size_t sz, bool owns)
        {
            if (owns)
                size = sz - sizeof(header_t);

            Assert(sz >= sizeof(header_t));
        }

        size_t head; // offset from base
        size_t tail; // valid only in case of !empty()
        size_t size;
        size_t used_space;
        bool   failed;
    };
    #pragma pack(pop)

private:
    char*       base_;
    header_t*   hdr_;
};


} //namespace ipc
