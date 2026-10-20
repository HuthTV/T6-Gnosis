// T6 Gnosis GSC helper, solo LAN only. Every Gnosis GSC helper lives in this
// one script: despawn eligibility, trigger metadata and the Origins and Mob of the
// Dead aids. It never replaces cleanup functions or deletes
// actors, and it changes nothing in the game: points, weapons and puzzle setup belong
// to the practice menu (O-Prac), which Gnosis must stay fully compatible with (2026-10-20).
// Private level/actor telemetry flags prevent duplicate helper/listener threads.
#define GNOSIS_MAX_ACTORS 32
// Origins wind puzzle aid, from in-game measurements of the wind staff:
// projectile range and speed, box margin for splash/graze, vanilla's ignore window.
#define GNOSIS_WIND_RANGE 1200
#define GNOSIS_WIND_SPEED 1500
#define GNOSIS_WIND_EDGE 2
#define GNOSIS_WIND_COOLDOWN 1050
// Origins fire puzzle aid: trace length, vanilla torch radius (dist sq 4096),
// flame lifetime and the re-arm wait in fire_puzzle_torch_run.
#define GNOSIS_FIRE_RANGE 4000
#define GNOSIS_FIRE_RADIUS 64
#define GNOSIS_FIRE_LIT_MS 15000
#define GNOSIS_FIRE_REARM_MS 2000
// Mob of the Dead blue-skull aid: vanilla catch radius and reveal time
// (zm_prison_sq_bg), the tomahawk check time (_zm_weap_tomahawk 0.5 s wait, seen
// at 0.55 s), and the throw fitted from the tomahawk probe.
#define GNOSIS_MOB_CATCH_RADIUS 100
#define GNOSIS_MOB_SKULL_SHOWN_MS 1600
#define GNOSIS_MOB_CHECK_TIME 0.55
#define GNOSIS_MOB_LAUNCH_SPEED 930
#define GNOSIS_MOB_LAUNCH_PITCH 7.1
#define GNOSIS_MOB_LAUNCH_FORWARD 7
#define GNOSIS_MOB_GRAVITY 800
#define GNOSIS_MOB_MAX_SKULLS 8
#define GNOSIS_MOB_MAX_BOXES 32

main() { gnosis_start(); }
init() { gnosis_start(); }
gnosis_start()
{
    // The plugin writes this script and leaves it installed after the game closes.
    // Without the plugin it does nothing at all (user decision 2026-10-14). Plugin
    // builtins are resolved when a script loads, so a direct call would stop the map
    // from loading without the DLL; every plugin call below therefore goes through
    // invokebuiltin, which passes listed arguments exactly like a direct call
    // (measured in game, 2026-10-14).
    if ( !builtinfunctionexists( "GnosisPluginLoaded" ) )
        return;
    // Formerly t6gnosis_despawn.gsc, which checks and sets only the old flag.
    // Sharing it keeps a stale installed copy from starting duplicate threads.
    if ( isdefined( level.gnosis_helper_started ) || isdefined( level.gnosis_despawn_observer_started ) )
        return;
    level.gnosis_helper_started = true;
    level.gnosis_despawn_observer_started = true;
    level thread gnosis_loop();
    level thread gnosis_trigger_metadata();
    level thread gnosis_mob_aid();
    level thread gnosis_wind_aid();
    level thread gnosis_fire_aid();
}

gnosis_loop()
{
    seq = 0;


    for ( ;; )
    {
        // Publish a loaded function identity and live interval, independent of actors.
        if ( builtinfunctionexists( "GnosisTrackerBinding" ) && builtinfunctionexists( "getfunction" )
            && isdefined( level.script ) && isdefined( level.zombie_tracking_wait ) )
        {
            path = undefined;
            if ( level.script == "zm_transit" ) path = "maps/mp/zm_transit_distance_tracking";
            if ( level.script == "zm_highrise" ) path = "maps/mp/zm_highrise_distance_tracking";
            if ( level.script == "zm_prison" ) path = "maps/mp/zm_alcatraz_distance_tracking";
            if ( level.script == "zm_buried" ) path = "maps/mp/zm_buried_distance_tracking";
            if ( level.script == "zm_tomb" ) path = "maps/mp/zm_tomb_distance_tracking";
            if ( isdefined( path ) )
            {
                tracker = getfunction( path, "zombie_tracking_init" );
                if ( isdefined( tracker ) )
                {
                    // A replacefunc (e.g. an O-Prac segment start) runs another loop in
                    // its place; follow the replacement. getdetour runs in its own
                    // thread so an error there cannot stop this loop.
                    if ( builtinfunctionexists( "getdetour" ) && !( isdefined( level.gnosis_detour_query ) && level.gnosis_detour_query ) )
                        level thread gnosis_query_detour( tracker );
                    if ( isdefined( level.gnosis_tracker_detour ) )
                        invokebuiltin( "GnosisTrackerBinding", tracker, level.zombie_tracking_wait, level.gnosis_tracker_detour );
                    else
                        invokebuiltin( "GnosisTrackerBinding", tracker, level.zombie_tracking_wait );
                }
            }
        }
        if ( builtinfunctionexists( "GnosisDespawnBegin" ) && builtinfunctionexists( "GnosisDespawnZombie" )
            && builtinfunctionexists( "GnosisDespawnCommit" ) && builtinfunctionexists( "getfunction" ) )
        {
            players = getplayers();
            if ( players.size == 1 && isdefined( level.script ) && isdefined( level.zombie_team ) )
            {
                enemies = getaispeciesarray( level.zombie_team, "all" );
                living = [];
                if ( isdefined( enemies ) && enemies.size <= GNOSIS_MAX_ACTORS )
                {
                    for ( i = 0; i < enemies.size; i++ )
                    {
                        if ( isdefined( enemies[i] ) && enemies[i].health > 0 )
                            living[living.size] = enemies[i];
                    }
                    interval = -1;
                    if ( isdefined( level.zombie_tracking_wait ) )
                        interval = level.zombie_tracking_wait;
                    seq++;
                    if ( seq > 1000000000 )
                        seq = 1;
                    // No wait/yield/thread inside a publication transaction.
                    invokebuiltin( "GnosisDespawnBegin", level.script, seq, living.size, interval );
                    // Maps without distance tracking (survival, Nuketown) never set
                    // zombie_tracking_wait: no cleanup classification or delete
                    // listeners there, only the identities for special-enemy markers.
                    for ( i = 0; i < living.size; i++ )
                    {
                        state = 0;
                        if ( interval > 0 )
                            state = living[i] gnosis_classify( players[0] );
                        invokebuiltin( "GnosisDespawnZombie", seq, living[i] getentitynumber(), state, living[i] gnosis_special_kind() );
                    }
                    invokebuiltin( "GnosisDespawnCommit", seq, living.size );
                }
            }
        }
        // Observer refresh cadence, NOT the game's cleanup interval/phase.
        wait 0.1;
    }
}

// Asks Plutonium once a second which function replaces the tracker loop.
// getdetour's contract is undocumented, so the plugin validates the value (F8
// shows a replaced loop by name; a value equal to the original is ignored). If the call
// errors, only this thread dies and the flag stays set, so no further queries are
// made and the vanilla binding remains.
gnosis_query_detour( tracker )
{
    level.gnosis_detour_query = 1;
    detour = getdetour( tracker );
    level.gnosis_tracker_detour = detour;
    wait 1;
    level.gnosis_detour_query = 0;
}

// 0 unknown, 1 rule candidate, 2 nearby, 3 view cone, 4 exemption/disabled,
// 5 nearby + view. Native HUD reconciles these IDs with verified living actors.
gnosis_special_kind()
{
    if ( level.script == "zm_transit" && isdefined( self.is_avogadro ) && self.is_avogadro ) return 5;
    if ( !isdefined( self.animname ) ) return 0;
    if ( level.script == "zm_transit" && self.animname == "screecher_zombie" ) return 1;
    if ( level.script == "zm_buried" && self.animname == "ghost_zombie" ) return 2;
    if ( level.script == "zm_tomb" && self.animname == "mechz_zombie" ) return 3;
    if ( level.script == "zm_highrise" && self.animname == "leaper_zombie" ) return 4;
    return 0;
}

gnosis_classify( player )
{
    map = level.script;
    if ( map != "zm_transit" && map != "zm_highrise" && map != "zm_prison" && map != "zm_buried" && map != "zm_tomb" )
        return 0;
    // Avogadro has its own cleanup; never classify as an ordinary candidate.
    if ( map == "zm_transit" && isdefined( self.is_avogadro ) && self.is_avogadro )
        return 4;
    if ( !isdefined( level.zombie_tracking_dist ) || level.zombie_tracking_dist <= 0 )
        return 0;
    if ( map != "zm_transit" && ( !isdefined( level.zombie_tracking_high ) || level.zombie_tracking_high <= 0 ) )
        return 0;
    if ( isdefined( level.ignore_distance_tracking ) && level.ignore_distance_tracking )
        return 4;
    if ( isdefined( self.ignore_enemy_count ) && self.ignore_enemy_count )
        return 4;
    if ( isdefined( self.ignore_distance_tracking ) && self.ignore_distance_tracking )
        return 4;
    if ( map == "zm_buried" && ( !isdefined( self.ignoreall ) || self.ignoreall ) )
        return 4;
    if ( !isdefined( self.animname ) )
        return 4;
    if ( self.animname != "zombie" && ( map != "zm_tomb" || self.animname != "mechz_zombie" ) )
        return 4;
    if ( isdefined( self.electrified ) && self.electrified == 1 )
        return 4;
    if ( ( map == "zm_transit" || map == "zm_prison" || map == "zm_tomb" ) && isdefined( self.in_the_ground ) && self.in_the_ground == 1 )
        return 4;

    how_close = level.zombie_tracking_dist;
    if ( map == "zm_buried" && !( isdefined( self.has_legs ) && self.has_legs ) )
        how_close = how_close * 1.5;
    too_far_squared = how_close * how_close * 3;
    if ( isdefined( level.zombie_tracking_too_far_dist ) )
        too_far_squared = level.zombie_tracking_too_far_dist * level.zombie_tracking_too_far_dist;
    // Buried's live cleanup may retarget a downed favorite enemy and invoke
    // path recovery. Those side effects are not simulated by this observer.
    if ( map == "zm_buried" && isdefined( player.laststand ) && player.laststand
        && isdefined( self.favoriteenemy ) && self.favoriteenemy == player )
        return 0;
    considered = player.sessionstate != "spectator";
    // Vanilla tests definition, not truthiness, of only_track_targeted_players.
    if ( isdefined( level.only_track_targeted_players ) )
        considered = considered && isdefined( self.favoriteenemy ) && self.favoriteenemy == player;
    nearby = false;
    inview = false;
    if ( considered )
    {
        dist_squared = distancesquared( self.origin, player.origin );
        modifier = 1;
        if ( map == "zm_tomb" )
        {
            // The main pass updates this from current zones before classification.
            zone_query = getfunction( "maps/mp/zombies/_zm_zonemgr", "player_in_zone" );
            if ( player [[zone_query]]( "zone_air_stairs" ) || player [[zone_query]]( "zone_bolt_stairs" )
                || player [[zone_query]]( "zone_fire_stairs" ) || player [[zone_query]]( "zone_ice_stairs" ) )
                modifier = 2.25;
        }
        nearby = dist_squared < how_close * how_close * modifier;
        if ( map != "zm_transit" )
            nearby = nearby && abs( self.origin[2] - player.origin[2] ) < level.zombie_tracking_high;
        path = "maps/mp/zm_transit_distance_tracking";
        if ( map == "zm_highrise" ) path = "maps/mp/zm_highrise_distance_tracking";
        if ( map == "zm_prison" ) path = "maps/mp/zm_alcatraz_distance_tracking";
        if ( map == "zm_buried" ) path = "maps/mp/zm_buried_distance_tracking";
        if ( map == "zm_tomb" ) path = "maps/mp/zm_tomb_distance_tracking";
        view_query = getfunction( path, "player_can_see_me" );
        inview = dist_squared < too_far_squared && self [[view_query]]( player );
    }
    if ( map == "zm_buried" )
    {
        // Pure zone-membership/occupancy query; it does not delete or retarget.
        zone_query = getfunction( "maps/mp/zm_buried_distance_tracking", "can_be_deleted_from_buried_special_zones" );
        if ( self [[zone_query]]() )
        {
            nearby = false;
            inview = false;
        }
    }
    if ( nearby && inview ) return 5;
    if ( nearby ) return 2;
    if ( inview ) return 3;
    return 1;
}

// Copies descriptive fields only; never fires triggers or changes gameplay.
gnosis_trigger_metadata()
{
    if ( !builtinfunctionexists( "GnosisTriggerField" ) )
        return;
    classes = array( "trigger_box", "trigger_radius", "trigger_radius_use", "trigger_use", "info_volume" );
    while ( true )
    {
        players = getplayers();
        if ( players.size == 1 )
        {
            foreach ( classname in classes )
            {
                ents = getentarray( classname, "classname" );
                if ( isdefined( ents ) && ents.size <= 1024 )
                {
                    foreach ( ent in ents )
                    {
                        if ( !isdefined( ent ) ) continue;
                        number = ent getentitynumber();
                        invokebuiltin( "GnosisTriggerField", number, classname, "", "" );
                        if ( isdefined( ent.targetname ) ) gnosis_trigger_field( number, classname, "targetname", ent.targetname );
                        if ( isdefined( ent.script_noteworthy ) ) gnosis_trigger_field( number, classname, "script_noteworthy", ent.script_noteworthy );
                        if ( isdefined( ent.target ) ) gnosis_trigger_field( number, classname, "target", ent.target );
                        if ( isdefined( ent.script_string ) ) gnosis_trigger_field( number, classname, "script_string", ent.script_string );
                        if ( isdefined( ent.script_noteworthy ) && ent.script_noteworthy == "player_volume" )
                        {
                            enabled = "0";
                            if ( isdefined( ent.targetname ) && isdefined( level.zones ) && isdefined( level.zones[ent.targetname] ) && level.zones[ent.targetname].is_enabled )
                                enabled = "1";
                            invokebuiltin( "GnosisTriggerField", number, classname, "zone_enabled", enabled );
                        }
                        override = "0";
                        if ( isdefined( level.player_out_of_playable_area_monitor_callback ) )
                            override = "1";
                        invokebuiltin( "GnosisTriggerField", number, classname, "monitor_override", override );
                        if ( isdefined( ent.stub ) )
                        {
                            stub = ent.stub;
                            if ( isdefined( stub.targetname ) ) gnosis_trigger_field( number, classname, "stub.targetname", stub.targetname );
                            if ( isdefined( stub.script_noteworthy ) ) gnosis_trigger_field( number, classname, "stub.script_noteworthy", stub.script_noteworthy );
                            if ( isdefined( stub.target ) ) gnosis_trigger_field( number, classname, "stub.target", stub.target );
                            if ( isdefined( stub.script_string ) ) gnosis_trigger_field( number, classname, "stub.script_string", stub.script_string );
                            if ( isdefined( stub.unitrigger_system ) ) gnosis_trigger_field( number, classname, "stub.unitrigger_system", stub.unitrigger_system );
                        }
                    }
                }
            }
        }
        wait 1;
    }
}

gnosis_trigger_field( number, classname, key, value )
{
    if ( isstring( value ) )
        invokebuiltin( "GnosisTriggerField", number, classname, key, value );
}

// Same lookup as common_scripts\utility getstructarray, without the include.
gnosis_structs( key, name )
{
    if ( !isdefined( level.struct_class_names[key] ) || !isdefined( level.struct_class_names[key][name] ) )
        return [];
    return level.struct_class_names[key][name];
}

// Origins wind puzzle 2 lineup aid. Read-only: traces and field reads only.
// Model measured by the staff probe: the projectile flies straight for about
// 1200 units. physicstrace sees the ghosted detector brushes, bullettrace only the
// world. Each hit deals one damage: direct (direction_vec = velocity, about 1500
// long) when the brush is reached first, otherwise splash (direction_vec = brush
// origin + 24 up - hit point). Vanilla tests dot( to_dest, direction_vec ) >
// cos( script_int ) on those unnormalized vectors.
gnosis_wind_aid()
{
    if ( !builtinfunctionexists( "GnosisWindSmoke" ) || !builtinfunctionexists( "GnosisWindAim" ) )
        return;
    while ( !isdefined( level.script ) )
        wait 0.05;
    if ( level.script != "zm_tomb" )
        return;
    while ( !( isdefined( level.flag ) && isdefined( level.flag["air_puzzle_1_complete"] ) && level.flag["air_puzzle_1_complete"] ) )
        wait 0.5;
    if ( isdefined( level.flag["air_puzzle_2_complete"] ) && level.flag["air_puzzle_2_complete"] )
        return;
    level endon( "air_puzzle_2_complete" );
    smokes = gnosis_structs( "targetname", "puzzle_smoke_origin" );
    dests = gnosis_structs( "targetname", "puzzle_smoke_dest" );
    if ( smokes.size == 0 || smokes.size > 3 || dests.size == 0 )
        return;
    dest = dests[0].origin;
    level.gnosis_wind_hit = [];
    for ( i = 0; i < smokes.size; i++ )
        level thread gnosis_wind_hits( i, smokes[i] );
    while ( true )
    {
        players = getplayers();
        if ( players.size == 1 )
        {
            for ( i = 0; i < smokes.size; i++ )
                gnosis_wind_publish_smoke( i, smokes[i], dest );
            if ( players[0] getcurrentweapon() == "staff_air_zm" )
                players[0] gnosis_wind_aim( smokes, dest );
        }
        wait 0.05;
    }
}

// Vanilla judges a hit, then waits for its 1 s FX rotation and misses any hit in
// between. Record only the hits it judges, to show when a smoke is ignoring hits.
gnosis_wind_hits( index, smoke )
{
    level endon( "air_puzzle_2_complete" );
    while ( !isdefined( smoke.detector_brush ) )
        wait 0.5;
    brush = smoke.detector_brush;
    while ( true )
    {
        brush waittill( "damage", damage, attacker, direction, point, mod, tagname, modelname, partname, weapon );
        if ( isdefined( weapon ) && weapon == "staff_air_zm" && !gnosis_wind_cooling( index ) )
            level.gnosis_wind_hit[index] = gettime();
    }
}

gnosis_wind_cooling( index )
{
    if ( isdefined( level.gnosis_wind_hit[index] ) && gettime() - level.gnosis_wind_hit[index] < GNOSIS_WIND_COOLDOWN )
        return 1;
    return 0;
}

gnosis_wind_publish_smoke( index, smoke, dest )
{
    brush = smoke.detector_brush;
    // smoke.solved exists once vanilla's puzzle-2 handler is running.
    if ( !isdefined( brush ) || !isdefined( smoke.solved ) )
        return;
    solved = 0;
    if ( smoke.solved )
        solved = 1;
    invokebuiltin( "GnosisWindSmoke", index, brush getabsmins(), brush getabsmaxs(), vectornormalize( dest - smoke.origin ), solved, gnosis_wind_cooling( index ) );
}

// Publishes one sample: verdict 0 miss, 1 fails, 2 solves, 3 grazes (uncertain);
// hit 0 none, 1 direct, 2 splash; the smoke index; the predicted landing point;
// then the inputs for the live readout: aim forward, the direction_vec vanilla
// would judge, cos( script_int ), ms that smoke still ignores hits, and the
// bullettrace (world) and physicstrace (brush) distances.
gnosis_wind_aim( smokes, dest )
{
    muzzle = self getweaponmuzzlepoint();
    fwd = self getweaponforwarddir();
    end = muzzle + fwd * GNOSIS_WIND_RANGE;
    world = bullettrace( muzzle, end, 1, self );
    solid = physicstrace( muzzle, end, ( 0, 0, 0 ), ( 0, 0, 0 ), self );
    world_dist = world["fraction"] * GNOSIS_WIND_RANGE;
    solid_dist = solid["fraction"] * GNOSIS_WIND_RANGE;
    margin = ( GNOSIS_WIND_EDGE, GNOSIS_WIND_EDGE, GNOSIS_WIND_EDGE );
    verdict = 0;
    hit = 0;
    index = -1;
    point = world["position"];
    direction = ( 0, 0, 0 );
    for ( i = 0; i < smokes.size; i++ )
    {
        if ( isdefined( smokes[i].detector_brush ) && isdefined( solid["entity"] ) && solid["entity"] == smokes[i].detector_brush && solid_dist < world_dist )
            index = i;
    }
    if ( index >= 0 )
    {
        hit = 1;
        point = solid["position"];
        direction = fwd * GNOSIS_WIND_SPEED;
    }
    for ( i = 0; i < smokes.size && index < 0 && world["fraction"] < 1; i++ )
    {
        brush = smokes[i].detector_brush;
        if ( !isdefined( brush ) || !gnosis_wind_in_box( point, brush getabsmins() - margin, brush getabsmaxs() + margin ) )
            continue;
        index = i;
        hit = 2;
        direction = brush.origin + ( 0, 0, 24 ) - point;
    }
    // Vanilla: solved = dot( normalize( dest - smoke ), direction_vec ) > cos( script_int ).
    threshold = 0;
    if ( index >= 0 && isdefined( smokes[index].script_int ) )
        threshold = cos( smokes[index].script_int );
    if ( hit > 0 )
    {
        verdict = 1;
        if ( isdefined( smokes[index].script_int ) && vectordot( vectornormalize( dest - smokes[index].origin ), direction ) > threshold )
            verdict = 2;
        // Within a unit of the world surface a direct hit may splash instead.
        if ( hit == 1 && world_dist - solid_dist < 1 )
            verdict = 3;
    }
    // The projectile has some size: a line just outside a box can still clip it.
    for ( i = 0; i < smokes.size && index < 0; i++ )
    {
        brush = smokes[i].detector_brush;
        if ( !isdefined( brush ) )
            continue;
        entry = gnosis_wind_ray_box( muzzle, fwd, world_dist, brush getabsmins() - margin, brush getabsmaxs() + margin );
        if ( entry < 0 )
            continue;
        index = i;
        verdict = 3;
        point = muzzle + fwd * entry;
        if ( isdefined( smokes[i].script_int ) )
            threshold = cos( smokes[i].script_int );
    }
    cooldown = 0;
    if ( index >= 0 && isdefined( level.gnosis_wind_hit[index] ) )
    {
        cooldown = GNOSIS_WIND_COOLDOWN - ( gettime() - level.gnosis_wind_hit[index] );
        if ( cooldown < 0 )
            cooldown = 0;
    }
    invokebuiltin( "GnosisWindAim", verdict, hit, index, point, fwd, direction, threshold, cooldown, world_dist, solid_dist );
}

gnosis_wind_in_box( p, lo, hi )
{
    for ( axis = 0; axis < 3; axis++ )
    {
        if ( p[axis] < lo[axis] || p[axis] > hi[axis] )
            return false;
    }
    return true;
}

// Slab test: distance along the unit direction where the segment enters the box, or -1.
gnosis_wind_ray_box( start, direction, length, lo, hi )
{
    near = 0;
    far = length;
    for ( axis = 0; axis < 3; axis++ )
    {
        if ( abs( direction[axis] ) < 0.000001 )
        {
            if ( start[axis] < lo[axis] || start[axis] > hi[axis] )
                return -1;
            continue;
        }
        t1 = ( lo[axis] - start[axis] ) / direction[axis];
        t2 = ( hi[axis] - start[axis] ) / direction[axis];
        if ( t1 > t2 )
        {
            swap = t1;
            t1 = t2;
            t2 = swap;
        }
        if ( t1 > near )
            near = t1;
        if ( t2 < far )
            far = t2;
        if ( near > far )
            return -1;
    }
    return near;
}

// Origins fire puzzle 2 aid. Read-only: traces and field reads only.
// Vanilla (zm_tomb_quest_fire.gsc): a torch lights when a staff_fire_zm explosion
// lands within 64 units of its church_torch_target struct, then ignores hits
// for 2 s and burns 15 s. The puzzle completes when a torch lights while every
// correct torch is lit and no wrong one is. The staff fires its main projectile
// along the aim, then (only if a 100-unit bullettrace ahead is clear) two
// magicbullets 0.2 s and 0.4 s later from muzzle + fwd * 50 toward muzzle +
// dir * 100, with dir the aim turned -15 then +15 degrees: about -/+29 degrees.
gnosis_fire_aid()
{
    if ( !builtinfunctionexists( "GnosisFireTorch" ) || !builtinfunctionexists( "GnosisFireAim" ) )
        return;
    while ( !isdefined( level.script ) )
        wait 0.05;
    if ( level.script != "zm_tomb" )
        return;
    // b_correct_torch is assigned when puzzle 2 starts, after puzzle 1.
    while ( !( isdefined( level.flag ) && isdefined( level.flag["fire_puzzle_1_complete"] ) && level.flag["fire_puzzle_1_complete"] ) )
        wait 0.5;
    if ( isdefined( level.flag["fire_puzzle_2_complete"] ) && level.flag["fire_puzzle_2_complete"] )
        return;
    level endon( "fire_puzzle_2_complete" );
    torches = gnosis_structs( "script_noteworthy", "church_torch_target" );
    if ( torches.size == 0 || torches.size > 9 )
        return;
    flames = [];
    lit_at = [];
    while ( true )
    {
        players = getplayers();
        if ( players.size == 1 )
        {
            now = gettime();
            for ( i = 0; i < torches.size; i++ )
            {
                torch = torches[i];
                lit = 0;
                left = 0;
                if ( isdefined( torch.e_fx ) )
                {
                    // A new flame model means vanilla (re)lit it.
                    if ( !isdefined( flames[i] ) || flames[i] != torch.e_fx )
                    {
                        flames[i] = torch.e_fx;
                        lit_at[i] = now;
                    }
                    lit = 1;
                    left = GNOSIS_FIRE_LIT_MS - ( now - lit_at[i] );
                    if ( left < 0 )
                        left = 0;
                }
                else
                    flames[i] = undefined;
                rearm = 0;
                if ( isdefined( lit_at[i] ) && now - lit_at[i] < GNOSIS_FIRE_REARM_MS )
                    rearm = GNOSIS_FIRE_REARM_MS - ( now - lit_at[i] );
                correct = 0;
                if ( isdefined( torch.b_correct_torch ) && torch.b_correct_torch )
                    correct = 1;
                invokebuiltin( "GnosisFireTorch", i, torch.origin, correct, lit, left, rearm );
            }
            if ( players[0] getcurrentweapon() == "staff_fire_zm" )
                players[0] gnosis_fire_aim( torches );
        }
        wait 0.05;
    }
}

// Publishes the predicted main, left and right explosion points, the torch each
// would light (-1 none), whether the side shots fire, and the muzzle and aim.
gnosis_fire_aim( torches )
{
    muzzle = self getweaponmuzzlepoint();
    fwd = self getweaponforwarddir();
    main_point = bullettrace( muzzle, muzzle + fwd * GNOSIS_FIRE_RANGE, 1, self )["position"];
    // Exactly vanilla fire_spread_shots' gate, including its undefined ignore entity.
    sides = 0;
    if ( bullettrace( muzzle, muzzle + fwd * 100, 0, undefined )["fraction"] == 1 )
        sides = 1;
    aim = vectortoangles( fwd );
    start = muzzle + fwd * 50;
    left_dir = vectornormalize( muzzle + anglestoforward( ( aim[0], aim[1] - 15, aim[2] ) ) * 100 - start );
    right_dir = vectornormalize( muzzle + anglestoforward( ( aim[0], aim[1] + 15, aim[2] ) ) * 100 - start );
    left_point = bullettrace( start, start + left_dir * GNOSIS_FIRE_RANGE, 1, self )["position"];
    right_point = bullettrace( start, start + right_dir * GNOSIS_FIRE_RANGE, 1, self )["position"];
    invokebuiltin( "GnosisFireAim", main_point, gnosis_fire_torch_at( torches, main_point ), left_point, gnosis_fire_torch_at( torches, left_point ),
        right_point, gnosis_fire_torch_at( torches, right_point ), sides, muzzle, fwd );
}

// Nearest torch whose 64-unit sphere contains the point, or -1.
gnosis_fire_torch_at( torches, point )
{
    best = -1;
    best_sq = GNOSIS_FIRE_RADIUS * GNOSIS_FIRE_RADIUS;
    for ( i = 0; i < torches.size; i++ )
    {
        dist_sq = distancesquared( point, torches[i].origin );
        if ( dist_sq <= best_sq )
        {
            best = i;
            best_sq = dist_sq;
        }
    }
    return best;
}

// Mob of the Dead blue-skull aid (2026-10-14). Publishes the Blundergat quest skulls,
// a predicted tomahawk throw, the real flight and the Afterlife shock boxes; the
// plugin draws them. Vanilla (zm_prison_sq_bg, _zm_weap_tomahawk): a skull is caught
// when the tomahawk's position at its single check is within 100 units of the skull
// origin, whether or not the skull is shown. The check happens at the first impact or
// 0.55 s after the throw. Throw model fitted from measured throws (2026-10-14):
// launch at the eye plus 7 along the view, 930 units/s along the view pitched 7.1
// degrees up, plus the player's horizontal velocity, gravity 800.
gnosis_mob_aid()
{
    while ( !isdefined( level.script ) )
        wait 0.05;
    if ( level.script != "zm_prison" )
        return;
    players = getplayers();
    while ( players.size != 1 )
    {
        wait 0.5;
        players = getplayers();
    }
    player = players[0];
    level.gnosis_mob_skull_next = 0;
    level thread gnosis_mob_boxes();
    player thread gnosis_mob_flights();
    while ( isdefined( player ) )
    {
        gnosis_mob_skulls();
        afterlife = 0;
        if ( isdefined( player.afterlife ) && player.afterlife )
            afterlife = 1;
        tomahawk = 0;
        if ( player hasweapon( "bouncing_tomahawk_zm" ) || player hasweapon( "upgraded_tomahawk_zm" ) )
            tomahawk = 1;
        invokebuiltin( "GnosisMobState", afterlife, tomahawk, player.origin );
        if ( tomahawk && !afterlife )
            player gnosis_mob_predict();
        wait 0.05;
    }
}

// Stable numbers: macguffins leave level.sq_bg_macguffins when collected.
gnosis_mob_skulls()
{
    if ( !isdefined( level.sq_bg_macguffins ) )
        return;
    now = gettime();
    foreach ( skull in level.sq_bg_macguffins )
    {
        if ( !isdefined( skull ) )
            continue;
        if ( !isdefined( skull.gnosis_index ) )
        {
            if ( level.gnosis_mob_skull_next >= GNOSIS_MOB_MAX_SKULLS )
                continue;
            skull.gnosis_index = level.gnosis_mob_skull_next;
            level.gnosis_mob_skull_next++;
            skull thread gnosis_mob_skull_shots();
        }
        // Time left in vanilla's 1.6 s reveal after a lightning-hands hit, or -1.
        left = -1;
        if ( isdefined( skull.gnosis_shown_at ) && now - skull.gnosis_shown_at < GNOSIS_MOB_SKULL_SHOWN_MS )
            left = GNOSIS_MOB_SKULL_SHOWN_MS - ( now - skull.gnosis_shown_at );
        invokebuiltin( "GnosisMobSkull", skull.gnosis_index, skull.origin, skull getabsmins(), skull getabsmaxs(), left );
    }
}

// Mirrors sq_bg_macguffin_think's condition; only records the time.
gnosis_mob_skull_shots()
{
    self endon( "death" );
    while ( true )
    {
        self waittill( "damage", amount, attacker );
        if ( isdefined( attacker ) && ( attacker == level || isplayer( attacker ) && attacker getcurrentweapon() == "lightning_hands_zm" ) )
            self.gnosis_shown_at = gettime();
    }
}

gnosis_mob_skull_at( point )
{
    if ( !isdefined( level.sq_bg_macguffins ) || !isdefined( point ) )
        return -1;
    foreach ( skull in level.sq_bg_macguffins )
    {
        if ( isdefined( skull ) && isdefined( skull.gnosis_index ) && distancesquared( skull.origin, point ) < GNOSIS_MOB_CATCH_RADIUS * GNOSIS_MOB_CATCH_RADIUS )
            return skull.gnosis_index;
    }
    return -1;
}

// The predicted check point: the arc in 0.05 s steps, stopped at the first world hit.
gnosis_mob_predict()
{
    angles = self getplayerangles();
    eye = self geteye();
    start = eye + anglestoforward( angles ) * GNOSIS_MOB_LAUNCH_FORWARD;
    velocity = self getvelocity();
    launch = anglestoforward( ( angles[0] - GNOSIS_MOB_LAUNCH_PITCH, angles[1], 0 ) ) * GNOSIS_MOB_LAUNCH_SPEED + ( velocity[0], velocity[1], 0 );
    previous = start;
    check = undefined;
    stop = GNOSIS_MOB_CHECK_TIME;
    impact = 0;
    for ( i = 1; i <= 11; i++ )
    {
        t = i * 0.05;
        point = start + launch * t + ( 0, 0, -0.5 * GNOSIS_MOB_GRAVITY * t * t );
        trace = bullettrace( previous, point, 0, self );
        if ( isdefined( trace ) && trace["fraction"] < 1 )
        {
            check = trace["position"];
            stop = ( i - 1 + trace["fraction"] ) * 0.05;
            impact = 1;
            break;
        }
        previous = point;
    }
    if ( !isdefined( check ) )
        check = previous;
    invokebuiltin( "GnosisTomahawkAim", start, launch, stop, check, gnosis_mob_skull_at( check ), impact );
}

// The real flight of every throw, then its check point (death or time_out first,
// as tomahawk_thrown waits). When vanilla deletes the grenade before this thread
// reads it, the point is extrapolated from the last two samples.
gnosis_mob_flights()
{
    self endon( "disconnect" );
    throws = 0;
    while ( true )
    {
        self waittill( "grenade_fire", grenade, weapon );
        if ( !isdefined( weapon ) || !isstring( weapon ) || !issubstr( weapon, "tomahawk_zm" ) || !isdefined( grenade ) )
            continue;
        throws++;
        state = spawnstruct();
        state.id = throws;
        state.points = [];
        state.times = [];
        level thread gnosis_mob_flight_event( grenade, "death", state );
        level thread gnosis_mob_flight_event( grenade, "time_out", state );
        level thread gnosis_mob_flight_track( grenade, state );
    }
}

gnosis_mob_flight_track( grenade, state )
{
    for ( frame = 0; frame < 30 && isdefined( grenade ) && !isdefined( state.done ); frame++ )
    {
        state.points[state.points.size] = grenade.origin;
        state.times[state.times.size] = gettime();
        invokebuiltin( "GnosisTomahawkFlight", state.id, grenade.origin, 0, -1 );
        wait 0.05;
    }
}

gnosis_mob_flight_event( grenade, event, state )
{
    grenade waittill( event );
    if ( isdefined( state.done ) )
        return;
    state.done = 1;
    point = undefined;
    if ( isdefined( grenade ) )
        point = grenade.origin;
    else if ( state.points.size >= 2 )
    {
        last = state.points[state.points.size - 1];
        before = state.points[state.points.size - 2];
        step = state.times[state.times.size - 1] - state.times[state.times.size - 2];
        if ( step > 0 )
            point = last + ( last - before ) * ( ( gettime() - state.times[state.times.size - 1] ) / step );
        else
            point = last;
    }
    else if ( state.points.size == 1 )
        point = state.points[0];
    if ( isdefined( point ) )
        invokebuiltin( "GnosisTomahawkFlight", state.id, point, 1, gnosis_mob_skull_at( point ) );
}

// Shock boxes, once a second. Afterlife props (afterlife_interact) need the player
// within 256 units in 3D; the afterlife-door boxes (zm_prison alcatraz_afterlife_doors)
// use distance2d. Door boxes are found through door -> struct -> box once.
gnosis_mob_boxes()
{
    while ( !isdefined( level.struct_class_names ) )
        wait 0.5;
    wait 5;
    boxes = [];
    planar = [];
    foreach ( box in getentarray( "afterlife_interact", "targetname" ) )
    {
        if ( isdefined( box.model ) && ( issubstr( box.model, "shock_box" ) || issubstr( box.model, "power_station_panels" ) ) )
        {
            boxes[boxes.size] = box;
            planar[planar.size] = 0;
        }
    }
    foreach ( door in getentarray( "zombie_door", "targetname" ) )
    {
        if ( !isdefined( door.script_noteworthy ) || door.script_noteworthy != "afterlife_door" || !isdefined( door.target ) )
            continue;
        structs = gnosis_structs( "targetname", door.target );
        if ( structs.size == 0 || !isdefined( structs[0].target ) )
            continue;
        box = getent( structs[0].target, "targetname" );
        if ( !isdefined( box ) )
            continue;
        // A door box can also be an afterlife_interact prop; list it once, with the
        // door's 2D rule (user screenshot 2026-10-14: a cylinder and a sphere on one box).
        listed = 0;
        for ( i = 0; i < boxes.size; i++ )
        {
            if ( boxes[i] == box )
            {
                planar[i] = 1;
                listed = 1;
            }
        }
        if ( !listed )
        {
            boxes[boxes.size] = box;
            planar[planar.size] = 1;
        }
    }
    while ( true )
    {
        for ( i = 0; i < boxes.size && i < GNOSIS_MOB_MAX_BOXES; i++ )
        {
            if ( isdefined( boxes[i] ) )
                invokebuiltin( "GnosisMobBox", i, boxes[i].origin, boxes[i] getabsmins(), boxes[i] getabsmaxs(), planar[i] );
        }
        wait 1;
    }
}
