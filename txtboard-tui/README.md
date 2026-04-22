# txtboard-tui

Interface TUI em C com ncursesw.

## deps

```bash
# debian/ubuntu
sudo apt install libncursesw5-dev gcc make

# arch
sudo pacman -S ncurses gcc make
```

## compilar

```bash
make && ./txtboard-tui
```

## integração com o servidor Ruby

O relay.c faz `fork+exec` do `relay_client.rb` e se comunica via pipe.
Coloque o `relay_client.rb` em `/usr/local/share/txtboard/` ou ajuste
o caminho em `relay.c` (linha `snprintf(cmd, ...)`).

```bash
# estrutura esperada
/usr/local/share/txtboard/relay_client.rb

# ou rode apontando para o fonte diretamente:
# edite relay.c: troque o caminho por ./txtboard-srv/lib/txtboard/relay_client.rb
```

## config (~/.txtboard/config.json)

```json
{
  "relay_host": "127.0.0.1",
  "relay_port": 7667
}
```

A identidade é lida automaticamente de `~/.txtboard/identity.json`
(gerado pelo `txtboard-core` em Rust).

## controles

**home**
| tecla | ação |
|-------|------|
| ↑↓ / jk | navegar |
| número + Enter | ir direto (ex: `3` → /a/) |
| r | relay global |
| q | sair |

**board**
| tecla | ação |
|-------|------|
| ↑↓ / jk | navegar posts |
| Enter | abrir/fechar thread |
| n | novo post |
| r | relay do canal |
| ESC/q | voltar |

**relay**
| tecla | ação |
|-------|------|
| Enter | enviar mensagem |
| PgUp/Dn | scroll |
| ESC | voltar |
