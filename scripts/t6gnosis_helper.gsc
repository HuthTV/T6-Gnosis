// T6 Gnosis GSC helper, solo LAN only. Every Gnosis GSC helper lives in this
// one script: despawn eligibility, trigger metadata and the Origins (staffs, robot
// footsteps), Mob of the Dead and TranZit Avogadro aids. It never replaces cleanup functions or deletes
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
// Origins giant robot footsteps: the three walkers and the 10 s wait of the wind staff
// clue thread (zm_tomb_vo watch_one_shot_samantha_clue). Route 3 is robot 1's intro walk
// (robot_walk_animation id 3, zm_robot_walk_intro), a different path from its usual walk.
#define GNOSIS_ROBOTS 3
#define GNOSIS_ROBOT_INTRO_ROUTE 3
#define GNOSIS_ROBOT_CLUE_WAIT_MS 10000
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
#define GNOSIS_MOB_MAX_SPAWNS 512
// Thrown-grenade aid (frag, EMP, G-strike). Kinds shared with src/grenade_aid.hpp
// (grenade::Kind), path flags with grenade::Flag, flight events with its event list.
// The G-strike strike pattern and 200-unit kill radius are vanilla (_zm_weap_beacon).
#define GNOSIS_THROW_FRAG 1
#define GNOSIS_THROW_EMP 2
#define GNOSIS_THROW_BEACON 3
#define GNOSIS_THROW_BOUNCE 1
#define GNOSIS_THROW_REST 2
#define GNOSIS_THROW_BURST 4
#define GNOSIS_THROW_STEP 0.05
#define GNOSIS_THROW_GRAVITY 800
#define GNOSIS_THROW_MAX_POINTS 96
#define GNOSIS_THROW_MAX_BOUNCES 8
#define GNOSIS_THROW_MAX_FRAMES 200
#define GNOSIS_THROW_STRIKE_RADIUS 200
// TranZit Avogadro aid: flag bits shared with src/avogadro_aid.hpp (avogadro::Flag).
#define GNOSIS_AVO_PAIN 1
#define GNOSIS_AVO_SHIELD 2
#define GNOSIS_AVO_TELEPORT 4
#define GNOSIS_AVO_TRAVERSE 8
#define GNOSIS_AVO_ENEMY_ON_BUS 16
#define GNOSIS_AVO_REGION_OCCUPIED 32
#define GNOSIS_AVO_PLAYER_NEAR 64
#define GNOSIS_AVO_POWER_ON 128
#define GNOSIS_AVO_ARRIVING 256
#define GNOSIS_AVO_BOLT_RANGE 512
#define GNOSIS_AVO_BOLT_FACING 1024
#define GNOSIS_AVO_BOLT_SIGHT 2048
#define GNOSIS_AVO_SWITCHES_ON 4096
#define GNOSIS_AVO_MAX_SPAWNS 96

main() { gnosis_start(); }
init() { gnosis_start(); }
gnosis_start()
{
    // The plugin writes this script and leaves it installed after the game closes.
    // Without the plugin it does nothing at all (user decision 2026-10-14). Plugin
    // builtins are resolved when a script loads, so a direct call would stop the map
    // from loading without the DLL. Every plugin call below therefore goes through
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
    level thread gnosis_mob_afterlife();
    level thread gnosis_wind_aid();
    level thread gnosis_fire_aid();
    level thread gnosis_robot_aid();
    level thread gnosis_avogadro_aid();
    level thread gnosis_throw_aid();
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
                    // its place. Follow the replacement. getdetour runs in its own
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
// shows a replaced loop by name. A value equal to the original is ignored). If the call
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
    // Avogadro has its own cleanup. Never classify as an ordinary candidate.
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
        // Pure zone-membership/occupancy query. It does not delete or retarget.
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

// Copies descriptive fields only. Never fires triggers or changes gameplay.
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

// Publishes one sample: the verdict (0 miss, 1 fails, 2 solves, 3 grazes, uncertain),
// the hit (0 none, 1 direct, 2 splash), the smoke index, the predicted landing point,
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
// _zm_weap_staff_fire fires those side shots for staff_fire_zm and staff_fire_upgraded_zm
// (an uncharged upgraded shot), so the aim is published whenever either is held, puzzle or
// not (user request 2026-11-20). The torches are published only while puzzle 2 runs: before
// it b_correct_torch is unset, after it a torch hit does nothing.
gnosis_fire_aid()
{
    if ( !builtinfunctionexists( "GnosisFireTorch" ) || !builtinfunctionexists( "GnosisFireAim" ) )
        return;
    while ( !isdefined( level.script ) )
        wait 0.05;
    if ( level.script != "zm_tomb" )
        return;
    torches = gnosis_structs( "script_noteworthy", "church_torch_target" );
    if ( torches.size > 9 )
        torches = [];
    flames = [];
    lit_at = [];
    while ( true )
    {
        players = getplayers();
        if ( players.size == 1 )
        {
            now = gettime();
            // b_correct_torch is assigned when puzzle 2 starts, after puzzle 1.
            puzzle = gnosis_flag( "fire_puzzle_1_complete" ) && !gnosis_flag( "fire_puzzle_2_complete" );
            active = [];
            if ( puzzle )
                active = torches;
            for ( i = 0; i < active.size; i++ )
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
            weapon = players[0] getcurrentweapon();
            if ( weapon == "staff_fire_zm" || weapon == "staff_fire_upgraded_zm" )
                players[0] gnosis_fire_aim( active );
        }
        wait 0.05;
    }
}

gnosis_flag( name )
{
    return isdefined( level.flag ) && isdefined( level.flag[name] ) && level.flag[name];
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

// Origins giant robot footsteps (zm_tomb_giant_robot). Read-only: field reads and listeners.
// Each robot's stomp kill triggers ride its feet (linked to TAG_ATTACH_HATCH_LE / _RI, 72 up).
// On each big footstep notetrack, monitor_footsteps_fx starts rumble_and_shake, which waits
// 0.2 s and sends sam_clue_giant for each valid player outside a robot head whose distance to
// that foot's trigger is under 1500. watch_one_shot_samantha_clue (zm_tomb_vo) takes it, waits
// 10 s and plays the wind staff clue if the player is in zombie blood. Publishes each robot's
// feet, the player, the helper's copy of the clue's 10 s wait, and every step at its check.
// Each walk is animscripted from a fixed align struct with fixed animations, so the n-th big
// step of a route lands on the same spot every walk: steps are numbered per walk and tagged
// with their route, and the plugin keeps each route's spots to show the steps still ahead.
gnosis_robot_aid()
{
    if ( !builtinfunctionexists( "GnosisRobot" ) || !builtinfunctionexists( "GnosisRobotPlayer" ) || !builtinfunctionexists( "GnosisRobotStep" ) )
        return;
    while ( !isdefined( level.script ) )
        wait 0.05;
    if ( level.script != "zm_tomb" )
        return;
    // giant_robot_initial_spawns adds each robot once its triggers are linked.
    while ( !isdefined( level.a_giant_robots ) || level.a_giant_robots.size < GNOSIS_ROBOTS )
        wait 0.5;
    level.gnosis_robot_clue_until = 0;
    level thread gnosis_robot_clues();
    level thread gnosis_robot_intro();
    for ( i = 0; i < GNOSIS_ROBOTS; i++ )
    {
        level.gnosis_robot_walking[i] = 0;
        level.gnosis_robot_step_next[i] = 0;
        level.gnosis_robot_step_checked[i] = 0;
        level thread gnosis_robot_steps( i, "left" );
        level thread gnosis_robot_steps( i, "right" );
    }
    while ( true )
    {
        players = getplayers();
        if ( players.size == 1 )
        {
            for ( i = 0; i < GNOSIS_ROBOTS; i++ )
                gnosis_robot_publish( i );
            players[0] gnosis_robot_player();
        }
        wait 0.05;
    }
}

// robot_cycling starts robot 1's intro walk first and waits for this notify before any other walk.
gnosis_robot_intro()
{
    level.gnosis_robot_intro_done = 0;
    level waittill( "giant_robot_intro_complete" );
    level.gnosis_robot_intro_done = 1;
}

gnosis_robot_route( id )
{
    if ( id == 1 && !level.gnosis_robot_intro_done )
        return GNOSIS_ROBOT_INTRO_ROUTE;
    return id;
}

// id, route, walking, the left and right trigger origins, the hatch foot (0 left, 1 right, -1
// none), whether its hatch is open, and how many big steps of this walk have had their check.
// hatch_foot is picked anew 10 s into each walk, before giant_robot_think sets is_walking, and
// only for a walk with a hatch. The step count restarts when a walk ends (the next walk starts
// at least 10 s later).
gnosis_robot_publish( id )
{
    ai = level.a_giant_robots[id];
    if ( !isdefined( ai ) || !isdefined( ai.trig_stomp_kill_left ) || !isdefined( ai.trig_stomp_kill_right ) )
        return;
    walking = 0;
    if ( isdefined( ai.is_walking ) && ai.is_walking )
        walking = 1;
    hatch = -1;
    if ( walking && isdefined( ai.b_has_hatch ) && ai.b_has_hatch && isdefined( ai.hatch_foot ) )
    {
        if ( ai.hatch_foot == "left" )
            hatch = 0;
        else if ( ai.hatch_foot == "right" )
            hatch = 1;
    }
    open = 0;
    if ( hatch >= 0 && isdefined( level.gr_foot_hatch_closed ) && isdefined( level.gr_foot_hatch_closed[id] ) && !level.gr_foot_hatch_closed[id] )
        open = 1;
    if ( level.gnosis_robot_walking[id] && !walking )
    {
        level.gnosis_robot_step_next[id] = 0;
        level.gnosis_robot_step_checked[id] = 0;
    }
    level.gnosis_robot_walking[id] = walking;
    invokebuiltin( "GnosisRobot", id, gnosis_robot_route( id ), walking, ai.trig_stomp_kill_left.origin, ai.trig_stomp_kill_right.origin, hatch, open,
        level.gnosis_robot_step_checked[id] );
}

// Origin, the robot head the player is in (-1 none), is_player_valid as rumble_and_shake
// calls it, zombie blood, and the clue state: 0 waiting for a step, 1 in its 10 s wait
// (ms left), 2 ended (the clue thread ends when every wind piece is found).
gnosis_robot_player()
{
    head = -1;
    if ( isdefined( self.in_giant_robot_head ) )
        head = self.in_giant_robot_head;
    valid = 0;
    if ( maps\mp\zombies\_zm_utility::is_player_valid( self ) )
        valid = 1;
    blood = 0;
    if ( isdefined( self.zombie_vars ) && isdefined( self.zombie_vars["zombie_powerup_zombie_blood_on"] ) && self.zombie_vars["zombie_powerup_zombie_blood_on"] )
        blood = 1;
    clue = 0;
    left = 0;
    if ( isdefined( level.flag ) && isdefined( level.flag["elemental_staff_air_all_pieces_found"] ) && level.flag["elemental_staff_air_all_pieces_found"] )
        clue = 2;
    else if ( gettime() < level.gnosis_robot_clue_until )
    {
        clue = 1;
        left = level.gnosis_robot_clue_until - gettime();
    }
    invokebuiltin( "GnosisRobotPlayer", self.origin, head, valid, blood, clue, left );
}

// Every sam_clue_giant the game sends. A notify while the clue thread is not in its wait
// starts a new 10 s wait. Approximate: the thread can also stay busy past the 10 s (Samantha
// talking, her line playing), which this copy does not follow.
gnosis_robot_clues()
{
    while ( true )
    {
        level waittill( "sam_clue_giant", player );
        level.gnosis_robot_clue_at = gettime();
        if ( gettime() >= level.gnosis_robot_clue_until )
            level.gnosis_robot_clue_until = gettime() + GNOSIS_ROBOT_CLUE_WAIT_MS;
    }
}

// Each big footstep of one foot, at rumble_and_shake's check: woken by the same notetrack,
// the same 0.2 s later, then after every other thread of that frame, so a notify the game sent
// at this check is already recorded. Publishes the step's route and number in this walk (taken
// at the notetrack, so both feet share one sequence), the trigger and player origins, their
// distance and whether the game sent the notify.
gnosis_robot_steps( id, side )
{
    ai = level.a_giant_robots[id];
    trig = ai.trig_stomp_kill_left;
    foot = 0;
    if ( side == "right" )
    {
        trig = ai.trig_stomp_kill_right;
        foot = 1;
    }
    note = "footstep_" + side + "_large";
    while ( isdefined( ai ) )
    {
        ai waittillmatch( "scripted_walk", note );
        route = gnosis_robot_route( id );
        index = level.gnosis_robot_step_next[id];
        level.gnosis_robot_step_next[id]++;
        wait 0.2;
        waittillframeend;
        level.gnosis_robot_step_checked[id] = index + 1;
        players = getplayers();
        if ( players.size != 1 || !isdefined( trig ) )
            continue;
        notified = 0;
        if ( isdefined( level.gnosis_robot_clue_at ) && level.gnosis_robot_clue_at == gettime() )
            notified = 1;
        invokebuiltin( "GnosisRobotStep", id, route, index, foot, trig.origin, players[0].origin, distance( players[0].origin, trig.origin ), notified );
    }
}

// Mob of the Dead blue-skull aid (2026-10-14). Publishes the Blundergat quest skulls,
// a predicted tomahawk throw, the real flight and the Afterlife shock boxes. The
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

// Mirrors sq_bg_macguffin_think's condition. Only records the time.
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
// within 256 units in 3D. The afterlife-door boxes (zm_prison alcatraz_afterlife_doors)
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
        // A door box can also be an afterlife_interact prop. List it once, with the
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

// Mob of the Dead Afterlife ghost spawns (2026-10-31). Publishes every spawn struct
// (player_respawn_point -> target group) with its lock state, and where a ghost would
// spawn if the player went down now. Vanilla afterlife_get_spawnpoint order: (1) the
// zone rule, check_for_valid_spawn_in_zone, which is pure and called as is, and (2)
// check_for_valid_spawn_near_position, whose get_valid_spawn_location picks a random
// free struct of the group and then keeps it for good in struct.plyr (the lock), so
// later deaths reuse it until a telefrag clears it. (2) is mirrored here read-only:
// nothing is assigned and no script randomness is used (the pick is only "one of the
// free spots"). Kinds sent: 0 unknown or no teleport yet, 1 zone rule, 2 locked,
// 3 random pick within a group. Spot state bits: 1 locked to the player, 2 taken by
// another player, 4 a body would be telefragged there.
gnosis_mob_afterlife()
{
    if ( !builtinfunctionexists( "GnosisMobSpawn" ) || !builtinfunctionexists( "GnosisMobSpawnNext" ) || !builtinfunctionexists( "getfunction" ) )
        return;
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
    while ( !( isdefined( level.struct_class_names ) && isdefined( level.flag ) && isdefined( level.flag["initial_blackscreen_passed"] ) && level.flag["initial_blackscreen_passed"] ) )
        wait 0.5;
    groups = [[ getfunction( "maps/mp/gametypes_zm/_zm_gametype", "get_player_spawns_for_gametype" ) ]]();
    zone_pick = getfunction( "maps/mp/zombies/_zm_afterlife", "check_for_valid_spawn_in_zone" );
    spots = [];
    spot_group = [];
    for ( g = 0; g < groups.size; g++ )
    {
        if ( !isdefined( groups[g].target ) )
            continue;
        foreach ( spot in gnosis_structs( "targetname", groups[g].target ) )
        {
            if ( spots.size >= GNOSIS_MOB_MAX_SPAWNS )
                break;
            spots[spots.size] = spot;
            spot_group[spot_group.size] = g;
        }
    }
    tick = 0;
    while ( isdefined( player ) )
    {
        me = player getentitynumber();
        // A quarter of the spots per tick, so each is refreshed once a second.
        {
            for ( i = tick % 4; i < spots.size; i += 4 )
            {
                state = 0;
                if ( isdefined( spots[i].plyr ) )
                {
                    if ( spots[i].plyr == me )
                        state = 1;
                    else
                        state = 2;
                }
                if ( positionwouldtelefrag( spots[i].origin ) )
                    state = state + 4;
                invokebuiltin( "GnosisMobSpawn", i, spots[i].origin, spot_group[i], state );
            }
        }
        kind = 0;
        index = -1;
        group = -1;
        started = isdefined( level.flag["afterlife_start_over"] ) && level.flag["afterlife_start_over"];
        if ( started && !( isdefined( player.afterlife ) && player.afterlife ) )
        {
            pick = [[ zone_pick ]]( player );
            if ( isdefined( pick ) )
            {
                kind = 1;
                index = gnosis_mob_spot_index( spots, pick );
            }
            else
            {
                group = gnosis_mob_spawn_group( groups, player.origin );
                if ( isdefined( group ) )
                {
                    kind = 3;
                    for ( i = 0; i < spots.size; i++ )
                    {
                        if ( spot_group[i] == group && isdefined( spots[i].plyr ) && spots[i].plyr == me && !positionwouldtelefrag( spots[i].origin ) )
                        {
                            kind = 2;
                            index = i;
                        }
                    }
                }
                else
                    group = -1;
            }
        }
        invokebuiltin( "GnosisMobSpawnNext", kind, index, group );
        tick++;
        wait 0.25;
    }
}

gnosis_mob_spot_index( spots, pick )
{
    for ( i = 0; i < spots.size; i++ )
    {
        if ( distancesquared( spots[i].origin, pick.origin ) < 1 )
            return i;
    }
    return -1;
}

// check_for_valid_spawn_near_position's group choice, loop for loop (including the
// backup group taking over inside the loop).
gnosis_mob_spawn_group( groups, position )
{
    closest_group = undefined;
    closest_distance = 100000000;
    backup_group = undefined;
    backup_distance = 100000000;
    for ( i = 0; i < groups.size; i++ )
    {
        ideal_distance = 1000;
        if ( isdefined( groups[i].script_int ) )
            ideal_distance = groups[i].script_int;
        if ( isdefined( groups[i].locked ) && groups[i].locked == 0 )
        {
            dist = distancesquared( position, groups[i].origin );
            if ( dist < ideal_distance * ideal_distance )
            {
                if ( dist < closest_distance )
                {
                    closest_distance = dist;
                    closest_group = i;
                }
            }
            else if ( dist < backup_distance )
            {
                backup_group = i;
                backup_distance = dist;
            }
        }
        if ( !isdefined( closest_group ) )
            closest_group = backup_group;
    }
    return closest_group;
}

// TranZit Avogadro aid (2026-10-30). Mirrors vanilla _zm_ai_avogadro's think loop with
// field reads and the same queries it makes (getanimfromasd, getmovedelta,
// maymovefrompointtopoint, bullettracepassed). The plugin draws the result. Nothing here
// may use script randomness: vanilla rolls storm regions, dashes and return rounds with
// randomint, so a helper roll (array_randomize included) would shift them.
gnosis_avogadro_aid()
{
    if ( !builtinfunctionexists( "GnosisAvogadroState" ) )
        return;
    while ( !isdefined( level.script ) )
        wait 0.05;
    if ( level.script != "zm_transit" )
        return;
    tick = 0;
    while ( true )
    {
        players = getplayers();
        avogadro = level.avogadro;
        if ( players.size == 1 && isdefined( avogadro ) && isdefined( avogadro.state ) )
        {
            avogadro gnosis_avogadro_publish( players[0] );
            // Dash queries run in their own thread, as gnosis_query_detour does: an error
            // there ends only that thread, and its flag stays set so none run again.
            if ( tick % 2 == 0 && ( avogadro.state == "chasing" || avogadro.state == "idle" ) && !( isdefined( level.gnosis_avogadro_dash_query ) && level.gnosis_avogadro_dash_query ) )
                avogadro thread gnosis_avogadro_dashes();
            if ( tick % 20 == 0 )
                avogadro gnosis_avogadro_spawns();
        }
        tick = ( tick + 1 ) % 1000;
        wait 0.05;
    }
}

gnosis_avogadro_publish( player )
{
    now = gettime();
    state = gnosis_avogadro_state_code( self.state );
    flags = 0;
    if ( isdefined( self.in_pain ) && self.in_pain )
        flags = flags | GNOSIS_AVO_PAIN;
    if ( isdefined( self.shield ) && self.shield )
        flags = flags | GNOSIS_AVO_SHIELD;
    if ( isdefined( self.is_teleport ) && self.is_teleport )
        flags = flags | GNOSIS_AVO_TELEPORT;
    if ( isdefined( self.is_traversing ) && self.is_traversing )
        flags = flags | GNOSIS_AVO_TRAVERSE;
    enemy = self.favoriteenemy;
    if ( isdefined( enemy ) && isdefined( enemy.isonbus ) && enemy.isonbus )
        flags = flags | GNOSIS_AVO_ENEMY_ON_BUS;
    region = -1;
    if ( isdefined( self.current_region ) )
    {
        region = gnosis_avogadro_region_code( self.current_region );
        if ( gnosis_avogadro_region_occupied( self.current_region ) )
            flags = flags | GNOSIS_AVO_REGION_OCCUPIED;
    }
    // region_empty: any player not in last stand within 3000 keeps him
    // (_zm_laststand player_is_in_laststand).
    downed = isdefined( player.revivetrigger );
    if ( isdefined( player.no_revive_trigger ) && player.no_revive_trigger )
        downed = isdefined( player.laststand ) && player.laststand;
    if ( !downed && distancesquared( self.origin, player.origin ) < 9000000 )
        flags = flags | GNOSIS_AVO_PLAYER_NEAR;
    if ( isdefined( level.flag ) && isdefined( level.flag["power_on"] ) && level.flag["power_on"] )
        flags = flags | GNOSIS_AVO_POWER_ON;
    if ( isdefined( level.flag ) && isdefined( level.flag["switches_on"] ) && level.flag["switches_on"] )
        flags = flags | GNOSIS_AVO_SWITCHES_ON;
    // Storm timing as seen here, at most 50 ms late: vanilla's cloud thread picks a region
    // on entry (always power) and again every 30 s. Returning, _in_cloud drops to 0 and,
    // after the arrival animation, he waits 30 s for a player to enter the region.
    timer = -1;
    if ( state == 10 )
    {
        if ( !isdefined( level.gnosis_avogadro_cloud_start ) )
            level.gnosis_avogadro_cloud_start = now;
        // _in_cloud keeps the 0 of an earlier return until the first tick in the storm.
        if ( isdefined( self._in_cloud ) && self._in_cloud )
            level.gnosis_avogadro_cloud_seen = 1;
        else if ( isdefined( level.gnosis_avogadro_cloud_seen ) )
        {
            flags = flags | GNOSIS_AVO_ARRIVING;
            if ( !isdefined( level.gnosis_avogadro_landing ) )
                level.gnosis_avogadro_landing = now;
        }
        if ( flags & GNOSIS_AVO_ARRIVING )
        {
            if ( isdefined( level.gnosis_avogadro_arrival_ms ) )
                timer = level.gnosis_avogadro_landing + level.gnosis_avogadro_arrival_ms + 30000 - now;
            else if ( !isdefined( level.gnosis_avogadro_arrival_query ) )
                self thread gnosis_avogadro_arrival_length();
        }
        else
            timer = 30000 - ( now - level.gnosis_avogadro_cloud_start ) % 30000;
    }
    else
    {
        level.gnosis_avogadro_cloud_start = undefined;
        level.gnosis_avogadro_cloud_seen = undefined;
        level.gnosis_avogadro_landing = undefined;
        if ( isdefined( self.region_timer ) )
            timer = self.region_timer - now;
    }
    // check_range_attack with every condition evaluated, so the plugin can show which
    // fails. While he waits to be seen, player_look's facing test instead.
    target = enemy;
    if ( !isdefined( target ) && state == 2 )
        target = player;
    if ( isdefined( target ) )
    {
        to_target = target.origin - self.origin;
        forward = vectornormalize( anglestoforward( self.angles ) );
        eye = self geteye();
        target_eye = target geteye();
        if ( isdefined( enemy ) )
        {
            dist_sq = lengthsquared( to_target );
            if ( dist_sq > 14400 && dist_sq < 360000 )
            {
                flags = flags | GNOSIS_AVO_BOLT_RANGE;
                if ( bullettracepassed( eye, target_eye, 0, undefined ) )
                    flags = flags | GNOSIS_AVO_BOLT_SIGHT;
            }
            if ( vectordot( forward, vectornormalize( to_target ) ) > 0.99 )
                flags = flags | GNOSIS_AVO_BOLT_FACING;
        }
        look = -2;
        if ( state == 2 )
            look = vectordot( vectornormalize( anglestoforward( player.angles ) ), vectornormalize( self.origin - player.origin ) );
        invokebuiltin( "GnosisAvogadroAim", forward, eye, target_eye, length( to_target ), look );
    }
    hits = 0;
    if ( isdefined( self.hit_by_melee ) )
        hits = self.hit_by_melee;
    absorbed = 0;
    if ( isdefined( self.damage_absorbed ) )
        absorbed = self.damage_absorbed;
    phase_ms = 0;
    if ( isdefined( self.phase_time ) )
        phase_ms = self.phase_time - now;
    return_round = -1;
    if ( isdefined( self.return_round ) )
        return_round = self.return_round;
    round_number = 0;
    if ( isdefined( level.round_number ) )
        round_number = level.round_number;
    invokebuiltin( "GnosisAvogadroState", self getentitynumber(), state, flags, hits, absorbed, phase_ms, region, return_round, round_number, timer );
    if ( state >= 5 && state <= 7 && isdefined( level.the_bus ) )
        self gnosis_avogadro_bus( state );
}

gnosis_avogadro_state_code( state )
{
    if ( !isdefined( state ) )
        return 0;
    switch ( state )
    {
        case "chamber":
            return 1;
        case "wait_for_player":
            return 2;
        case "idle":
            return 3;
        case "chasing":
            return 4;
        case "chasing_bus":
            return 5;
        case "attacking_bus":
            return 6;
        case "stay_attached":
            return 7;
        case "phasing":
            return 8;
        case "exiting":
            return 9;
        case "cloud":
            return 10;
    }
    return 0;
}

// Vanilla's level.transit_region keys, in cloud_update_fx's order.
gnosis_avogadro_region_code( region )
{
    names = array( "bus", "diner", "farm", "cornfield", "power", "town" );
    for ( i = 0; i < names.size; i++ )
    {
        if ( names[i] == region )
            return i;
    }
    return -1;
}

gnosis_avogadro_zone_region( zone )
{
    if ( !isdefined( level.transit_region ) )
        return -1;
    names = array( "bus", "diner", "farm", "cornfield", "power", "town" );
    for ( i = 0; i < names.size; i++ )
    {
        region = level.transit_region[names[i]];
        if ( !isdefined( region ) || !isdefined( region.zones ) )
            continue;
        foreach ( name in region.zones )
        {
            if ( name == zone )
                return i;
        }
    }
    return -1;
}

// get_player_in_region without its array_randomize: is any zone of the region occupied.
gnosis_avogadro_region_occupied( region )
{
    if ( !isdefined( level.transit_region ) || !isdefined( level.transit_region[region] ) || !isdefined( level.zones ) )
        return 0;
    foreach ( zone in level.transit_region[region].zones )
    {
        if ( isdefined( level.zones[zone] ) && isdefined( level.zones[zone].is_occupied ) && level.zones[zone].is_occupied )
            return 1;
    }
    return 0;
}

// Bus states: chasing_bus runs to the nearest bus opening (chase_bus_entry). The attack
// stops the bus after 20 s of draining and shocks riders every 2 s (bus_disable,
// attach_update).
gnosis_avogadro_bus( state )
{
    tag = undefined;
    if ( state == 5 && isdefined( self.chase_bus_entry ) )
        tag = self.chase_bus_entry.tagname;
    else if ( state != 5 && isdefined( self.bus_attack_struct ) )
        tag = self.bus_attack_struct.window_tag;
    if ( !isdefined( tag ) )
        return;
    attack_ms = -1;
    if ( state == 6 && isdefined( self.bus_attack_time ) )
        attack_ms = int( 20000 - self.bus_attack_time * 1000 );
    shock_ms = -1;
    if ( state != 5 && isdefined( self.bus_shock_time ) )
        shock_ms = int( 2000 - self.bus_shock_time * 1000 );
    invokebuiltin( "GnosisAvogadroBus", level.the_bus gettagorigin( tag ), attack_ms, shock_ms );
}

// The 12 dashes check_phase picks from (level.avogadro_phase: forward, left, right, back,
// substates 0-2), with vanilla's end point (getanimendpos is localtoworldcoords of
// getmovedelta( anim, 0, 1 )) and its test: a dash is taken only when
// maymovefrompointtopoint clears it.
gnosis_avogadro_dashes()
{
    level.gnosis_avogadro_dash_query = 1;
    if ( !isdefined( level.avogadro_phase ) || level.avogadro_phase.size != 4 )
        return;
    for ( i = 0; i < 4; i++ )
    {
        for ( j = 0; j < 3; j++ )
        {
            anim_id = self getanimfromasd( level.avogadro_phase[i].animstate, j );
            end = self localtoworldcoords( getmovedelta( anim_id, 0, 1 ) );
            clear = 0;
            if ( self maymovefrompointtopoint( self.origin, end ) )
                clear = 1;
            invokebuiltin( "GnosisAvogadroDash", i * 3 + j, end, clear );
        }
    }
    level.gnosis_avogadro_dash_query = 0;
}

// Arrival animation length (cloud_update plays zm_arrival before its 30 s wait). Its own
// thread: if the query errors, the leave time is shown without it.
gnosis_avogadro_arrival_length()
{
    level.gnosis_avogadro_arrival_query = 1;
    level.gnosis_avogadro_arrival_ms = int( getanimlength( self getanimfromasd( "zm_arrival", 0 ) ) * 1000 );
}

// Every avogadro_location, flagged when cloud_find_spawn could pick it now: in a zone of
// the storm's region that is enabled and allows spawning (the Bus Depot's zone_pri only
// once occupied or its yard door is open). Unlike ordinary spawns the zone need not be
// active, and the spot's own is_enabled is not checked.
gnosis_avogadro_spawns()
{
    if ( !isdefined( level.zones ) )
        return;
    storm = -1;
    if ( self.state == "cloud" && isdefined( self.current_region ) )
        storm = gnosis_avogadro_region_code( self.current_region );
    index = 0;
    foreach ( name in getarraykeys( level.zones ) )
    {
        zone = level.zones[name];
        if ( !isdefined( zone.avogadro_locations ) )
            continue;
        region = gnosis_avogadro_zone_region( name );
        candidate = 0;
        if ( storm >= 0 && region == storm && isdefined( zone.is_enabled ) && zone.is_enabled && isdefined( zone.is_spawning_allowed ) && zone.is_spawning_allowed )
        {
            candidate = 1;
            if ( name == "zone_pri" && !( isdefined( zone.is_occupied ) && zone.is_occupied ) && !( isdefined( level.flag ) && isdefined( level.flag["OnPriDoorYar2"] ) && level.flag["OnPriDoorYar2"] ) )
                candidate = 0;
        }
        foreach ( spot in zone.avogadro_locations )
        {
            if ( index >= GNOSIS_AVO_MAX_SPAWNS )
                return;
            if ( !isdefined( spot.origin ) )
                continue;
            invokebuiltin( "GnosisAvogadroSpawn", index, spot.origin, candidate, region );
            index++;
        }
    }
}

// Thrown-grenade aid (2026-11-03): frag, EMP and the Origins G-strike. 20 times a second
// the predicted path is published: of the grenade held back, else of every throwable carried
// (the plugin draws one of them, by its lethal or tactical priority option). The arc runs in
// 0.05 s steps (the server frame the engine moves missiles in), bounced at each bullettrace
// hit, until it rests or its fuse ends. Every real throw is published too, from the engine's
// own notifies: positions each frame, grenade_bounce, stationary and explode.
gnosis_throw_aid()
{
    while ( !isdefined( level.script ) )
        wait 0.05;
    players = getplayers();
    while ( players.size != 1 )
    {
        wait 0.5;
        players = getplayers();
    }
    player = players[0];
    player endon( "disconnect" );
    level.gnosis_throw_seq = 0;
    gnosis_throw_models();
    level thread gnosis_throw_world();
    player thread gnosis_throw_pullbacks();
    player thread gnosis_throw_flights();
    while ( true )
    {
        player gnosis_throw_aim();
        wait 0.05;
    }
}

gnosis_throw_aim()
{
    // Down (vanilla player_is_in_laststand) or spectating: nothing to throw.
    if ( isdefined( self.revivetrigger ) || self.sessionstate != "playing" )
    {
        self.gnosis_throw_pulled = undefined;
        return;
    }
    if ( isdefined( self.gnosis_throw_pulled ) && gettime() - self.gnosis_throw_pulled_at > 30000 )
        self.gnosis_throw_pulled = undefined;
    if ( isdefined( self.gnosis_throw_pulled ) )
    {
        self gnosis_throw_predict( self.gnosis_throw_pulled, ( gettime() - self.gnosis_throw_pulled_at ) / 1000, 1 );
        return;
    }
    carried = [];
    if ( isdefined( self.current_lethal_grenade ) )
        carried[carried.size] = self.current_lethal_grenade;
    if ( isdefined( self.current_tactical_grenade ) )
        carried[carried.size] = self.current_tactical_grenade;
    foreach ( weapon in carried )
    {
        kind = gnosis_throw_kind( weapon );
        if ( kind && self getweaponammoclip( weapon ) > 0 )
            self gnosis_throw_predict( kind, 0, 0 );
    }
}

// Held back from grenade_pullback until grenade_fire (gnosis_throw_flights), going down, or 30 s.
// Not isthrowinggrenade(): with it the frag path showed only for a moment in game (2026-11-03).
gnosis_throw_pullbacks()
{
    self endon( "disconnect" );
    while ( true )
    {
        self waittill( "grenade_pullback", weapon );
        kind = gnosis_throw_kind( weapon );
        if ( !kind )
            continue;
        self.gnosis_throw_pulled = kind;
        self.gnosis_throw_pulled_at = gettime();
    }
}

gnosis_throw_kind( weapon )
{
    if ( !isdefined( weapon ) || !isstring( weapon ) )
        return 0;
    if ( weapon == "frag_grenade_zm" )
        return GNOSIS_THROW_FRAG;
    if ( weapon == "emp_grenade_zm" )
        return GNOSIS_THROW_EMP;
    if ( weapon == "beacon_zm" )
        return GNOSIS_THROW_BEACON;
    return 0;
}

// Throw model per kind. NOT fitted for these grenades yet: the launch is the tomahawk's
// measured throw (930 units/s along the view pitched 7.1 degrees up, from the eye plus 7
// along the view, plus the player's horizontal velocity) and the rest are placeholders,
// until research/tools/gnosis_throw_probe.gsc has measured them. fuse: seconds from the
// throw (from the pullback when cook is set), -1 when the path simply runs to rest.
// across/along: share of the velocity kept across and along the surface at a bounce.
// stop: speed below which a grenade rests after a floor bounce. stick: rests at the first hit.
gnosis_throw_models()
{
    level.gnosis_throw_models = [];
    for ( kind = GNOSIS_THROW_FRAG; kind <= GNOSIS_THROW_BEACON; kind++ )
    {
        model = spawnstruct();
        model.speed = 930;
        model.pitch = 7.1;
        model.forward = 7;
        model.fuse = -1;
        model.cook = 0;
        model.across = 0.3;
        model.along = 0.6;
        model.stop = 60;
        model.stick = 0;
        level.gnosis_throw_models[kind] = model;
    }
    level.gnosis_throw_models[GNOSIS_THROW_FRAG].fuse = 3;
    level.gnosis_throw_models[GNOSIS_THROW_FRAG].cook = 1;
}

// Once a second: the EMP radii (vanilla zombie_vars, read live) and the player volumes the
// G-strike zone check looks at.
gnosis_throw_world()
{
    while ( true )
    {
        if ( isdefined( level.zombie_vars ) && isdefined( level.zombie_vars["emp_perk_off_range"] ) && isdefined( level.zombie_vars["emp_stun_range"] ) )
            invokebuiltin( "GnosisThrowRadii", GNOSIS_THROW_EMP, level.zombie_vars["emp_perk_off_range"], level.zombie_vars["emp_stun_range"] );
        level.gnosis_throw_volumes = getentarray( "player_volume", "script_noteworthy" );
        wait 1;
    }
}

gnosis_throw_predict( kind, held, pulled )
{
    model = level.gnosis_throw_models[kind];
    angles = self getplayerangles();
    start = self geteye() + anglestoforward( angles ) * model.forward;
    velocity = self getvelocity();
    v = anglestoforward( ( angles[0] - model.pitch, angles[1], 0 ) ) * model.speed + ( velocity[0], velocity[1], 0 );
    fuse = model.fuse;
    if ( fuse > 0 && model.cook )
        fuse = max( fuse - held, 0 );
    level.gnosis_throw_seq++;
    if ( level.gnosis_throw_seq > 1000000000 )
        level.gnosis_throw_seq = 1;
    seq = level.gnosis_throw_seq;
    // No wait inside the publication: Begin, points, strikes and End form one transaction.
    invokebuiltin( "GnosisThrowBegin", kind, seq, pulled );
    invokebuiltin( "GnosisThrowPoint", seq, start, 0 );
    p = start;
    t = 0;
    bounces = 0;
    for ( n = 1; n < GNOSIS_THROW_MAX_POINTS; n++ )
    {
        if ( fuse >= 0 && t >= fuse - 0.001 )
        {
            invokebuiltin( "GnosisThrowPoint", seq, p, GNOSIS_THROW_BURST );
            break;
        }
        dt = GNOSIS_THROW_STEP;
        if ( fuse >= 0 && t + dt > fuse )
            dt = fuse - t;
        q = p + v * dt + ( 0, 0, -0.5 * GNOSIS_THROW_GRAVITY * dt * dt );
        trace = bullettrace( p, q, 0, self );
        if ( !isdefined( trace ) || trace["fraction"] >= 1 )
        {
            p = q;
            v = v + ( 0, 0, -1 * GNOSIS_THROW_GRAVITY * dt );
            t = t + dt;
            invokebuiltin( "GnosisThrowPoint", seq, p, 0 );
            continue;
        }
        t = t + dt * trace["fraction"];
        v = v + ( 0, 0, -1 * GNOSIS_THROW_GRAVITY * dt * trace["fraction"] );
        normal = trace["normal"];
        p = trace["position"];
        into = vectordot( v, normal );
        if ( into < 0 )
        {
            across = normal * into;
            v = ( v - across ) * model.along - across * model.across;
        }
        bounces++;
        if ( model.stick || bounces >= GNOSIS_THROW_MAX_BOUNCES || normal[2] > 0.7 && length( v ) < model.stop )
        {
            invokebuiltin( "GnosisThrowPoint", seq, p, GNOSIS_THROW_REST );
            break;
        }
        invokebuiltin( "GnosisThrowPoint", seq, p, GNOSIS_THROW_BOUNCE );
        // Off the surface, so the next step does not start inside it.
        p = p + normal * 0.5;
    }
    left = -1;
    if ( fuse >= 0 )
        left = max( fuse - t, 0 );
    valid = 1;
    if ( kind == GNOSIS_THROW_BEACON )
    {
        strikes = gnosis_throw_strikes( p );
        for ( i = 0; i < strikes.size; i++ )
            invokebuiltin( "GnosisThrowStrike", seq, i, strikes[i] );
        valid = gnosis_throw_zone_ok( p );
    }
    invokebuiltin( "GnosisThrowEnd", seq, left, valid );
}

// Where each G-strike shell lands, as vanilla artillery_barrage_logic computes it: a trace
// from the shell's start (8500 up, offset 6500 sideways for the corner shells) lowered by
// 5000 to its landing offset. A roof or wall in between takes the shell.
gnosis_throw_strikes( origin )
{
    lands = [];
    lands[0] = ( 0, 0, 0 );
    lands[1] = ( -72, 72, 0 );
    lands[2] = ( 72, 72, 0 );
    lands[3] = ( 72, -72, 0 );
    lands[4] = ( -72, -72, 0 );
    starts = [];
    starts[0] = ( 0, 0, 8500 );
    starts[1] = ( -6500, 6500, 8500 );
    starts[2] = ( 6500, 6500, 8500 );
    starts[3] = ( 6500, -6500, 8500 );
    starts[4] = ( -6500, -6500, 8500 );
    points = [];
    for ( i = 0; i < lands.size; i++ )
    {
        trace = bullettrace( origin + starts[i] - ( 0, 0, 5000 ), origin + lands[i], 0, undefined );
        points[i] = origin + lands[i];
        if ( isdefined( trace ) )
            points[i] = trace["position"];
    }
    return points;
}

// Approximates vanilla check_point_in_enabled_zone, which spawns a script_origin 40 units up
// and tests istouching (an observer must not spawn entities): the point 40 up inside the
// absolute bounds of an enabled zone's player volume. Bounds can be larger than a rotated
// volume, so a spot at a zone's edge can read valid and still be a dud.
gnosis_throw_zone_ok( origin )
{
    if ( !isdefined( level.zones ) || !isdefined( level.gnosis_throw_volumes ) )
        return 1;
    point = origin + ( 0, 0, 40 );
    foreach ( volume in level.gnosis_throw_volumes )
    {
        if ( !isdefined( volume ) || !isdefined( volume.targetname ) || !isdefined( level.zones[volume.targetname] ) )
            continue;
        zone = level.zones[volume.targetname];
        if ( !( isdefined( zone.is_enabled ) && zone.is_enabled ) )
            continue;
        lo = volume getabsmins();
        hi = volume getabsmaxs();
        if ( point[0] >= lo[0] && point[0] <= hi[0] && point[1] >= lo[1] && point[1] <= hi[1] && point[2] >= lo[2] && point[2] <= hi[2] )
            return 1;
    }
    return 0;
}

// The real flight of every throw. Events (grenade::Event): 0 position, 1 bounce, 2 at rest,
// 3 exploded, 4 still there, 5 G-strike accepted, 6 G-strike dud (the game's own verdict:
// player_throw_beacon sets attract_to_origin only for a beacon in an enabled zone).
gnosis_throw_flights()
{
    self endon( "disconnect" );
    throws = 0;
    while ( true )
    {
        self waittill( "grenade_fire", grenade, weapon );
        self.gnosis_throw_pulled = undefined;
        kind = gnosis_throw_kind( weapon );
        if ( !kind || !isdefined( grenade ) )
            continue;
        throws++;
        state = spawnstruct();
        state.id = throws;
        state.kind = kind;
        level thread gnosis_throw_track( grenade, state );
        level thread gnosis_throw_bounces( grenade, state );
        level thread gnosis_throw_rest( grenade, state );
        level thread gnosis_throw_explode( grenade, state );
    }
}

gnosis_throw_track( grenade, state )
{
    for ( frame = 0; frame < GNOSIS_THROW_MAX_FRAMES && isdefined( grenade ) && !isdefined( state.done ); frame++ )
    {
        event = 0;
        if ( isdefined( state.rest ) )
            event = 4;
        invokebuiltin( "GnosisThrowFlight", state.id, state.kind, grenade.origin, event );
        wait 0.05;
    }
}

gnosis_throw_bounces( grenade, state )
{
    grenade endon( "death" );
    while ( !isdefined( state.done ) )
    {
        grenade waittill( "grenade_bounce", position );
        invokebuiltin( "GnosisThrowFlight", state.id, state.kind, position, 1 );
    }
}

gnosis_throw_rest( grenade, state )
{
    grenade waittill( "stationary" );
    state.rest = 1;
    origin = grenade.origin;
    invokebuiltin( "GnosisThrowFlight", state.id, state.kind, origin, 2 );
    if ( state.kind != GNOSIS_THROW_BEACON )
        return;
    strikes = gnosis_throw_strikes( origin );
    for ( i = 0; i < strikes.size; i++ )
        invokebuiltin( "GnosisThrowFlightStrike", state.id, i, strikes[i] );
    // Vanilla decides in the frame the beacon comes to rest.
    wait 0.1;
    event = 6;
    if ( isdefined( grenade ) && isdefined( grenade.attract_to_origin ) && grenade.attract_to_origin )
        event = 5;
    invokebuiltin( "GnosisThrowFlight", state.id, state.kind, origin, event );
}

gnosis_throw_explode( grenade, state )
{
    grenade waittill( "explode", position );
    state.done = 1;
    invokebuiltin( "GnosisThrowFlight", state.id, state.kind, position, 3 );
}
