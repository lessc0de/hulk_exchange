
#ifndef _hulk_exchange_types_h_
#define _hulk_exchange_types_h_

#include <string>

namespace hulk {
namespace exchange {

struct symbol {
    char _data[8];
};

enum state
{
    PENDING_NEW,
    PENDING_CANCEL,
    NEW,
    FILLED,
    CANCELED,
    REJECTED
};

enum side
{
    BUY,
    SELL
};

typedef double px;

typedef unsigned long long qty;

//typedef unsigned long long id;

typedef std::string id;

}
}

#endif
