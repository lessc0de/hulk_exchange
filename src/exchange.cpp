
#include "hulk/core/logger.h"

#include "hulk/fix/acceptor.h"
#include "hulk/fix/session_factory.h"
#include "hulk/fix/tcp.h"

#include "hulk/exchange/order.h"
#include "hulk/exchange/orderbook.h"

#include <sys/socket.h>
#include <set>

using namespace hulk;
using namespace hulk::exchange;

log& l = logger::instance().get( "hulk.exchange" );

// -----------------------------------------------------------------------------
bool fill( order& new_order, order& book_order )
{
    order* buy;
    order* sell;

    if( new_order._side == BUY ) {
        buy = &new_order; sell = &book_order;
    } else {
        buy = &book_order; sell = &new_order;
    }

    if( sell->_px <= buy->_px )
    {
        LOG_DEBUG( l, "matched! " <<
            "B " << buy->_order_qty << " @ " << buy->_px << " versus " <<
            "S " << sell->_order_qty << " @ " << sell->_px );

        qty qty = std::min( new_order._leaves_qty, book_order._leaves_qty );
        new_order.fill( qty, book_order._px );
        book_order.fill( qty, book_order._px );

        return true;
    }

    return false;
}

template< typename TOrders >
void match( order& order, orderbook& book, TOrders& book_orders )
{
    std::vector< typename TOrders::iterator > to_del;
    ::hulk::exchange::order* book_order;

    typename TOrders::iterator it = book_orders.begin();
    while( it != book_orders.end() && !order.is_filled() )
    {
        book_order = it->second;

        if( !fill( order, *book_order ) ) {
            LOG_INFO( l, "no match!" ); break;
        }

        if( book_order->is_filled() ) {
            to_del.push_back( it );
        }

        ++it;
    }

    for( int i=0; i<to_del.size(); i++ ) {
        book.del( to_del[i] );
    }
}

void process( order& order, orderbook& book )
{
    order.ack();

    if( order._side == BUY ) {
        match( order, book, book.get_sell_orders() );
    } else {
        match( order, book, book.get_buy_orders() );
    }

    if( !order.is_filled() ) {
        book.add( order );
    }
}

// -----------------------------------------------------------------------------
struct my_order_callback : public order::callback
{
    void build_msg( const order& o, fix::fields& body, const fix::value& exec_type )
    {
        body.push_back( fix::field( 11, o._id ) );
        body.push_back( fix::field( 38, o._order_qty ) );
        body.push_back( fix::field( 44, o._px ) );
        body.push_back( fix::field( 54, o._side == BUY ? 1 : 2 ) );
        body.push_back( fix::field( 55, o._symbol ) );
        body.push_back( fix::field( 150, exec_type ) );
    }

    virtual void on_ack( const order& o )
    {
        LOG_INFO( l, "ack " << o._id );

        fix::session* session = o.get_session();
        fix::fields body;
        if( o._state == NEW ) {
            build_msg( o, body, "0" );
        } else {
            build_msg( o, body, "4" );
        }

        std::string buf;
        session->send( "8", body, &buf );
    }

    virtual void on_fill( const order& o, qty qty, px px )
    {
        LOG_INFO( l, "fill " << qty << " @ " << px << " leaves " << o._leaves_qty );

        fix::session* session = o.get_session();
        fix::fields body;
        build_msg( o, body, "F" );
        body.push_back( fix::field( 31, px ) );
        body.push_back( fix::field( 32, qty ) );

        std::string buf;
        session->send( "8", body, &buf );
    }
};

// -----------------------------------------------------------------------------
typedef std::map< id, order* > id_to_order_map;
id_to_order_map txn_to_order;

std::map< std::string, orderbook > orderbooks;

class order_entry_session : public fix::session
{
public:
    order_entry_session()
    {
        fix::fields header;
        header.push_back( fix::field( 49, "HULK-EXCHANGE" ) );
        header.push_back( fix::field( 56, "HULK-CLIENT" ) );
        set_protocol( "FIX.4.4" );
        set_header( header );

        LOG_INFO( l, "created order_entry_session" );
    }

    virtual void on_recv( const fix::fields& msg, const std::string buf )
    {
        LOG_INFO( l, "parsing " << msg.size() << " fields" );

        std::string msg_type;
        std::string symbol;
        id txn_id;
        qty qty;
        px px;
        side side;

        for( int i=0; i<msg.size(); i++ )
        {
            switch( msg[i]._tag )
            {
                case 35: msg_type = msg[i]._value; break;
                case 11: txn_id = msg[i]._value; break;
                case 38: qty = atoi( msg[i]._value.c_str() ); break;
                case 44: px = atof( msg[i]._value.c_str() ); break;
                case 54: side = ( msg[i]._value[0] == '1' ? BUY : SELL ); break;
                case 55: symbol = msg[i]._value; break;
            }
        }

        orderbook& book = orderbooks[symbol];

        LOG_DEBUG( l,
            "msg_type=" << msg_type << ", " <<
            "txn_id=" << txn_id << ", " <<
            "qty=" << qty << ", " <<
            "px=" << px << ", " <<
            "side=" << ( side == BUY ? "B" : "S" ) << ", " <<
            "symbol=" << symbol );

        if( msg_type == "D" )
        {
            LOG_INFO( l, "new order " << txn_id );

            order* o = new order( symbol, txn_id, side, qty, px );
            o->set_callback( cb );
            o->set_session( *this );
            process( *o, book );

            if( !o->is_filled() ) {
                txn_to_order[ txn_id ] = o;
            }
        }
        else
        if( msg_type == "F" )
        {
            LOG_INFO( l, "cxl order " << txn_id );

            id_to_order_map::iterator it = txn_to_order.find( txn_id );
            if( it != txn_to_order.end() )
            {
                order* o = it->second;
                o->cancel();
                o->ack();

                txn_to_order.erase( txn_id );
                book.del( *o );
            }
        }
        else
        {
            LOG_INFO( l, "ignored msg_type " << msg_type );
        }

        LOG_INFO( l, "book " << symbol << " has "
            << book.get_buy_orders().size() << " buys and "
            << book.get_sell_orders().size() << " sells" );
    }

    my_order_callback cb;
};

class order_entry_session_factory : public fix::session_factory
{
public:
    virtual fix::session* create()
    {
        return new order_entry_session;
    }
};

// -----------------------------------------------------------------------------
int main( int argc, char** argv )
{
    int port = 5554;
    LOG_INFO( l, "hulk exchange starting" );

    shared_ptr< fix::session_factory > factory( new order_entry_session_factory );
    shared_ptr< fix::transport_callback > acceptor( new fix::acceptor( factory ) );
    fix::tcp_event_loop eloop;
    eloop.new_acceptor( port, acceptor );

    LOG_INFO( l, "starting main loop" );
    while( 1 )
    {
        eloop.loop( 100 );
    }
}