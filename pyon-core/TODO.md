# PYON — TODO

## 🔴 Alto
- [ ] **Canonical signature mismatch** — board.rs usa "board|id|body|subject", relay_server.rb usa "board|post_id|body|subject", sync.rb NÃO inclui subject
- [ ] **C TUI: command injection** — system() em board_view.c, config.c, relay.c com input do usuário
- [ ] **store.rs: race condition** — flock depois de open, delete() truncate enquanto leitores ativos

## 🟡 Médio
- [ ] **store.rs: exists() é O(n)** — linear scan em vez de índice
- [ ] **identity.rs: signing_key() faz unwrap** — panic em identity.json corrompido
- [ ] **relay_server.rb: threads ilimitadas** — sem limite de conexões
- [ ] **ban.rs: is_banned() carrega tabela inteira** — usar HashSet

## 🔵 Baixo
- [ ] **relay.c: post_queue circular** — drop silencioso quando cheio
- [ ] **chat.rs: módulo compilado mas nunca usado** — lib.rs exporta mas main.rs não usa
- [ ] **protocol.rb: pipe `|` não escapado** — injection em valores
- [ ] **setup.sh: curl | sh sem verificação de hash**
- [ ] **Testes** — teste de sync entre pyon-core e pyon-srv
