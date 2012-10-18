#include "cyclic_queue.h"

namespace ipc
{

cyclic_queue::cyclic_queue(void* base, size_t size, bool owns)
    : base_(static_cast<char*>(base))
    , hdr_ (new (base_ + size - sizeof(header_t)) header_t(size, owns))
{
}

void cyclic_queue::push(message::id_t id, data_wrap::bytes_ptr data)
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

void cyclic_queue::pop()
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

message& cyclic_queue::top() const
{
    VerifyMsg(!empty(), "no top, cyclic_queue is empty");
    return *head();
}

bool cyclic_queue::empty() const
{
    return hdr_->used_space == 0;
}

void cyclic_queue::clear()
{
    hdr_->head       = 0;
    hdr_->tail       = 0;
    hdr_->used_space = 0;
}

cyclic_queue::iterator cyclic_queue::begin()
{
    return empty() ? iterator() : iterator(base_, hdr_->head);
}

cyclic_queue::iterator cyclic_queue::end()
{
    return iterator();
}

bool cyclic_queue::is_valid () const
{
    return !hdr_->failed;
}

void cyclic_queue::set_invalid()
{
    hdr_->failed = true;
}

message* cyclic_queue::head() const
{
    Verify(!empty() || hdr_->head  == 0);
    return message::dispose(base_ + hdr_->head);
}

message* cyclic_queue::tail() const
{
    Verify(!empty());
    return message::dispose(base_ + hdr_->tail);
}

size_t cyclic_queue::next_offset()
{
    if (empty())
        return hdr_->head;

    return hdr_->tail + tail()->msg_size();
}

} // ipc
