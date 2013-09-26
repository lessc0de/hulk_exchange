
#include "hulk/exchange/orderbook.h"

using namespace hulk::exchange;

int main( int argc, char** argv )
{
    orderbook book;
    order::callback cb;
    order_sptr o( new order( cb, BUY, 1, 1 ) );
    book.add( o );
}
