-module(lithium_job_registry).

-export([start/0, create_job/3, get_job/1, update_job_status/2,
         all_jobs/0, pending_jobs/0]).

start() ->
    ets:new(lithium_jobs, [named_table, public, set,
                           {keypos, 1}, {read_concurrency, true}]),
    Jobs = lithium_db:load_jobs(),
    lists:foreach(fun(Job) ->
        JobId = maps:get(job_id, Job),
        ets:insert(lithium_jobs, {JobId, Job})
    end, Jobs),
    io:format("  Job registry initialized (~p jobs restored)~n", [length(Jobs)]).

create_job(ClientId, Runtime, Command) ->
    JobId = generate_job_id(),
    Now   = erlang:system_time(second),
    Job = #{
        job_id     => JobId,
        client_id  => ClientId,
        runtime    => Runtime,
        command    => Command,
        status     => pending,
        node_id    => none,
        created_at => Now,
        started_at => none,
        result     => none
    },
    ets:insert(lithium_jobs, {JobId, Job}),
    lithium_db:save_job(JobId, Job),
    io:format("  [jobs] Created: ~s~n", [JobId]),
    {ok, JobId}.

get_job(JobId) ->
    case ets:lookup(lithium_jobs, JobId) of
        [{JobId, Job}] -> {ok, Job};
        []             -> {error, not_found}
    end.

update_job_status(JobId, Updates) ->
    case ets:lookup(lithium_jobs, JobId) of
        [{JobId, Job}] ->
            Updated = maps:merge(Job, Updates),
            ets:insert(lithium_jobs, {JobId, Updated}),
            lithium_db:save_job(JobId, Updated),
            {ok, updated};
        [] ->
            {error, not_found}
    end.

all_jobs() ->
    ets:foldl(fun({_Id, Job}, Acc) -> [Job | Acc] end, [], lithium_jobs).

pending_jobs() ->
    lists:filter(fun(J) -> maps:get(status, J) =:= pending end, all_jobs()).

generate_job_id() ->
    <<A:32, B:16, C:16>> = crypto:strong_rand_bytes(8),
    list_to_binary(io_lib:format("JOB-~8.16.0B-~4.16.0B-~4.16.0B", [A, B, C])).
