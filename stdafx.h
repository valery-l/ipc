//#define _WIN32_WINNT 0x0601
//#define POINTER_64 __ptr64

#include "common/stl.h"
#include "common/boost.h"

#define LOG_ASSERT
#include "logger/logger.hpp"

#include <boost/asio.hpp>
#include <boost/date_time.hpp>
#include <boost/thread.hpp>

namespace posix_time = boost::posix_time;
using namespace boost::asio;
using namespace boost::asio::ip;

using boost::none;
using boost::system::error_code;

using std::forward;
using std::move;

#include <QEvent>
#include <QObject>
#include <QCoreApplication>
