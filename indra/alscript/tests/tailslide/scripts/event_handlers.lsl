default {
    at_rot_target( integer tnum, rotation targetrot, rotation ourrot ) {}
    at_target( integer tnum, vector targetpos, vector ourpos ) {}
    attach( key id ) {}
    changed( integer change ) {}
    collision( integer num_detected ) {}
    collision_end( integer num_detected ) {}
    collision_start( integer num_detected ) {}
    control( key id, integer level, integer edge ) {}
    dataserver( key queryid, string data ) {}
    email( string time, string address, string subj, string message, integer num_left ) {}
    http_request( key id, string method, string body ) {}
    http_response( key request_id, integer status, list metadata, string body ) {}
    land_collision( vector pos ) {}
    land_collision_end( vector pos ) {}
    land_collision_start( vector pos ) {}
    link_message( integer sender_num, integer num, string str, key id ) {}
    listen( integer channel, string name, key id, string message ) {}
    money( key id, integer amount ) {}
    moving_end(  ) {}
    moving_start(  ) {}
    no_sensor(  ) {}
    not_at_rot_target(  ) {}
    not_at_target(  ) {}
    object_rez( key id ) {}
    on_rez( integer start_param ) {}
    remote_data( integer event_type, key channel, key message_id, string sender, integer idata, string sdata ) {}
    run_time_permissions( integer perm ) {}
    sensor( integer num_detected ) {}
    state_entry(  ) {}
    state_exit(  ) {}
    timer(  ) {}
    touch( integer num_detected ) {}
    touch_end( integer num_detected ) {}
    touch_start( integer num_detected ) {}
    // these are new so we don't know their event numbers for sure.
    // transaction_result( key id, integer success, string data ) {}
    // path_update( integer type, list reserved ) {}
    // experience_permissions( key agent ) {}
    // experience_permissions_denied( key agent, integer reason ) {}
}
