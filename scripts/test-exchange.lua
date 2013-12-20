
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

function cxl( session )
    body = {
        [11]=next_id
    };
    return session:send( "F", body );
end

oe_session = fix.new_initiator( "tcp://localhost:5554", "FIX.4.4", header );

-- send an order and cxl it
nos( oe_session, "AAA.L", 1, 100, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

cxl( oe_session )
oe_session:expect( { [11]=body[11], [35]="8", [150]="4" } );

-- two orders fully filled against each other
nos( oe_session, "BBB.L", 1, 100, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

nos( oe_session, "BBB.L", 2, 100, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );
oe_session:expect( { [11]=body[11], [35]="8", [38]="100", [150]="F" } );
oe_session:expect( { [11]=body[11]-1, [35]="8", [38]="100", [150]="F" } );

-- three orders fully filled against each order
nos( oe_session, "CCC.L", 1, 100, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

nos( oe_session, "CCC.L", 1, 100, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

nos( oe_session, "CCC.L", 2, 200, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );
oe_session:expect( { [11]=body[11], [35]="8", [32]="100", [150]="F" } );
oe_session:expect( { [11]=body[11]-2, [35]="8", [32]="100", [150]="F" } );
oe_session:expect( { [11]=body[11], [35]="8", [32]="100", [150]="F" } );
oe_session:expect( { [11]=body[11]-1, [35]="8", [32]="100", [150]="F" } );

-- sell only at the limit price or higher
nos( oe_session, "DDD.L", 2, 100, 101 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

nos( oe_session, "DDD.L", 1, 100, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

nos( oe_session, "DDD.L", 2, 100, 99 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );
oe_session:expect( { [11]=body[11], [35]="8", [31]="100", [32]="100", [150]="F" } );
oe_session:expect( { [11]=body[11]-1, [35]="8", [31]="100", [32]="100", [150]="F" } );

nos( oe_session, "DDD.L", 1, 100, 102 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );
oe_session:expect( { [11]=body[11], [35]="8", [31]="101", [32]="100", [150]="F" } );
oe_session:expect( { [11]=body[11]-3, [35]="8", [31]="101", [32]="100", [150]="F" } );

-- buy only at the limit price or lower
nos( oe_session, "EEE.L", 1, 100, 100 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

nos( oe_session, "EEE.L", 2, 100, 101 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );

nos( oe_session, "EEE.L", 1, 100, 102 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );
oe_session:expect( { [11]=body[11], [35]="8", [31]="101", [32]="100", [150]="F" } );
oe_session:expect( { [11]=body[11]-1, [35]="8", [31]="101", [32]="100", [150]="F" } );

nos( oe_session, "EEE.L", 2, 100, 99 );
oe_session:expect( { [11]=body[11], [35]="8", [150]="0" } );
oe_session:expect( { [11]=body[11], [35]="8", [31]="100", [32]="100", [150]="F" } );
oe_session:expect( { [11]=body[11]-3, [35]="8", [31]="100", [32]="100", [150]="F" } );

print( "\ndone!" );
