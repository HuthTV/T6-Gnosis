#include maps\mp\gametypes_zm\_hud_util;

init()
{
    precacheshader( "progress_bar_fill" );
    precacheshader( "progress_bar_fg" );
    precacheshader( "progress_bar_bg" );
    precacheshader( "white" );
    thread watch_connections();
}

watch_connections()
{
    for ( ;; )
    {
        level waittill( "connected", player );
        player thread player_hud();
    }
}

player_hud()
{
    self endon( "disconnect" );
    self waittill( "spawned_player" );
    if ( self getentitynumber() != 0 )
        return;

    bar = self createbar( ( 0.25, 0.85, 0.45 ), 180, 8 );
    bar setpoint( "CENTER", "BOTTOM", 0, -65 );
    bar.sort = 10;
    bar.bar.sort = 11;
    bar.barframe.sort = 12;

    marker = self createicon( "white", 2, 14 );
    marker setpoint( "CENTER", "BOTTOM", -45, -65 );
    marker.sort = 13;
    marker.color = ( 1, 0.85, 0.25 );

    label = self createfontstring( "default", 1.2 );
    label setpoint( "CENTER", "BOTTOM", 0, -81 );
    label.sort = 13;
    self thread cleanup_hud( bar, marker, label );

    for ( ;; )
    {
        if ( !isalive( self ) || self.sessionstate != "playing" )
        {
            bar hideelem();
            marker.alpha = 0;
            label.alpha = 0;
            wait 0.1;
            continue;
        }

        sample = self stamina_sample();
        if ( sample[0] < 0 || sample[1] <= 0 )
        {
            bar hideelem();
            marker.alpha = 0;
            label.alpha = 1;
            label settextunlimited( "STAMINA unavailable" );
            wait 0.1;
            continue;
        }

        bar showelem();
        label.alpha = 1;
        fraction = sample[0] / sample[1];
        bar updatebar( fraction );
        // Vanilla updatebar keeps a one-pixel fill at zero; hide it when empty.
        bar.bar.alpha = fraction > 0;
        bar.bar.color = ( 0.25, 0.85, 0.45 );
        if ( sample[0] <= sample[2] )
            bar.bar.color = ( 1, 0.55, 0.2 );

        marker.alpha = sample[2] <= sample[1];
        marker.x = -90 + 180 * sample[2] / sample[1];
        label settextunlimited( "STAMINA " + int( sample[0] ) + " / " + int( sample[1] ) + " ms" );
        wait 0.05;
    }
}

cleanup_hud( bar, marker, label )
{
    self waittill( "disconnect" );
    if ( isdefined( bar ) )
        bar destroyelem();
    if ( isdefined( marker ) )
        marker destroyelem();
    if ( isdefined( label ) )
        label destroyelem();
}
