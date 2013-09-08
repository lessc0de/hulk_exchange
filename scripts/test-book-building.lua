
header = {
     [49]="HULK-CLIENT",
     [56]="HULK-EXCHANGE"
}

next_id = 0;
body = {};

function nos( session, symbol, side, qty, px )
    next_id = next_id + 1;
    body = {
        [11]=next_id,
        [38]=qty,
        [44]=px,
        [54]=side,
        [55]=symbol
    };
    return session:send( "D", body );
end

oe_session = fix.new_initiator( "tcp://localhost:8001", "FIX.4.4", header );

nos( oe_session, "AAA.L", 1, 100, 96 );
nos( oe_session, "AAA.L", 1, 100, 97 );
nos( oe_session, "AAA.L", 1, 100, 98 );
nos( oe_session, "AAA.L", 1, 100, 99 );
nos( oe_session, "AAA.L", 1, 100, 100 );

nos( oe_session, "AAA.L", 2, 100, 101 );
nos( oe_session, "AAA.L", 2, 100, 102 );
nos( oe_session, "AAA.L", 2, 100, 103 );
nos( oe_session, "AAA.L", 2, 100, 104 );
nos( oe_session, "AAA.L", 2, 100, 105 );

nos( oe_session, "AAA.L", 2, 50, 96 );
nos( oe_session, "AAA.L", 1, 50, 105 );

print( "\ndone!" );
