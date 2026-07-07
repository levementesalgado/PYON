#!/bin/bash
set -e

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; NC='\033[0m'; BOLD='\033[1m'

say()  { echo -e "${CYN}✦${NC} $1"; }
ok()   { echo -e "${GRN}✓${NC} $1"; }
warn() { echo -e "${YLW}!${NC} $1"; }
die()  { echo -e "${RED}✗${NC} $1"; exit 1; }

echo -e "${BOLD}"
cat << 'BANNER'
  ╔════════════════════════════════╗
  ║     pyon — setup v0.1      ║
  ╚════════════════════════════════╝
BANNER
echo -e "${NC}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHARE_DIR="/usr/local/share/pyon"
BIN_DIR="/usr/local/bin"

# ── detecta distro ───────────────────────────────────────────────
detect_pkg_manager() {
    if   command -v slackpkg  &>/dev/null; then echo "slackware"
    elif command -v apt-get   &>/dev/null; then echo "apt"
    elif command -v pacman    &>/dev/null; then echo "pacman"
    elif command -v dnf       &>/dev/null; then echo "dnf"
    elif command -v zypper    &>/dev/null; then echo "zypper"
    else echo "unknown"; fi
}

PKG_MGR=$(detect_pkg_manager)
say "distro detectada: ${BOLD}${PKG_MGR}${NC}"

# ── instala dependências do sistema ─────────────────────────────
install_sys_deps() {
    say "instalando dependências do sistema..."
    case "$PKG_MGR" in
        slackware)
            # Slackware já vem com gcc/make/ncurses
            # ruby pode precisar de SlackBuild ou sbopkg
            if ! command -v ruby &>/dev/null; then
                warn "ruby não encontrado."
                warn "no Slackware, instale via sbopkg:"
                warn "  sbopkg -i ruby"
                warn "ou baixe o SlackBuild em slackbuilds.org/ruby"
                warn "continuando sem ruby (relay desativado)..."
                RUBY_OK=0
            else
                RUBY_OK=1
            fi
            if ! command -v rustc &>/dev/null; then
                warn "rustc não encontrado. instalando via rustup..."
                curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
                source "$HOME/.cargo/env"
            fi
            ;;
        apt)
            sudo apt-get update -qq
            sudo apt-get install -y \
                gcc make libncursesw5-dev \
                ruby ruby-bundler \
                curl build-essential
            if ! command -v rustc &>/dev/null; then
                curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
                source "$HOME/.cargo/env"
            fi
            RUBY_OK=1
            ;;
        pacman)
            sudo pacman -Sy --needed --noconfirm \
                gcc make ncurses ruby rubygems rust cargo
            RUBY_OK=1
            ;;
        dnf)
            sudo dnf install -y \
                gcc make ncurses-devel ruby ruby-devel rust cargo
            RUBY_OK=1
            ;;
        zypper)
            sudo zypper install -y \
                gcc make ncurses-devel ruby rust cargo
            RUBY_OK=1
            ;;
        *)
            warn "gerenciador de pacotes não reconhecido."
            warn "certifique-se de ter: gcc, make, libncursesw, ruby, rustc"
            RUBY_OK=1
            ;;
    esac
    ok "dependências do sistema prontas"
}

RUBY_OK=1
install_sys_deps

# ── garante cargo no PATH ────────────────────────────────────────
if [ -f "$HOME/.cargo/env" ]; then
    source "$HOME/.cargo/env"
fi
command -v cargo &>/dev/null || die "cargo não encontrado após instalação"
command -v gcc   &>/dev/null || die "gcc não encontrado"

# ── compila pyon-core (Rust) ─────────────────────────────────
say "compilando pyon-core (Rust)..."
cd "$SCRIPT_DIR/pyon-core"
cargo build --release 2>&1 | grep -E "^error|Compiling|Finished" || true
ok "core compilado: $SCRIPT_DIR/pyon-core/target/release/pyon"

# ── gera identidade se não existir ──────────────────────────────
IDENTITY="$HOME/.pyon/identity.json"
if [ ! -f "$IDENTITY" ]; then
    say "gerando identidade Ed25519..."
    "$SCRIPT_DIR/pyon-core/target/release/pyon" > /dev/null 2>&1 || true
    if [ -f "$IDENTITY" ]; then
        ACCESS=$(python3 -c "import json,sys; d=json.load(open('$IDENTITY')); print(d['access_code'])" 2>/dev/null \
              || grep -o '"access_code":"[^"]*"' "$IDENTITY" | cut -d'"' -f4)
        ok "identidade criada"
        echo -e "  ${BOLD}código de acesso: ${YLW}${ACCESS}${NC}"
        echo -e "  ${RED}↑ guarde isso! é sua chave de login ↑${NC}"
    fi
else
    ok "identidade já existe em $IDENTITY"
fi

# ── instala gems Ruby ────────────────────────────────────────────
if [ "$RUBY_OK" -eq 1 ]; then
    say "instalando gems Ruby..."
    cd "$SCRIPT_DIR/pyon-srv"
    if command -v bundle &>/dev/null; then
        bundle install --quiet
        ok "gems instaladas"
    else
        gem install ed25519 msgpack async async-io --quiet
        ok "gems instaladas via gem install"
    fi
else
    warn "pulando gems — ruby não disponível"
fi

# ── compila TUI (C) ──────────────────────────────────────────────
say "compilando pyon-tui (C + ncursesw)..."
cd "$SCRIPT_DIR/pyon-tui"

# ajusta caminho do relay_client.rb no relay.c para o dir real
RELAY_CLIENT_PATH="$SCRIPT_DIR/pyon-srv/lib/pyon/relay_client.rb"
sed -i "s|/usr/local/share/pyon|$SCRIPT_DIR/pyon-srv/lib/pyon|g" \
    src/relay.c 2>/dev/null || true

make 2>&1 | grep -E "^error|warning:|gcc" || true
ok "TUI compilada: $SCRIPT_DIR/pyon-tui/pyon-tui"

# ── instala binários ─────────────────────────────────────────────
say "instalando em $BIN_DIR e $SHARE_DIR..."
sudo mkdir -p "$SHARE_DIR"
sudo cp "$SCRIPT_DIR/pyon-tui/pyon-tui" "$BIN_DIR/pyon"
sudo cp "$SCRIPT_DIR/pyon-core/target/release/pyon" "$BIN_DIR/pyon-core"
sudo cp -r "$SCRIPT_DIR/pyon-srv/lib" "$SHARE_DIR/"
sudo cp "$SCRIPT_DIR/pyon-srv/server.rb" "$SHARE_DIR/"
ok "binários instalados"

# ── wrapper para o servidor ──────────────────────────────────────
sudo tee "$BIN_DIR/pyon-srv" > /dev/null << WRAPPER
#!/bin/bash
cd "$SHARE_DIR"
exec \${TORSOCKS:+torsocks} ruby server.rb "\$@"
WRAPPER
sudo chmod +x "$BIN_DIR/pyon-srv"
ok "pyon-srv instalado"

# ── config padrão se não existir ─────────────────────────────────
CFG="$HOME/.pyon/config.json"
if [ ! -f "$CFG" ]; then
    mkdir -p "$HOME/.pyon"
    cat > "$CFG" << 'JSON'
{
  "relay_host": "127.0.0.1",
  "relay_port": 7667
}
JSON
    ok "config criada em $CFG"
fi

# ── resumo ───────────────────────────────────────────────────────
echo
echo -e "${BOLD}══════════════════════════════════════${NC}"
echo -e "${GRN}  tudo pronto! (◕‿◕✿)${NC}"
echo -e "${BOLD}══════════════════════════════════════${NC}"
echo
echo -e "  ${CYN}rodar a TUI:${NC}         ${BOLD}pyon${NC}"
echo -e "  ${CYN}rodar o servidor:${NC}    ${BOLD}pyon-srv${NC}"
echo -e "  ${CYN}com tor:${NC}             ${BOLD}torsocks pyon-srv${NC}"
echo -e "  ${CYN}dados em:${NC}            ${BOLD}~/.pyon/${NC}"
echo
if [ "$PKG_MGR" = "slackware" ] && [ "$RUBY_OK" -eq 0 ]; then
    echo -e "  ${YLW}relay desativado — instale ruby e rode setup.sh novamente${NC}"
    echo
fi
