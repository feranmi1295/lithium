-module(lithium_heartbeat_handler).
-export([init/2]).

init(Req0, State) ->
    Method = cowboy_req:method(Req0),
    handle(Method, Req0, State).

handle(<<"POST">>, Req0, State) ->
    {ok, Body, Req1} = cowboy_req:read_body(Req0),
    case get_field(Body, <<"node_id">>) of
        {ok, NodeId} ->
            Ip   = get_field_default(Body, <<"ip">>,   <<"127.0.0.1">>),
            Port = get_field_default(Body, <<"port">>,  <<"7701">>),
            case lithium_registry:get_node(NodeId) of
                {ok, _} ->
                    lithium_registry:update_heartbeat(NodeId, Ip, Port),
                    reply(200, <<"{\"status\":\"ok\"}">>, Req1, State);
                {error, not_found} ->
                    case get_field(Body, <<"public_key">>) of
                        {ok, PubKey} ->
                            lithium_registry:register_node(NodeId, PubKey, Ip, Port),
                            reply(200, <<"{\"status\":\"ok\",\"message\":\"registered\"}">>, Req1, State);
                        _ ->
                            reply(404, <<"{\"error\":\"node not found\"}">>, Req1, State)
                    end
            end;
        _ ->
            reply(400, <<"{\"error\":\"missing node_id\"}">>, Req1, State)
    end;

handle(_, Req, State) ->
    reply(405, <<"{\"error\":\"method not allowed\"}">>, Req, State).

reply(Code, Body, Req, State) ->
    Req2 = cowboy_req:reply(Code,
        #{<<"content-type">> => <<"application/json">>},
        Body, Req),
    {ok, Req2, State}.

get_field(Body, Field) ->
    Str = binary_to_list(Body),
    Key = "\"" ++ binary_to_list(Field) ++ "\":\"",
    case string:str(Str, Key) of
        0   -> {error, not_found};
        Pos ->
            Start = Pos + length(Key),
            Rest  = string:sub_string(Str, Start),
            End   = string:str(Rest, "\""),
            if End > 0 -> {ok, list_to_binary(string:sub_string(Rest, 1, End - 1))};
               true    -> {error, malformed}
            end
    end.

get_field_default(Body, Field, Default) ->
    case get_field(Body, Field) of
        {ok, Val} -> Val;
        _         -> Default
    end.
