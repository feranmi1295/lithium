-module(lithium_crypto).

-export([start/0, sign_job/1, get_public_key/0]).

-define(KEY_FILE, "scheduler_keypair.bin").

start() ->
    case filelib:is_file(?KEY_FILE) of
        true ->
            io:format("  Scheduler keypair loaded~n");
        false ->
            %% generate new keypair using crypto
            #{public := Pub, secret := Sec} = generate_keypair(),
            write_keypair(Pub, Sec),
            io:format("  Scheduler keypair generated~n")
    end,
    ok.

generate_keypair() ->
    %% Ed25519 via crypto module
    {Pub, Sec} = crypto:generate_key(eddsa, ed25519),
    #{public => Pub, secret => Sec}.

write_keypair(Pub, Sec) ->
    PubHex = binary:encode_hex(Pub),
    SecHex = binary:encode_hex(Sec),
    file:write_file(?KEY_FILE,
        <<PubHex/binary, "\n", SecHex/binary, "\n">>).

read_keypair() ->
    {ok, Bin} = file:read_file(?KEY_FILE),
    Lines = binary:split(Bin, <<"\n">>, [global, trim]),
    [PubHex, SecHex | _] = Lines,
    Pub = binary:decode_hex(PubHex),
    Sec = binary:decode_hex(SecHex),
    {Pub, Sec}.

get_public_key() ->
    {Pub, _} = read_keypair(),
    binary:encode_hex(Pub).

sign_job(JobPayload) ->
    {_Pub, Sec} = read_keypair(),
    %% sign the payload
    Sig = crypto:sign(eddsa, none, JobPayload, [Sec, ed25519]),
    SigHex = binary:encode_hex(Sig),
    SigHex.
