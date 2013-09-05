
#ifndef _hulk_exchange_events_h_
#define _hulk_exchange_events_h_

#include "hulk/exchange/types.h"

namespace hulk {
namespace exchange {

struct input_event
{
    enum type {
        NEW_ORDER, CANCEL_ORDER
    };

    struct header {
        type _type;
    };

    struct new_order
    {
        id _transaction_id;
        symbol _symbol;
        side _side;
        qty _qty;
        px _px;
    };

    struct cancel_order {
        id _transaction_id;
    };

    union body
    {
        new_order _new_order;
        cancel_order _cancel_order;
    };

    header _header;
    body _body;
};

struct output_event
{
    enum type {
        ACK_ORDER, REJECT_ORDER, FILL_ORDER
    };

    struct header {
        type _type;
    };

    struct ack_order {
        id _transaction_id;
    };

    struct reject_order {
        id _transaction_id;
    };

    struct fill_order
    {
        id _transaction_id;
        qty _qty;
        px _px;
    };

    union body
    {
        ack_order _ack_order;
        reject_order _reject_order;
        fill_order _fill_order;
    };

    header _header;
    body _body;
};

}
}

#endif

