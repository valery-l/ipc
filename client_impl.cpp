#include "client_impl.h"


typedef function<void(const void*, size_t)> on_receive_f;
typedef function<void()>                    on_event_f; // connected, disconnected

client_impl(
    string name,
    on_receive_f const& on_recv,
    on_event_f const& on_connected,
    on_event_f const& on_disconnected);

~client_impl();

void send(void const* data, size_t size);

optional<table_sync   > table_sync_;
optional<server2client> recv_sync_;
optional<client2server> send_sync_;

boost::thread   sending_;
boost::thread   receiving_;

// TODO:
live_table*  table_;
char*        buffer_;


client_impl(
    string name,
    on_receive_f const& on_recv,
    on_event_f const& on_connected,
    on_event_f const& on_disconnected)

    :
{
}

    void send(const void* data, size_t size); // in main thread

private:

    bool connect()
    {
        sync_->


    }

    bool is_connected();
    bool disconnect();

    void recv_loop()
    {
        named_mutex mut(open_or_create, common_mutex_name);

        try
        {
            while(connected_)
            {
                try
                {
                    scoped_lock<named_mutex> lock(mut, boost::posix_time::seconds(ipc_traits::mutex_timeout));


                }
                catch(interprocess_exception const&) // shit happens
                {

                }
            }
        }
        catch(boost::thread_interrupted const& )
        {
        }
    }

private:
    bool connected_;

private:
    optional<table_sync>    table_sync_;

    boost::thread           recv_thread_;
};
