-module(lithium_job_handler).
-export([init/2]).

init(Req0, State) ->
    Method = cowboy_req:method(Req0),
    Path   = cowboy_req:path(Req0),
    handle(Method, Path, Req0, State).

%% POST /jobs — submit a new job
handle(<<"POST">>, <<"/jobs">>, Req0, State) ->
    {ok, Body, Req1} = cowboy_req:read_body(Req0),

    ClientId = get_field(Body, <<"client_id">>, <<"anonymous">>),
    Runtime  = get_field(Body, <<"runtime">>,   <<"shell">>),
    Command  = get_field(Body, <<"command">>,   <<"echo hello">>),

    {ok, JobId} = lithium_job_registry:create_job(ClientId, Runtime, Command),
    AssignedTo  = case lithium_job_assign:assign(JobId) of
        {ok, N}           -> N;
        {error, no_nodes} -> <<"none">>
    end,

    RespBody = list_to_binary(io_lib:format(
        "{\"status\":\"ok\",\"job_id\":\"~s\",\"assigned_to\":\"~s\"}",
        [JobId, AssignedTo])),
    reply(200, RespBody, Req1, State);

%% GET /jobs — list all jobs
handle(<<"GET">>, <<"/jobs">>, Req, State) ->
    Jobs  = lithium_job_registry:all_jobs(),
    Lines = lists:map(fun(Job) -> format_job(Job) end, Jobs),
    Joined = string:join(Lines, ","),
    reply(200, list_to_binary("[" ++ Joined ++ "]"), Req, State);

%% GET /jobs/JOB-XXXX — single job by ID
handle(<<"GET">>, Path, Req, State) ->
    %% extract job ID from path — everything after /jobs/
    case Path of
        <<"/jobs/", JobId/binary>> ->
            case lithium_job_registry:get_job(JobId) of
                {ok, Job} ->
                    reply(200, list_to_binary(format_job(Job)), Req, State);
                {error, not_found} ->
                    reply(404, <<"{\"error\":\"job not found\"}">>, Req, State)
            end;
        _ ->
            reply(404, <<"{\"error\":\"not found\"}">>, Req, State)
    end;

handle(_, _, Req, State) ->
    reply(405, <<"{\"error\":\"method not allowed\"}">>, Req, State).

format_job(Job) ->
    JobId   = maps:get(job_id,   Job),
    Status  = maps:get(status,   Job),
    NodeId  = maps:get(node_id,  Job),
    Command = maps:get(command,  Job),
    Result  = maps:get(result,   Job),
    ResultStr = case Result of
        none -> <<"null">>;
        R    -> list_to_binary(io_lib:format("\"~s\"", [R]))
    end,
    lists:flatten(io_lib:format(
        "{\"job_id\":\"~s\",\"status\":\"~s\","
        "\"node_id\":\"~s\",\"command\":\"~s\",\"result\":~s}",
        [JobId, Status, NodeId, Command, ResultStr])).

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
