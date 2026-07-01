-module(lithium_heartbeat_handler).
-export([init/2]).

init(Req0, State) ->
    Method = cowboy_req:method(Req0),
    handle(Method, Req0, State).

handle(<<"POST">>, Req0, State) ->
    {ok, Body, Req1} = cowboy_req:read_body(Req0),
    case parse_json_field(Body, <<"node_id">>) of
        {ok, NodeId} ->
            case lithium_registry:get_node(NodeId) of
                {ok, _Node} ->
                    lithium_registry:update_heartbeat(NodeId),
                    reply(200, <<"{\"status\":\"ok\",\"message\":\"heartbeat received\"}">>, Req1, State);
                {error, not_found} ->
                    %% auto-register if new node sends heartbeat with public key
                    case parse_json_field(Body, <<"public_key">>) of
                        {ok, PubKey} ->
                            lithium_registry:register_node(NodeId, PubKey),
                            reply(200, <<"{\"status\":\"ok\",\"message\":\"node registered\"}">>, Req1, State);
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

%% minimal json field extractor — no deps needed
parse_json_field(Body, Field) ->
    Str = binary_to_list(Body),
    Key = "\"" ++ binary_to_list(Field) ++ "\":\"",
    case string:str(Str, Key) of
        0 -> {error, not_found};
        Pos ->
            Start = Pos + length(Key),
            Rest  = string:sub_string(Str, Start),
            End   = string:str(Rest, "\""),
            if End > 0 ->
                Val = string:sub_string(Rest, 1, End - 1),
                {ok, list_to_binary(Val)};
               true -> {error, malformed}
            end
    end.
