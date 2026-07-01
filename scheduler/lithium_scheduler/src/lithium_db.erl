-module(lithium_db).

-export([start/0, save_node/2, load_nodes/0, save_job/2, load_jobs/0]).

-define(DB_FILE, "lithium.db").

start() ->
    {ok, Db} = esqlite3:open(?DB_FILE),
    esqlite3:exec("CREATE TABLE IF NOT EXISTS nodes (
        node_id     TEXT PRIMARY KEY,
        public_key  TEXT,
        registered_at INTEGER,
        last_seen   INTEGER,
        status      TEXT,
        score       INTEGER,
        missed_pings INTEGER
    );", Db),
    esqlite3:exec("CREATE TABLE IF NOT EXISTS jobs (
        job_id      TEXT PRIMARY KEY,
        client_id   TEXT,
        runtime     TEXT,
        command     TEXT,
        status      TEXT,
        node_id     TEXT,
        created_at  INTEGER,
        result      TEXT
    );", Db),
    esqlite3:close(Db),
    io:format("  Database initialized~n"),
    ok.

save_node(NodeId, Node) ->
    {ok, Db} = esqlite3:open(?DB_FILE),
    PubKey    = maps:get(public_key,    Node, <<"">>),
    RegAt     = maps:get(registered_at, Node, 0),
    LastSeen  = maps:get(last_seen,     Node, 0),
    Status    = atom_to_list(maps:get(status, Node, active)),
    Score     = maps:get(score,         Node, 100),
    Missed    = maps:get(missed_pings,  Node, 0),
    esqlite3:exec(io_lib:format(
        "INSERT OR REPLACE INTO nodes VALUES ('~s','~s',~p,~p,'~s',~p,~p);",
        [NodeId, PubKey, RegAt, LastSeen, Status, Score, Missed]), Db),
    esqlite3:close(Db).

load_nodes() ->
    {ok, Db} = esqlite3:open(?DB_FILE),
    Rows = esqlite3:q("SELECT node_id,public_key,registered_at,
                              last_seen,status,score,missed_pings
                       FROM nodes;", Db),
    esqlite3:close(Db),
    lists:map(fun({NodeId, PubKey, RegAt, LastSeen, Status, Score, Missed}) ->
        #{node_id       => NodeId,
          public_key    => PubKey,
          registered_at => RegAt,
          last_seen     => LastSeen,
          status        => list_to_atom(binary_to_list(Status)),
          score         => Score,
          missed_pings  => Missed,
          jobs_completed => 0}
    end, Rows).

save_job(JobId, Job) ->
    {ok, Db} = esqlite3:open(?DB_FILE),
    ClientId  = maps:get(client_id,  Job, <<"">>),
    Runtime   = maps:get(runtime,    Job, <<"">>),
    Command   = maps:get(command,    Job, <<"">>),
    Status    = atom_to_list(maps:get(status, Job, pending)),
    NodeId    = case maps:get(node_id, Job, none) of
        none -> "none";
        N    -> N
    end,
    CreatedAt = maps:get(created_at, Job, 0),
    Result    = case maps:get(result, Job, none) of
        none -> "null";
        R    -> binary_to_list(R)
    end,
    esqlite3:exec(io_lib:format(
        "INSERT OR REPLACE INTO jobs VALUES ('~s','~s','~s','~s','~s','~s',~p,'~s');",
        [JobId, ClientId, Runtime, Command, Status, NodeId, CreatedAt, Result]), Db),
    esqlite3:close(Db).

load_jobs() ->
    {ok, Db} = esqlite3:open(?DB_FILE),
    Rows = esqlite3:q("SELECT job_id,client_id,runtime,command,
                              status,node_id,created_at,result
                       FROM jobs;", Db),
    esqlite3:close(Db),
    lists:map(fun({JobId, ClientId, Runtime, Command,
                   Status, NodeId, CreatedAt, Result}) ->
        #{job_id     => JobId,
          client_id  => ClientId,
          runtime    => Runtime,
          command    => Command,
          status     => list_to_atom(binary_to_list(Status)),
          node_id    => NodeId,
          created_at => CreatedAt,
          started_at => none,
          result     => case Result of
                            <<"null">> -> none;
                            R          -> R
                        end}
    end, Rows).
