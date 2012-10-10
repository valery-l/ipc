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
    {
        if (owns)
            create_mem();
        else
            open_mem  ();
    }

    shared_buffer(shared_buffer&& other)
        : remover_(move(other.remover_))
        , mem_    (move(other.mem_    ))
        , reg_    (move(other.reg_    ))
    {
    }

    const void* pointer() const
    {
        return reg.get_address();
    }

    void clear()
    {
        memset(pointer(), 0, mem_.get_size());
    }

    template<class mutex_t>
    static optional<shared_buffer> safe_create(string name, size_t size, bool owns, mutex_t& mutex)
    {
        scoped_lock<mutex_t> lock(mutex, time_after_ms(wait_timeout_ms));
        if (lock.owns())
        {
            bool create = true;
            for(;;)
            {
                try
                {
                    shared_memory_object mem(create ? create_only : open_only, name, read_write);
                    mem.truncate(size);

                    mapped_region reg(smo, read_write);

                    if (create)
                        memset(reg.get_address(), 0, size);

                    return shared_buffer(name, std::move(mem), std::move(reg), owns);
                }
                catch(interprocess_exception const&)
                {
                    create = !create;
                }
            }
        }
        else
            return boost::none;
    }

private:
    shared_buffer(string name, shared_memory_object&& mem, mapped_region&& reg, bool owns)
        : remover_  (owns ? name : "")
        , mem_      (forward(mem))
        , reg_      (forward(reg))
    {
    }

private:
    void create_mem(string name, size_t size)
    {
        shared_memory_object mem(create_only, name.c_str(), read_write);
        mem.truncate(size);

        mapped_region reg(smo, read_write);
        memset(reg.get_address(), 0, size);

        mem_ = move(mem);
        reg_ = move(reg);
    }

    void open_mem(string name, size_t size)
    {
        shared_memory_object mem(open_only, name.c_str(), read_write);
        mapped_region reg(smo, read_write);

        mem_ = move(mem);
        reg_ = move(reg);
    }



protected:
    remover<shared_memory_object>   remover_;
    shared_memory_object            mem_;
    mapped_region                   reg_;
};

} // namespace ipc
