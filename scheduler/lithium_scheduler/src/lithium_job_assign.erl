-module(lithium_job_assign).

-export([assign/1, init_counter/0]).

assign(JobId) ->
    case lithium_job_registry:get_job(JobId) of
        {error, not_found} ->
            {error, job_not_found};
        {ok, Job} ->
            case pick_node() of
                {error, no_nodes} ->
                    io:format("  [assign] No available nodes for ~s~n", [JobId]),
                    {error, no_nodes};
                {ok, Node} ->
                    NodeId = maps:get(node_id, Node),
                    Now    = erlang:system_time(second),
                    lithium_job_registry:update_job_status(JobId, #{
                        status     => assigned,
                        node_id    => NodeId,
                        started_at => Now
                    }),
                    io:format("  [assign] Job ~s → ~s~n", [JobId, NodeId]),
                    spawn(fun() -> dispatch_to_node(Node, JobId, Job) end),
                    {ok, NodeId}
            end
    end.

init_counter() ->
    case ets:info(lithium_rr) of
        undefined ->
            ets:new(lithium_rr, [named_table, public, set]),
            ets:insert(lithium_rr, {counter, 0});
        _ ->
            ok
    end.

get_counter() ->
    init_counter(),
    case ets:lookup(lithium_rr, counter) of
        [{counter, N}] -> N;
        []             -> 0
    end.

bump_counter() ->
    init_counter(),
    ets:update_counter(lithium_rr, counter, 1).

pick_node() ->
    Nodes  = lithium_registry:all_nodes(),
    Active = lists:filter(fun(N) ->
        maps:get(status, N) =:= active
    end, Nodes),
    case Active of
        []    -> {error, no_nodes};
        [One] -> {ok, One};
        _     ->
            Sorted = lists:sort(fun(A, B) ->
                maps:get(score, A) >= maps:get(score, B)
            end, Active),
            MaxScore = maps:get(score, hd(Sorted)),
            TopNodes = lists:filter(fun(N) ->
                maps:get(score, N) =:= MaxScore
            end, Sorted),
            Idx = get_counter() rem length(TopNodes),
            bump_counter(),
            {ok, lists:nth(Idx + 1, TopNodes)}
    end.

dispatch_to_node(Node, JobId, Job) ->
    NodeId   = maps:get(node_id,   Node),
    Ip       = maps:get(ip,        Node, <<"127.0.0.1">>),
    Port     = maps:get(port,      Node, <<"7701">>),
    Runtime  = maps:get(runtime,   Job),
    Command  = maps:get(command,   Job),
    ClientId = maps:get(client_id, Job),

    PortInt = case Port of
        P when is_integer(P) -> P;
        P when is_binary(P)  -> binary_to_integer(P);
        P when is_list(P)    -> list_to_integer(P)
    end,

    IpStr = case Ip of
        I when is_binary(I) -> binary_to_list(I);
        I when is_list(I)   -> I
    end,

    Payload   = list_to_binary(io_lib:format(
        "~s:~s:~s:~s", [JobId, ClientId, Runtime, Command])),
    Signature = lithium_crypto:sign_job(Payload),
    PubKey    = lithium_crypto:get_public_key(),

    Body = list_to_binary(io_lib:format(
        "{\"job_id\":\"~s\",\"client_id\":\"~s\","
        "\"runtime\":\"~s\",\"command\":\"~s\","
        "\"signature\":\"~s\",\"scheduler_pubkey\":\"~s\"}",
        [JobId, ClientId, Runtime, Command, Signature, PubKey])),

    Len     = integer_to_list(byte_size(Body)),
    Request = list_to_binary([
        "POST /job HTTP/1.0\r\n",
        "Host: ", IpStr, ":", integer_to_list(PortInt), "\r\n",
        "Content-Type: application/json\r\n",
        "Content-Length: ", Len, "\r\n",
        "\r\n", Body
    ]),

    io:format("  [dispatch] Signed job ~s → ~s @ ~s:~p~n",
              [JobId, NodeId, IpStr, PortInt]),

    IpTuple = list_to_tuple([list_to_integer(X)
                              || X <- string:tokens(IpStr, ".")]),

    case gen_tcp:connect(IpTuple, PortInt, [binary, {active, false}], 5000) of
        {ok, Sock} ->
            gen_tcp:send(Sock, Request),
            case gen_tcp:recv(Sock, 0, 5000) of
                {ok, Resp} ->
                    case binary:match(Resp, <<"200">>) of
                        nomatch ->
                            io:format("  [dispatch] Node rejected job~n"),
                            lithium_job_registry:update_job_status(JobId,
                                #{status => failed});
                        _ ->
                            io:format("  [dispatch] Node accepted job ~s~n", [JobId])
                    end;
                _ ->
                    io:format("  [dispatch] No response~n"),
                    lithium_job_registry:update_job_status(JobId, #{status => failed})
            end,
            gen_tcp:close(Sock);
        {error, Reason} ->
            io:format("  [dispatch] Cannot reach ~s:~p — ~p~n",
                      [IpStr, PortInt, Reason]),
            lithium_job_registry:update_job_status(JobId, #{status => failed})
    end.
