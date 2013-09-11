#include "hulk/core/logger.h"
#include "hulk/fix/tcp.h"
#include "hulk/exchange/order.h"
#include "hulk/exchange/orderbook.h"

#include <set>

using namespace hulk;
using namespace hulk::exchange;

core::log& log = core::logger::instance().get( "hulk.exchange" );

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
        LOG_DEBUG( log, "matched! " <<
            "B " << buy->_order_qty << " @ " << buy->_px << " versus " <<
            "S " << sell->_order_qty << " @ " << sell->_px );

        qty qty = std::min( new_order._leaves_qty, book_order._leaves_qty );
        new_order.fill( qty, book_order._px );
        book_order.fill( qty, book_order._px );

        return true;
    }
    else
    {
        LOG_DEBUG( log, "no match! " <<
            "B " << buy->_order_qty << " @ " << buy->_px << " versus " <<
            "S " << sell->_order_qty << " @ " << sell->_px );

        return false;
    }
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
            LOG_INFO( log, "no match!" ); break;
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
struct drop_copy_callback : core::tcp::callback
{
    virtual void on_open( int fd )
    {
        LOG_INFO( log, "drop copy client connected" );
        _fds.insert( fd );
    }

    virtual void on_close( int fd )
    {
        LOG_INFO( log, "drop copy client disconnected" );
        _fds.erase( fd );
    }

    void publish( const std::string& s )
    {
        LOG_INFO( log, "publishing to " << _fds.size() << " clients" );

        std::set< int >::iterator it = _fds.begin();
        while( it != _fds.end() )
        {
            ::send( *it, (s+"\r\n").c_str(), s.size(), 0 );
            ++it;
        }
    }

    std::set< int > _fds;
};

drop_copy_callback* dc_callback = new drop_copy_callback;

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
        LOG_INFO( log, "ack " << o._id );

        fix::session* session = o.get_session();
        fix::fields body;
        if( o._state == NEW ) {
            build_msg( o, body, "0" );
        } else {
            build_msg( o, body, "4" );
        }

        std::string buf;
        session->send( "8", body, &buf );
        dc_callback->publish( buf );
    }

    virtual void on_fill( const order& o, qty qty, px px )
    {
        LOG_INFO( log, "fill " << qty << " @ " << px << " leaves " << o._leaves_qty );

        fix::session* session = o.get_session();
        fix::fields body;
        build_msg( o, body, "F" );
        body.push_back( fix::field( 31, px ) );
        body.push_back( fix::field( 32, qty ) );

        std::string buf;
        session->send( "8", body, &buf );
        dc_callback->publish( buf );
    }
};

// -----------------------------------------------------------------------------
struct my_orderbook_callback : public orderbook::callback
{
    typedef std::map< px, qty, std::greater< px > > buy_orders;
    typedef std::map< px, qty, std::less< px > > sell_orders;

    buy_orders _buy_orders;
    sell_orders _sell_orders;

    virtual void on_add( const orderbook& book, const order& order )
    {
        if( order._side == BUY ) {
            _buy_orders[order._px] += order._leaves_qty;
        } else {
            _sell_orders[order._px] += order._leaves_qty;
        }
    }

    virtual void on_del( const orderbook& book, const order& order )
    {
        if( order._side == BUY ) {
            _buy_orders[order._px] -= order._leaves_qty;
        } else {
            _sell_orders[order._px] -= order._leaves_qty;
        }
    }

    std::string& to_string( std::string& s )
    {
        std::stringstream ss;

        for( buy_orders::iterator it = _buy_orders.begin(); it != _buy_orders.end(); it++ ) {
            ss << "B " << it->second << " @ " << it->first << "\n";
        }

        for( sell_orders::iterator it = _sell_orders.begin(); it != _sell_orders.end(); it++ ) {
            ss << "S " << it->second << " @ " << it->first << "\n";
        }

        s = ss.str();

        return s;
    }
};

// -----------------------------------------------------------------------------
typedef std::map< id, order* > id_to_order_map;
id_to_order_map txn_to_order;

std::map< std::string, orderbook > orderbooks;
std::map< std::string, my_orderbook_callback > orderbook_callbacks;

class order_entry_session : public fix::session
{
public:
    order_entry_session( fix::transport& transport )
    : fix::session( transport ), num_recvd( 0 )
    {
    }

    virtual void recv( const fix::fields& msg, const std::string buf )
    {
        dc_callback->publish( buf );

        LOG_INFO( log, "parsing " << msg.size() << " fields" );

        if( num_recvd++ == 0 )
        {
            fix::fields header;
            header.push_back( fix::field( 49, "HULK-EXCHANGE" ) );
            header.push_back( fix::field( 56, "HULK-CLIENT" ) );
            set_protocol( "FIX.4.4" );
            set_header( header );
        }

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
                case 35: msg_type = msg[i]._value;
                case 11: txn_id = msg[i]._value; break;
                case 38: qty = atoi( msg[i]._value.c_str() ); break;
                case 44: px = atof( msg[i]._value.c_str() ); break;
                case 54: side = ( msg[i]._value[0] == '1' ? BUY : SELL ); break;
                case 55: symbol = msg[i]._value; break;
            }
        }

        orderbook& book = orderbooks[symbol];
        book.set_callback( orderbook_callbacks[symbol] );

        LOG_DEBUG( log,
            "msg_type=" << msg_type << ", " <<
            "txn_id=" << txn_id << ", " <<
            "qty=" << qty << ", " <<
            "px=" << px << ", " <<
            "side=" << ( side == BUY ? "B" : "S" ) << ", " <<
            "symbol=" << symbol );

        if( msg_type == "D" )
        {
            LOG_INFO( log, "new order " << txn_id );

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
            LOG_INFO( log, "cxl order " << txn_id );

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
            LOG_INFO( log, "ignored msg_type " << msg_type );
        }

        LOG_INFO( log, "book " << symbol << " has "
            << book.get_buy_orders().size() << " buys and "
            << book.get_sell_orders().size() << " sells" );

        //std::string s;
        //LOG_DEBUG( log, "book " << symbol << ":\n" << orderbook_callbacks[symbol].to_string( s ) );
    }

    int num_recvd;
    my_order_callback cb;
};

// -----------------------------------------------------------------------------
int main( int argc, char** argv )
{
    int port = 8001;
    LOG_INFO( log, "hulk exchange starting" );

    core::tcp::event_loop dc_eloop;
    dc_eloop.watch( core::tcp::bind( port+1 ), true, *dc_callback );

    fix::tcp_event_loop oe_eloop;
    oe_eloop.new_acceptor< order_entry_session >( port );

    LOG_INFO( log, "starting main loop" );
    while( 1 )
    {
        dc_eloop.loop();
        oe_eloop.loop();
    }
}