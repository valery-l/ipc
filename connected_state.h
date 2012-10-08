#pragma once

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include "live_table.h"

namespace ipc
{
    struct shared_connected_state
    {
        shared_connected_state()
            : connected_(false)
        {
        }

        bool set_connected(bool connected) // returns previous connection state
        {
            boost::mutex::scoped_lock lock(mutex_);
            bool was_connected = connected_;

            connected_ = connected;

            if (!was_connected && connected)
                condvar_.notify_one();

            return was_connected;
        }

        void is_connected()
        {
            boost::mutex::scoped_lock lock(mutex_);
            return connected_;
        }

        void wait_for_connected()
        {
            boost::mutex::scoped_lock lock(mutex_);

            while (!connected_)
                condvar_.wait(lock);
        }

    private:
        bool                        connected_;
        boost::mutex                mutex_;
        boost::condition_variable   condvar_;
    };


} // namespace ipc
