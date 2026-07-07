#!/bin/bash
# pyon setup — instala dependências, compila e instala
# Suporte: Debian/Ubuntu, Arch, Fedora/RHEL, openSUSE, Slackware, genérico

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; NC='\033[0m'; BOLD='\033[1m'

say()  { echo -e "${CYN}✦${NC} $1"; }
ok()   { echo -e "${GRN}✓${NC} $1"; }
warn() { echo -e "${YLW}!${NC} $1"; }
die()  { echo -e "${RED}✗${NC} $1"; exit 1; }
ask()  { echo -e "${CYN}?${NC} $1"; }

echo -e "${BOLD}"
cat << 'BANNER'
  ╔══════════════════════════════════════╗
  ║       PYON  —  setup  v0.2-alpha     ║
  ╚══════════════════════════════════════╝
BANNER
echo -e "${NC}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHARE_DIR="/usr/local/share/pyon"
BIN_DIR="/usr/local/bin"
RUBY_OK=1
RUST_OK=1

# ── flags de controle ───────────────────────────────────────────
FORCE_REBUILD=0
SKIP_DEPS=0
for arg in "$@"; do
    case "$arg" in
        --rebuild)    FORCE_REBUILD=1 ;;
        --skip-deps)  SKIP_DEPS=1 ;;
    esac
done

# ── detecta se já há binários instalados ────────────────────────
PYON_EXISTS=0
CORE_EXISTS=0
[ -x "$BIN_DIR/pyon" ]      && PYON_EXISTS=1
[ -x "$BIN_DIR/pyon-core" ] && CORE_EXISTS=1

if [ "$PYON_EXISTS" -eq 1 ] && [ "$CORE_EXISTS" -eq 1 ] && [ "$FORCE_REBUILD" -eq 0 ]; then
    # binários já estão em /usr/local/bin — pula recompilação
    # mas se os fontes compilados não existem, precisa recompilar para copiar
    if [ -f "$SCRIPT_DIR/pyon-tui/pyon-tui" ] && \
       [ -f "$SCRIPT_DIR/pyon-core/target/release/pyon" ]; then
        say "binários compilados encontrados — pulando recompilação (--rebuild para forçar)"
        SKIP_COMPILE=1
    else
        say "binários instalados mas fontes não compilados — recompilando..."
        SKIP_COMPILE=0
    fi
else
    SKIP_COMPILE=0
fi

# ── detecta distro ──────────────────────────────────────────────
detect_distro() {
    if   [ -f /etc/slackware-version ];   then echo "slackware"
    elif [ -f /etc/arch-release ];        then echo "pacman"
    elif command -v apt-get  &>/dev/null; then echo "apt"
    elif command -v dnf      &>/dev/null; then echo "dnf"
    elif command -v zypper   &>/dev/null; then echo "zypper"
    elif command -v pacman   &>/dev/null; then echo "pacman"
    else                                       echo "unknown"
    fi
}
PKG_MGR=$(detect_distro)
say "distro: ${BOLD}${PKG_MGR}${NC}"

# ── helpers Slackware ───────────────────────────────────────────
slk_has()  { ls /var/log/packages/${1}-[0-9]* &>/dev/null 2>&1; }
slk_need() {
    local pkg=$1
    if slk_has "$pkg"; then return 0; fi
    if command -v sbopkg &>/dev/null; then
        say "instalando $pkg via sbopkg..."
        sbopkg -B -i "$pkg" 2>&1 | tail -3 || warn "sbopkg falhou para $pkg"
    else
        warn "$pkg ausente. Com sbopkg: sbopkg -i $pkg"
        warn "Sem sbopkg: https://slackbuilds.org/repository/15.0/${pkg}/"
    fi
}

# ── instala dependências do sistema ─────────────────────────────
if [ "$SKIP_DEPS" -eq 0 ] && [ "$SKIP_COMPILE" -eq 0 ]; then
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
            fi ;;
        pacman)
            sudo pacman -Sy --needed --noconfirm \
                gcc make ncurses \
                ruby rubygems ruby-bundler \
                rust cargo curl git 2>&1 | tail -5 || true ;;
        dnf)
            sudo dnf install -y \
                gcc make ncurses-devel \
                ruby ruby-devel rubygems \
                curl git 2>&1 | grep -E "^(Install|Error)" || true
            command -v bundle &>/dev/null || gem install bundler --no-document 2>/dev/null || true
            if ! command -v cargo &>/dev/null; then
                say "instalando Rust via rustup..."
                curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
                    | sh -s -- -y --default-toolchain stable --no-modify-path
                source "$HOME/.cargo/env" 2>/dev/null || true
            fi ;;
        zypper)
            sudo zypper install -y --no-confirm \
                gcc make ncurses-devel \
                ruby ruby-devel curl git 2>&1 | grep -E "^(Installing|Error)" || true
            command -v bundle &>/dev/null || gem install bundler --no-document 2>/dev/null || true
            if ! command -v cargo &>/dev/null; then
                say "instalando Rust via rustup..."
                curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
                    | sh -s -- -y --default-toolchain stable --no-modify-path
                source "$HOME/.cargo/env" 2>/dev/null || true
            fi ;;
        slackware)
            say "Slackware: verificando pacotes base..."
            for pkg in gcc make; do
                command -v "$pkg" &>/dev/null || warn "$pkg ausente no PATH"
            done
            NCU_INC=""
            for d in /usr/include/ncurses /usr/include/ncursesw /usr/include; do
                [ -f "$d/ncurses.h" ] && NCU_INC="$d" && break
            done
            if [ -n "$NCU_INC" ]; then
                say "ncurses headers em $NCU_INC"
                export EXTRA_CFLAGS="-I$NCU_INC"
            else
                warn "ncurses.h não encontrado — tentando slackpkg..."
                command -v slackpkg &>/dev/null && sudo slackpkg install ncurses 2>/dev/null || true
            fi
            command -v ruby  &>/dev/null || { slk_need "ruby";  command -v ruby  &>/dev/null && RUBY_OK=1 || RUBY_OK=0; }
            command -v cargo &>/dev/null || {
                slk_need "rust"
                command -v cargo &>/dev/null || {
                    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
                        | sh -s -- -y --default-toolchain stable --no-modify-path 2>&1 | tail -3 || true
                    source "$HOME/.cargo/env" 2>/dev/null || true
                }
                command -v cargo &>/dev/null && RUST_OK=1 || RUST_OK=0
            }
            command -v torsocks &>/dev/null || warn "torsocks ausente (opcional): sbopkg -i torsocks" ;;
        *)
            warn "gerenciador não reconhecido — instale: gcc make libncursesw-dev ruby cargo" ;;
    esac
else
    say "pulando instalação de dependências (binários já existem)"
fi

# garante cargo no PATH
[ -f "$HOME/.cargo/env" ] && source "$HOME/.cargo/env" 2>/dev/null || true

# verifica ferramentas mínimas
if [ "$SKIP_COMPILE" -eq 0 ] || [ "$FORCE_REBUILD" -eq 1 ]; then
    command -v cargo &>/dev/null || { warn "cargo não encontrado — pulando core Rust"; RUST_OK=0; }
    command -v gcc   &>/dev/null || die "gcc não encontrado"
    command -v ruby  &>/dev/null || { warn "ruby não encontrado — relay desativado"; RUBY_OK=0; }
fi
ok "dependências verificadas"

# ── identidade: gerar nova ou importar ──────────────────────────
IDENTITY="$HOME/.pyon/identity.json"
mkdir -p "$HOME/.pyon/db"

if [ ! -f "$IDENTITY" ]; then
    echo
    echo -e "${BOLD}  ── identidade ──────────────────────────────────${NC}"
    echo -e "  Você não tem uma identidade pyon ainda."
    echo
    echo -e "  ${BOLD}[1]${NC} Criar nova identidade"
    echo -e "  ${BOLD}[2]${NC} Importar chave existente (secret_hex)"
    echo
    ask "Escolha [1/2, padrão=1]: "
    read -r ID_CHOICE </dev/tty || ID_CHOICE="1"
    ID_CHOICE="${ID_CHOICE:-1}"

    if [ "$ID_CHOICE" = "2" ]; then
        echo
        echo -e "  Cole o seu ${BOLD}secret_hex${NC} (64 chars hex, de identity.json):"
        ask "> "
        read -r SECRET_HEX </dev/tty || SECRET_HEX=""
        SECRET_HEX=$(echo "$SECRET_HEX" | tr -d '[:space:]')

        if [ -z "$SECRET_HEX" ]; then
            warn "nenhuma chave fornecida — criando nova identidade"
            ID_CHOICE="1"
        elif [ ${#SECRET_HEX} -ne 64 ]; then
            warn "chave inválida (esperado 64 chars hex, recebido ${#SECRET_HEX}) — criando nova"
            ID_CHOICE="1"
        else
            say "restaurando identidade..."
            CORE_BIN=""
            [ -f "$SCRIPT_DIR/pyon-core/target/release/pyon" ] && \
                CORE_BIN="$SCRIPT_DIR/pyon-core/target/release/pyon"
            [ -z "$CORE_BIN" ] && command -v pyon-core &>/dev/null && CORE_BIN="pyon-core"

            if [ -n "$CORE_BIN" ]; then
                if "$CORE_BIN" restore "$SECRET_HEX"; then
                    ACCESS=$(python3 -c "import json; print(json.load(open('$IDENTITY'))['access_code'])" 2>/dev/null \
                          || grep -o '"access_code":"[^"]*"' "$IDENTITY" | cut -d'"' -f4 || echo "?")
                    ok "identidade importada  |  acesso: ${YLW}${ACCESS}${NC}"
                else
                    warn "falha ao importar — criando nova identidade"
                    ID_CHOICE="1"
                fi
            else
                # pyon-core ainda não compilado — escreve diretamente o JSON mínimo
                # será substituído quando o core compilar
                warn "pyon-core não disponível ainda — salvando chave, identidade gerada após compilação"
                printf '{"secret_hex":"%s","pubkey_hex":"","access_code":"pendente","display_name":null}' \
                    "$SECRET_HEX" > "$IDENTITY"
                IMPORT_PENDING=1
            fi
        fi
    fi

    if [ "${ID_CHOICE}" = "1" ] && [ "${IMPORT_PENDING:-0}" -eq 0 ]; then
        say "identidade será gerada ao compilar o core..."
    fi
else
    ACCESS=$(python3 -c "import json; print(json.load(open('$IDENTITY'))['access_code'])" 2>/dev/null \
          || grep -o '"access_code":"[^"]*"' "$IDENTITY" | cut -d'"' -f4 || echo "?")
    ok "identidade já existe  |  acesso: ${YLW}${ACCESS}${NC}"

    # oferece opção de substituir
    echo
    echo -e "  Deseja importar uma chave diferente? ${BOLD}[s/N]${NC}: "
    ask "> "
    read -r REPLACE </dev/tty || REPLACE="n"
    if [ "${REPLACE,,}" = "s" ] || [ "${REPLACE,,}" = "y" ]; then
        echo -e "  Cole o ${BOLD}secret_hex${NC} (64 chars hex):"
        ask "> "
        read -r SECRET_HEX </dev/tty || SECRET_HEX=""
        SECRET_HEX=$(echo "$SECRET_HEX" | tr -d '[:space:]')
        if [ ${#SECRET_HEX} -eq 64 ]; then
            CORE_BIN=""
            [ -f "$SCRIPT_DIR/pyon-core/target/release/pyon" ] && \
                CORE_BIN="$SCRIPT_DIR/pyon-core/target/release/pyon"
            [ -z "$CORE_BIN" ] && command -v pyon-core &>/dev/null && CORE_BIN="pyon-core"
            if [ -n "$CORE_BIN" ]; then
                "$CORE_BIN" restore "$SECRET_HEX" && ok "chave substituída" || warn "falha ao substituir"
            else
                warn "pyon-core não disponível — não é possível validar a chave agora"
            fi
        else
            warn "chave inválida, mantendo identidade atual"
        fi
    fi
fi

# ── pyon-core (Rust) ────────────────────────────────────────
if [ "$RUST_OK" -eq 1 ] && { [ "$FORCE_REBUILD" -eq 1 ] || [ "$SKIP_COMPILE" -eq 0 ]; }; then
    say "compilando pyon-core (Rust)..."
    cd "$SCRIPT_DIR/pyon-core"
    cargo build --release 2>&1 | grep -E "^error|Compiling pyon|Finished" || true
    if [ -f "target/release/pyon" ]; then
        ok "pyon-core compilado"
        # se havia importação pendente, finaliza agora
        if [ "${IMPORT_PENDING:-0}" -eq 1 ] && [ -n "$SECRET_HEX" ]; then
            say "finalizando importação de chave..."
            "$SCRIPT_DIR/pyon-core/target/release/pyon" restore "$SECRET_HEX" && \
                ok "identidade importada com sucesso" || warn "falha ao finalizar importação"
        elif [ ! -f "$IDENTITY" ] || grep -q '"pubkey_hex":""' "$IDENTITY" 2>/dev/null; then
            say "gerando identidade Ed25519..."
            "$SCRIPT_DIR/pyon-core/target/release/pyon" >/dev/null 2>&1 || true
            if [ -f "$IDENTITY" ]; then
                ACCESS=$(python3 -c "import json; print(json.load(open('$IDENTITY'))['access_code'])" 2>/dev/null \
                      || grep -o '"access_code":"[^"]*"' "$IDENTITY" | cut -d'"' -f4 || echo "?")
                ok "identidade criada"
                echo
                echo -e "  ${BOLD}código de acesso:${NC} ${YLW}${ACCESS}${NC}"
                echo -e "  ${RED}↑ ANOTE! é sua chave de recuperação ↑${NC}"
                echo
            fi
        fi
    else
        warn "falha ao compilar pyon-core"
        RUST_OK=0
    fi
elif [ "$SKIP_COMPILE" -eq 1 ] && [ "$FORCE_REBUILD" -eq 0 ]; then
    ok "pyon-core já instalado — pulando compilação"
fi

# ── gems Ruby ───────────────────────────────────────────────────
if [ "$RUBY_OK" -eq 1 ]; then
    # verifica se gems já estão instaladas
    GEMS_OK=1
    for gem in ed25519 msgpack json; do
        ruby -e "require '$gem'" 2>/dev/null || { GEMS_OK=0; break; }
    done

    if [ "$GEMS_OK" -eq 0 ] || [ "$FORCE_REBUILD" -eq 1 ]; then
        say "instalando gems Ruby..."
        cd "$SCRIPT_DIR/pyon-srv"
        if command -v bundle &>/dev/null; then
            bundle install --quiet 2>&1 | grep -v "^Using\|^Bundle\|^Fetching" || true
        else
            for gem in ed25519 msgpack json; do
                ruby -e "require '$gem'" 2>/dev/null || \
                    gem install "$gem" --no-document 2>&1 | grep -E "Successfully|ERROR" || true
            done
        fi
        ok "gems prontas"
    else
        ok "gems Ruby já instaladas — pulando"
    fi
fi

# ── TUI C ───────────────────────────────────────────────────────
if [ "$FORCE_REBUILD" -eq 1 ] || [ "$SKIP_COMPILE" -eq 0 ]; then
    say "compilando pyon-tui (C + ncurses)..."
    cd "$SCRIPT_DIR/pyon-tui"

    NCURSES_CFLAGS="${EXTRA_CFLAGS:-}"
    NCURSES_LIBS=""
    if command -v pkg-config &>/dev/null; then
        if   pkg-config --exists ncursesw 2>/dev/null; then
            NCURSES_CFLAGS="$(pkg-config --cflags ncursesw) ${EXTRA_CFLAGS:-}"
            NCURSES_LIBS=$(pkg-config --libs ncursesw)
        elif pkg-config --exists ncurses 2>/dev/null; then
            NCURSES_CFLAGS="$(pkg-config --cflags ncurses) ${EXTRA_CFLAGS:-}"
            NCURSES_LIBS=$(pkg-config --libs ncurses)
        fi
    fi

    compile_tui() {
        local cflags="$1" ldflags="$2"
        make clean 2>/dev/null || true
        make EXTRA_CFLAGS="$cflags" LDFLAGS="$ldflags" 2>&1 | \
            grep -E "^error|undefined reference|cannot find" || true
        [ -f "pyon-tui" ]
    }

    if   [ -n "$NCURSES_LIBS" ] && compile_tui "$NCURSES_CFLAGS" "$NCURSES_LIBS"; then
        ok "pyon-tui compilado (pkg-config)"
    elif compile_tui "${EXTRA_CFLAGS:-}" "-lncursesw"; then
        ok "pyon-tui compilado (ncursesw)"
    elif compile_tui "${EXTRA_CFLAGS:-}" "-lncurses"; then
        ok "pyon-tui compilado (ncurses fallback)"
    elif compile_tui "-I/usr/include/ncursesw ${EXTRA_CFLAGS:-}" "-lncursesw"; then
        ok "pyon-tui compilado (path explícito)"
    else
        make 2>&1 | tail -15
        die "falha ao compilar pyon-tui — verifique que libncursesw-dev está instalado"
    fi
else
    ok "pyon-tui já instalado — pulando compilação"
fi

# ── instala binários ─────────────────────────────────────────────
say "instalando em $BIN_DIR..."
sudo mkdir -p "$SHARE_DIR/lib/pyon" "$BIN_DIR"

sudo rm -f "$BIN_DIR/pyon" "$BIN_DIR/pyon-core" "$BIN_DIR/pyon-srv"
sudo rm -f "$BIN_DIR/pyon" "$BIN_DIR/pyon-core" "$BIN_DIR/pyon-srv"

# TUI — usa o recém-compilado ou o que já estava na pasta src
TUI_BIN=""
[ -f "$SCRIPT_DIR/pyon-tui/pyon-tui" ] && TUI_BIN="$SCRIPT_DIR/pyon-tui/pyon-tui"
if [ -n "$TUI_BIN" ]; then
    sudo install -m755 "$TUI_BIN" "$BIN_DIR/pyon"
    ok "pyon instalado"
else
    warn "pyon-tui não encontrado — não instalado"
fi

# core Rust
CORE_BIN=""
[ -f "$SCRIPT_DIR/pyon-core/target/release/pyon" ] && \
    CORE_BIN="$SCRIPT_DIR/pyon-core/target/release/pyon"
if [ -n "$CORE_BIN" ]; then
    sudo install -m755 "$CORE_BIN" "$BIN_DIR/pyon-core"
    ok "pyon-core instalado"
fi

# arquivos Ruby — SEMPRE atualiza (relay_client.rb, server.rb, etc.)
if [ "$RUBY_OK" -eq 1 ]; then
    sudo cp -r "$SCRIPT_DIR/pyon-srv/lib/pyon/"* "$SHARE_DIR/lib/pyon/"
    sudo cp "$SCRIPT_DIR/pyon-srv/server.rb" "$SHARE_DIR/"
    sudo tee "$BIN_DIR/pyon-srv" > /dev/null << WRAPPER
#!/bin/bash
cd "$SHARE_DIR"
exec \${TORSOCKS:+torsocks} ruby server.rb "\$@"
WRAPPER
    sudo chmod +x "$BIN_DIR/pyon-srv"
    ok "arquivos Ruby atualizados"
fi

ok "instalação concluída"

# ── config padrão ────────────────────────────────────────────────
CFG="$HOME/.pyon/config.json"
if [ ! -f "$CFG" ]; then
    mkdir -p "$HOME/.pyon"
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
[ "$RUBY_OK" -eq 1 ] && echo -e "  ${CYN}servidor relay:${NC}      ${BOLD}pyon-srv${NC}"
[ "$RUBY_OK" -eq 1 ] && echo -e "  ${CYN}relay com tor:${NC}       ${BOLD}torsocks pyon-srv${NC}"
echo -e "  ${CYN}dados:${NC}               ${BOLD}~/.pyon/${NC}"
echo -e "  ${CYN}importar chave:${NC}      ${BOLD}pyon-core restore <secret_hex>${NC}"
echo
echo -e "  ${CYN}teclas home:${NC}  [↑↓] nav  [Enter] abrir  [/] buscar  [s] nome  [q] sair"
echo -e "  ${CYN}teclas board:${NC} [↑↓] nav  [Enter] thread  [n] post  [r] relay"
echo -e "  ${CYN}relay:${NC}        [Tab] sidebar  [/dm nick] DM  [ESC] sair"
echo

if [ "$PKG_MGR" = "slackware" ]; then
    echo -e "  ${YLW}notas Slackware:${NC}"
    [ "$RUBY_OK"  -eq 0 ] && echo -e "    ruby:     ${BOLD}sbopkg -i ruby${NC}"
    [ "$RUST_OK"  -eq 0 ] && echo -e "    rust:     ${BOLD}sbopkg -i rust${NC}"
    ! command -v torsocks &>/dev/null && \
        echo -e "    torsocks: ${BOLD}sbopkg -i torsocks${NC} (opcional)"
    echo
fi

if [ "$FORCE_REBUILD" -eq 0 ] && [ "$SKIP_COMPILE" -eq 1 ]; then
    echo -e "  ${CYN}dica:${NC} para recompilar: ${BOLD}./setup.sh --rebuild${NC}"
    echo
fi
