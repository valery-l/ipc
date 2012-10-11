#pragma once

#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "ipc_common.h"

namespace ipc
{

template<class resource_t>
struct remover
{
    explicit remover(string name)
        : name(name)
    {
        // cause if someone crashes with locked mutex, they will be cleaned by server on its startup
        apply();
    }

    remover(remover&& other)
        : name(move(other.name))
    {
    }

   ~remover()
    {
        apply();
    }

    void apply()
    {
        if (!name.empty())
            resource_t::remove(name.c_str());
    }

private:
    string name;
};


template<class resource_t>
struct auto_rm
{
    auto_rm(const char* name, bool auto_remove)
        : remover_  (auto_remove ? name : "")
        , resource_ (open_or_create, name)
    {
    }

    auto_rm(auto_rm&& other)
        : remover_  (move(other.remover_ ))
        , resource_ (move(other.resource_))
    {
    }

    resource_t& get()       { return resource_; }
    resource_t& operator* (){ return get();     }
    resource_t* operator->(){ return &resource_;}

private:
    remover<resource_t> remover_ ; // should be invoked before resource constructor, and after its destructor
    resource_t          resource_;
};


struct shared_buffer
    : noncopyable
{
    shared_buffer(string name, size_t size, bool owns)
        : remover_  (owns ? name : "")
        , size_     (size)
    {
        if (owns)
            create_mem(name);
        else
            open_mem  (name);
    }

    shared_buffer(shared_buffer&& other)
        : remover_(move(other.remover_))
        , mem_    (move(other.mem_    ))
        , reg_    (move(other.reg_    ))
    {
    }

    void* pointer() const
    {
        return reg_.get_address();
    }

    void clear()
    {
        memset(pointer(), 0, size_);
    }

private:
    void create_mem(string name)
    {
        shared_memory_object mem(create_only, name.c_str(), read_write);
        mem.truncate(size_);

        mapped_region reg(mem, read_write);
        memset(reg.get_address(), 0, size_);

        mem_ = move(mem);
        reg_ = move(reg);
    }

    void open_mem(string name)
    {
        shared_memory_object mem(open_only, name.c_str(), read_write);
        mapped_region reg(mem, read_write);

        mem_ = move(mem);
        reg_ = move(reg);
    }

private:
    remover<shared_memory_object>   remover_;
    size_t                          size_;
    shared_memory_object            mem_;
    mapped_region                   reg_;
};




template<class primitive>
bool create(optional<primitive>& result)
{
    try
    {
        result = in_place(true);
        return true;
    }
    catch(interprocess_exception const&)
    {
        return false;
    }
}

template<class primitive>
bool open(optional<primitive>& result)
{
    try
    {
        result = in_place(false);
        return true;
    }
    catch(interprocess_exception const&)
    {
        return false;
    }
}

} // namespace ipc
