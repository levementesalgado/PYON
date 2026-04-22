# txtboard-srv

Servidor Ruby — relay TCP + protocolo de sync entre nós.

## setup

```bash
bundle install
# rode o core Rust primeiro para gerar ~/.txtboard/identity.json
./txtboard-core/target/release/txtboard
```

## rodar

```bash
# direto (dev)
ruby server.rb

# com torsocks (produção)
torsocks ruby server.rb --relay-port 7667

# debug
ruby server.rb --debug
```

## arquivos

```
lib/txtboard/
  protocol.rb      framing MessagePack + verificação Ed25519
  identity.rb      lê identity.json do core Rust
  ban_list.rb      lê/escreve bans.ndjson
  sync.rb          protocolo announce→request→data de posts
  relay_server.rb  servidor TCP de relay chat (Async)
  relay_client.rb  cliente TCP para a TUI C (stdin/stdout JSON)
server.rb          entry point
```

## protocolo de sync

```
Nó A                          Nó B
post_announce ──────────────▶  (tem? não)
              ◀────────────── post_request
post_data ──────────────────▶  verifica sig → salva
```

## relay chat — integração com a TUI C

A TUI spawna `relay_client.rb` como processo filho e se comunica via pipe:

```c
// C — spawn
popen("torsocks ruby relay_client.rb 127.0.0.1 7667 geral", "rw");

// enviar mensagem
fprintf(pipe, "{\"type\":\"send\",\"body\":\"oi~\"}\n");

// receber
fgets(buf, sizeof(buf), pipe);
// {"type":"msg","from":"yumeiri","body":"oi!","ts":1234,"dm":false}
```

## variáveis de ambiente

| var | descrição |
|-----|-----------|
| `TXTBOARD_ADMIN_PUBKEY` | pubkey hex do admin (você) para ban_broadcast |
