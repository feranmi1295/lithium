-module(lithium_result_handler).
-export([init/2]).

init(Req0, State) ->
    {ok, Body, Req1} = cowboy_req:read_body(Req0),
    JobId  = get_str_field(Body, <<"job_id">>,  <<"">>),
    NodeId = get_str_field(Body, <<"node_id">>, <<"">>),
    Output = get_str_field(Body, <<"output">>,  <<"">>),

    %% exit_code is a number not a string — parse differently
    ExitCode = get_int_field(Body, <<"exit_code">>),

    io:format("  [result] Job ~s complete on ~s (exit: ~p)~n",
              [JobId, NodeId, ExitCode]),

    lithium_job_registry:update_job_status(JobId, #{
        status => complete,
        result => Output
    }),

    case ExitCode of
        0 -> lithium_scoring:job_success(NodeId);
        _ -> lithium_scoring:job_failure(NodeId)
    end,

    Req2 = cowboy_req:reply(200,
        #{<<"content-type">> => <<"application/json">>},
        <<"{\"status\":\"ok\"}">>, Req1),
    {ok, Req2, State}.

get_str_field(Body, Field, Default) ->
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

get_int_field(Body, Field) ->
    Str = binary_to_list(Body),
    Key = "\"" ++ binary_to_list(Field) ++ "\":",
    case string:str(Str, Key) of
        0   -> 0;
        Pos ->
            Start = Pos + length(Key),
            Rest  = string:sub_string(Str, Start),
            %% read digits
            Digits = lists:takewhile(fun(C) ->
                C >= $0 andalso C =< $9
            end, Rest),
            case Digits of
                [] -> 0;
                D  -> list_to_integer(D)
            end
    end.
