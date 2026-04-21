#!/bin/bash
# pyon setup — instala dependências, compila e instala
# Suporte: Debian/Ubuntu, Arch, Fedora/RHEL, openSUSE, Slackware, genérico

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; NC='\033[0m'; BOLD='\033[1m'

say()  { echo -e "${CYN}✦${NC} $1"; }
ok()   { echo -e "${GRN}✓${NC} $1"; }
warn() { echo -e "${YLW}!${NC} $1"; }
die()  { echo -e "${RED}✗${NC} $1"; exit 1; }

echo -e "${BOLD}"
cat << 'BANNER'
  ╔══════════════════════════════════════╗
  ║       PYON  —  setup  v0.2-alpha     ║
  ╚══════════════════════════════════════╝
BANNER
echo -e "${NC}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHARE_DIR="/usr/local/share/txtboard"
BIN_DIR="/usr/local/bin"
RUBY_OK=1
RUST_OK=1

# ── detecta distro ──────────────────────────────────────────────
detect_distro() {
    if   [ -f /etc/slackware-version ];         then echo "slackware"
    elif [ -f /etc/arch-release ];              then echo "pacman"
    elif command -v apt-get  &>/dev/null;       then echo "apt"
    elif command -v dnf      &>/dev/null;       then echo "dnf"
    elif command -v zypper   &>/dev/null;       then echo "zypper"
    elif command -v pacman   &>/dev/null;       then echo "pacman"
    else                                             echo "unknown"
    fi
}
PKG_MGR=$(detect_distro)
say "distro detectada: ${BOLD}${PKG_MGR}${NC}"

# ── helpers Slackware ───────────────────────────────────────────
slk_has()  { ls /var/log/packages/${1}-[0-9]* &>/dev/null 2>&1; }
slk_need() {
    local pkg=$1
    if slk_has "$pkg"; then return 0; fi
    if command -v sbopkg &>/dev/null; then
        say "instalando $pkg via sbopkg..."
        sbopkg -B -i "$pkg" 2>&1 | tail -3 || warn "sbopkg falhou para $pkg — continue manualmente"
    else
        warn "$pkg ausente. Com sbopkg: sbopkg -i $pkg"
        warn "Sem sbopkg: https://slackbuilds.org/repository/15.0/${pkg}/"
    fi
}

# ── instala dependências ─────────────────────────────────────────
say "instalando dependências do sistema..."
case "$PKG_MGR" in
    apt)
        sudo apt-get update -qq 2>&1 | tail -1
        sudo apt-get install -y \
            gcc make \
            libncursesw5-dev \
            ruby ruby-bundler ruby-dev \
            curl git 2>&1 | grep -E "^(Inst|E:|erro)" || true
        if ! command -v cargo &>/dev/null; then
            say "instalando Rust via rustup..."
            curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
                | sh -s -- -y --default-toolchain stable --no-modify-path
            source "$HOME/.cargo/env" 2>/dev/null || true
        fi
        ;;
    pacman)
        sudo pacman -Sy --needed --noconfirm \
            gcc make ncurses \
            ruby rubygems ruby-bundler \
            rust cargo curl git 2>&1 | tail -5 || true
        ;;
    dnf)
        sudo dnf install -y \
            gcc make \
            ncurses-devel \
            ruby ruby-devel rubygems \
            curl git 2>&1 | grep -E "^(Install|Error)" || true
        command -v bundle &>/dev/null || gem install bundler --no-document 2>/dev/null || true
        if ! command -v cargo &>/dev/null; then
            say "instalando Rust via rustup..."
            curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
                | sh -s -- -y --default-toolchain stable --no-modify-path
            source "$HOME/.cargo/env" 2>/dev/null || true
        fi
        ;;
    zypper)
        sudo zypper install -y --no-confirm \
            gcc make \
            ncurses-devel \
            ruby ruby-devel \
            curl git 2>&1 | grep -E "^(Installing|Error)" || true
        command -v bundle &>/dev/null || gem install bundler --no-document 2>/dev/null || true
        if ! command -v cargo &>/dev/null; then
            say "instalando Rust via rustup..."
            curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
                | sh -s -- -y --default-toolchain stable --no-modify-path
            source "$HOME/.cargo/env" 2>/dev/null || true
        fi
        ;;
    slackware)
        # Slackware base já tem gcc, make, ncurses (com headers)
        say "Slackware: verificando pacotes base..."
        for pkg in gcc make; do
            command -v "$pkg" &>/dev/null || warn "$pkg ausente no PATH"
        done
        # ncurses.h no Slackware fica em /usr/include/ncurses/
        NCU_INC=""
        for d in /usr/include/ncurses /usr/include/ncursesw /usr/include; do
            [ -f "$d/ncurses.h" ] && NCU_INC="$d" && break
        done
        if [ -n "$NCU_INC" ]; then
            say "ncurses headers em $NCU_INC"
            export EXTRA_CFLAGS="-I$NCU_INC"
        else
            warn "ncurses.h não encontrado em /usr/include — tentando slackpkg..."
            command -v slackpkg &>/dev/null && sudo slackpkg install ncurses 2>/dev/null || true
        fi
        if ! command -v ruby &>/dev/null; then
            slk_need "ruby"
            command -v ruby &>/dev/null && RUBY_OK=1 || RUBY_OK=0
        fi
        if ! command -v cargo &>/dev/null; then
            slk_need "rust"
            if ! command -v cargo &>/dev/null; then
                say "tentando rustup como fallback..."
                curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
                    | sh -s -- -y --default-toolchain stable --no-modify-path 2>&1 | tail -3 || true
                source "$HOME/.cargo/env" 2>/dev/null || true
            fi
            command -v cargo &>/dev/null && RUST_OK=1 || RUST_OK=0
        fi
        command -v torsocks &>/dev/null || warn "torsocks ausente (opcional para .onion): sbopkg -i torsocks"
        ;;
    *)
        warn "gerenciador de pacotes não reconhecido."
        warn "instale manualmente: gcc make libncursesw-dev ruby cargo"
        ;;
esac

# garante cargo no PATH
[ -f "$HOME/.cargo/env" ] && source "$HOME/.cargo/env" 2>/dev/null || true
command -v cargo &>/dev/null || { warn "cargo não encontrado — pulando core Rust"; RUST_OK=0; }
command -v gcc   &>/dev/null || die "gcc não encontrado — impossível compilar TUI"
command -v ruby  &>/dev/null || { warn "ruby não encontrado — relay desativado"; RUBY_OK=0; }
ok "dependências verificadas"

# ── txtboard-core (Rust) ────────────────────────────────────────
if [ "$RUST_OK" -eq 1 ]; then
    say "compilando pyon-core (Rust)..."
    cd "$SCRIPT_DIR/txtboard-core"
    cargo build --release 2>&1 | grep -E "^error|Compiling txtboard|Finished" || true
    if [ -f "target/release/txtboard" ]; then
        ok "pyon-core compilado"
    else
        warn "falha ao compilar pyon-core — continuando sem ele"
        RUST_OK=0
    fi

    IDENTITY="$HOME/.txtboard/identity.json"
    if [ ! -f "$IDENTITY" ]; then
        say "gerando identidade Ed25519..."
        mkdir -p "$HOME/.txtboard"
        "$SCRIPT_DIR/txtboard-core/target/release/txtboard" >/dev/null 2>&1 || true
        if [ -f "$IDENTITY" ]; then
            ACCESS=$(python3 -c "import json; print(json.load(open('$IDENTITY'))['access_code'])" 2>/dev/null \
                  || grep -o '"access_code":"[^"]*"' "$IDENTITY" | cut -d'"' -f4 || echo "?")
            ok "identidade criada"
            echo
            echo -e "  ${BOLD}código de acesso:${NC} ${YLW}${ACCESS}${NC}"
            echo -e "  ${RED}↑ ANOTE! é sua chave de recuperação ↑${NC}"
            echo
        fi
    else
        ok "identidade já existe"
    fi
fi

# ── gems Ruby ───────────────────────────────────────────────────
if [ "$RUBY_OK" -eq 1 ]; then
    say "instalando gems Ruby..."
    cd "$SCRIPT_DIR/txtboard-srv"
    if command -v bundle &>/dev/null; then
        bundle install --quiet 2>&1 | grep -v "^Using\|^Bundle\|^Fetching" || true
    else
        for gem in ed25519 msgpack json; do
            ruby -e "require '$gem'" 2>/dev/null || \
                gem install "$gem" --no-document 2>&1 | grep -E "Successfully|ERROR" || true
        done
    fi
    ok "gems prontas"
fi

# ── TUI C ───────────────────────────────────────────────────────
say "compilando pyon-tui (C + ncurses)..."
cd "$SCRIPT_DIR/txtboard-tui"

# Detecta flags de ncurses via pkg-config ou busca manual
NCURSES_CFLAGS=""
NCURSES_LIBS=""
if command -v pkg-config &>/dev/null; then
    if   pkg-config --exists ncursesw 2>/dev/null; then
        NCURSES_CFLAGS=$(pkg-config --cflags ncursesw)
        NCURSES_LIBS=$(pkg-config --libs ncursesw)
        say "ncursesw encontrado via pkg-config"
    elif pkg-config --exists ncurses 2>/dev/null; then
        NCURSES_CFLAGS=$(pkg-config --cflags ncurses)
        NCURSES_LIBS=$(pkg-config --libs ncurses)
        say "ncurses encontrado via pkg-config"
    fi
fi

# Tenta compilar com flags detectadas, depois fallbacks
compile_tui() {
    local cflags="$1" ldflags="$2"
    make clean 2>/dev/null || true
    make EXTRA_CFLAGS="$cflags" LDFLAGS="$ldflags" 2>&1 | grep -E "^error|warning:.*error|undefined" || true
    [ -f "pyon-tui" ]
}

if [ -n "$NCURSES_LIBS" ] && compile_tui "$NCURSES_CFLAGS" "$NCURSES_LIBS"; then
    ok "pyon-tui compilado (pkg-config)"
elif compile_tui "" "-lncursesw"; then
    ok "pyon-tui compilado (ncursesw)"
elif compile_tui "" "-lncurses"; then
    ok "pyon-tui compilado (ncurses fallback)"
elif compile_tui "-I/usr/include/ncursesw" "-lncursesw"; then
    ok "pyon-tui compilado (path explícito)"
else
    make 2>&1 | tail -12
    die "falha ao compilar pyon-tui — verifique que libncursesw-dev está instalado"
fi

# ── instala binários ─────────────────────────────────────────────
say "instalando em $BIN_DIR..."
sudo mkdir -p "$SHARE_DIR/lib/txtboard" "$BIN_DIR"

# remove versões antigas
sudo rm -f "$BIN_DIR/pyon" "$BIN_DIR/pyon-core" "$BIN_DIR/pyon-srv"
sudo rm -f "$BIN_DIR/txtboard" "$BIN_DIR/txtboard-core" "$BIN_DIR/txtboard-srv"

# TUI
sudo install -m755 "$SCRIPT_DIR/txtboard-tui/pyon-tui" "$BIN_DIR/pyon"

# core Rust
if [ "$RUST_OK" -eq 1 ] && [ -f "$SCRIPT_DIR/txtboard-core/target/release/txtboard" ]; then
    sudo install -m755 "$SCRIPT_DIR/txtboard-core/target/release/txtboard" "$BIN_DIR/pyon-core"
fi

# arquivos Ruby
if [ "$RUBY_OK" -eq 1 ]; then
    sudo cp -r "$SCRIPT_DIR/txtboard-srv/lib/txtboard/"* "$SHARE_DIR/lib/txtboard/"
    sudo cp "$SCRIPT_DIR/txtboard-srv/server.rb" "$SHARE_DIR/"

    sudo tee "$BIN_DIR/pyon-srv" > /dev/null << WRAPPER
#!/bin/bash
cd "$SHARE_DIR"
exec \${TORSOCKS:+torsocks} ruby server.rb "\$@"
WRAPPER
    sudo chmod +x "$BIN_DIR/pyon-srv"
fi

ok "instalado"

# ── config padrão ────────────────────────────────────────────────
CFG="$HOME/.txtboard/config.json"
if [ ! -f "$CFG" ]; then
    mkdir -p "$HOME/.txtboard"
    printf '{\n  "relay_host": "127.0.0.1",\n  "relay_port": 7667\n}\n' > "$CFG"
    ok "config criada em $CFG"
fi

# ── resumo ───────────────────────────────────────────────────────
echo
echo -e "${BOLD}══════════════════════════════════════${NC}"
echo -e "${GRN}  pronto! (◕‿◕✿)${NC}"
echo -e "${BOLD}══════════════════════════════════════${NC}"
echo
echo -e "  ${CYN}iniciar pyon:${NC}        ${BOLD}pyon${NC}"
[ "$RUBY_OK" -eq 1 ] && \
echo -e "  ${CYN}servidor relay:${NC}      ${BOLD}pyon-srv${NC}"
[ "$RUBY_OK" -eq 1 ] && \
echo -e "  ${CYN}relay com tor:${NC}       ${BOLD}torsocks pyon-srv${NC}"
echo -e "  ${CYN}dados:${NC}               ${BOLD}~/.txtboard/${NC}"
echo
echo -e "  ${CYN}teclas home:${NC}         ${BOLD}[↑↓] nav  [Enter] abrir  [/] buscar  [s] nome  [q] sair${NC}"
echo -e "  ${CYN}teclas board:${NC}        ${BOLD}[↑↓] nav  [Enter] thread  [n] post  [r] relay${NC}"
echo -e "  ${CYN}teclas relay:${NC}        ${BOLD}[Tab] sidebar  [/dm nick] DM direto  [ESC] sair${NC}"
echo

if [ "$PKG_MGR" = "slackware" ]; then
    echo -e "  ${YLW}notas Slackware:${NC}"
    [ "$RUBY_OK"  -eq 0 ] && echo -e "    ruby:     ${BOLD}sbopkg -i ruby${NC}"
    [ "$RUST_OK"  -eq 0 ] && echo -e "    rust:     ${BOLD}sbopkg -i rust${NC}  ou  ${BOLD}curl ... rustup${NC}"
    ! command -v torsocks &>/dev/null && \
    echo -e "    torsocks: ${BOLD}sbopkg -i torsocks${NC} (opcional)"
    echo
fi
