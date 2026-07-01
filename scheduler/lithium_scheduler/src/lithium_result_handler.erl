-module(lithium_result_handler).
-export([init/2]).

init(Req0, State) ->
    {ok, Body, Req1} = cowboy_req:read_body(Req0),
    JobId  = get_field(Body, <<"job_id">>,  <<"">>),
    NodeId = get_field(Body, <<"node_id">>, <<"">>),
    Output = get_field(Body, <<"output">>,  <<"">>),

    io:format("  [result] Job ~s complete on ~s~n", [JobId, NodeId]),

    lithium_job_registry:update_job_status(JobId, #{
        status => complete,
        result => Output
    }),

    Req2 = cowboy_req:reply(200,
        #{<<"content-type">> => <<"application/json">>},
        <<"{\"status\":\"ok\"}">>, Req1),
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
