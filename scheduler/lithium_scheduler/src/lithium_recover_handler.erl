-module(lithium_recover_handler).
-export([init/2]).

init(Req0, State) ->
    {ok, Body, Req1} = cowboy_req:read_body(Req0),
    Fingerprint = get_field(Body, <<"fingerprint">>, <<"">>),
    NewPubKey   = get_field(Body, <<"new_public_key">>, <<"">>),

    io:format("  [recover] Recovery attempt — fingerprint: ~s...~n",
              [binary:part(Fingerprint, 0, min(16, byte_size(Fingerprint)))]),

    case find_node_by_fingerprint(Fingerprint) of
        {error, not_found} ->
            io:format("  [recover] No matching node found~n"),
            reply(404, <<"{\"error\":\"no node found for this hardware\"}">>,
                  Req1, State);
        {ok, Node} ->
            NodeId = maps:get(node_id, Node),
            Score  = maps:get(score,   Node),

            %% update public key in registry
            lithium_registry:update_score(NodeId, #{
                public_key => NewPubKey
            }),
            lithium_db:save_node(NodeId, maps:put(public_key, NewPubKey, Node)),

            io:format("  [recover] Node ~s recovered (score: ~p)~n",
                      [NodeId, Score]),

            RespBody = list_to_binary(io_lib:format(
                "{\"status\":\"ok\",\"node_id\":\"~s\","
                "\"score\":~p,\"message\":\"keypair replaced\"}",
                [NodeId, Score])),

            reply(200, RespBody, Req1, State)
    end.

find_node_by_fingerprint(Fingerprint) ->
    Nodes = lithium_registry:all_nodes(),
    %% look through all nodes for matching fingerprint
    Matches = lists:filter(fun(Node) ->
        StoredFp = maps:get(fingerprint, Node, <<"">>),
        StoredFp =:= Fingerprint
    end, Nodes),
    case Matches of
        []      -> {error, not_found};
        [Node|_] -> {ok, Node}
    end.

reply(Code, Body, Req, State) ->
    Req2 = cowboy_req:reply(Code,
        #{<<"content-type">> => <<"application/json">>},
        Body, Req),
    {ok, Req2, State}.

get_field(Body, Field, Default) ->
    Str = binary_to_list(Body),
    Key = "\"" ++ binary_to_list(Field) ++ "\":\"",
    case string:str(Str, Key) of
        0   -> Default;
        Pos ->
            Start = Pos + length(Key),
            Rest  = string:sub_string(Str, Start),
            End   = string:str(Rest, "\""),
            if End > 0 -> list_to_binary(string:sub_string(Rest, 1, End - 1));
               true    -> Default
            end
    end.
