#pragma once
#include "common/data_wrapper.h"

// to *.cpp
#include <cstddef>


namespace ipc
{

struct message
{
    typedef size_t id_t;
    const static size_t npos = size_t(-1);

    #pragma pack(push, 1)
    struct header_t
    {
        id_t    id;
        size_t  size;
        size_t  next; // offset from base

        header_t(id_t id, size_t size)
            : id    (id)
            , size  (size)
            , next  (npos)
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
        iterator(char* base = 0, size_t offset = message::npos)
            : base_  (base  )
            , offset_(offset)
        {}

        iterator& operator++()
        {
            offset_ = (*this)->header().next;
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

    cyclic_queue(void* base, size_t size)
        : base_(static_cast<char*>(base))
        , hdr_ (new (base_ + size - sizeof(header_t)) header_t(size))
    {
    }

    bool push(message::id_t id, bytes_ptr data)
    {
        size_t next          = next_offset();
        size_t new_used_size = hdr_->used_space + data->size();

        if (next + message::size(data) > hdr_->size)
        {
            next = 0;
            new_used_size += hdr_->size - next; // unused place in the end
        }

        if (new_used_size <= hdr_->size)
        {
            if (!empty())
                tail()->header().next = next;

            message::dispose(base_ + next)->init(id, data);

            hdr_->used_space = new_used_size;
            hdr_->tail       = next;

            return true;
        }

        return false;
    }

    void pop()
    {
        if (empty())
            throw std::runtime_error("nothing to pop from cyclic_queue");

        if (head()->header().next < hdr_->head) // the end of the buffer
            hdr_->used_space -= hdr_->size - hdr_->head;
        else
            hdr_->used_space -= head()->header().size;

        hdr_->head = (hdr_->used_space == 0) ? 0 : head()->header().next;
    }

    message& top () const
    {
        if (empty())
            throw std::runtime_error("no top in cyclic_queue, it's empty");

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

private:
    message* head() const { return message::dispose(base_ + hdr_->head); }
    message* tail() const { return message::dispose(base_ + hdr_->tail); }

private:
    size_t next_offset()
    {
        if (empty())
            return hdr_->head;

        return hdr_->tail + tail()->header().size;
    }

private:
    struct header_t
    {
        header_t(size_t sz)
            : size(sz - sizeof(header_t))
        {
            Assert(sz >= sizeof(header_t));
        }

        size_t head; // offset from base
        size_t tail; // valid only in case of !empty()
        size_t size;
        size_t used_space;
    };

private:
    char*       base_;
    header_t*   hdr_;
};


} //namespace ipc
