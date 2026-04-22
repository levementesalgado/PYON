# txtboard/relay_client.rb
#
# Cliente de relay. Usado pela TUI (C) via processo filho:
#   torsocks ruby relay_client.rb <host> <port> <channel>
#
# A TUI se comunica com este processo por stdin/stdout (linhas JSON).
# Protocolo interno TUI↔cliente:
#   stdin  → { "type": "send",  "body": "..." }
#             { "type": "dm",   "to": "<pubkey>", "body": "..." }
#             { "type": "quit" }
#   stdout ← { "type": "msg",  "from": "nome", "body": "...", "ts": 123, "dm": false }
#             { "type": "sys",  "body": "..." }
#             { "type": "users","list": [...] }
#             { "type": "error","msg": "..." }

require "socket"
require "json"
require "thread"
require "fileutils"
require "ed25519"
require_relative "protocol"
require_relative "identity"

module Txtboard
  class RelayClient
    def initialize(host:, port:, channel:, identity: nil)
      @host     = host
      @port     = port
      @channel  = channel
      @identity = identity || Identity.new
    end

    def run
      begin
        @socket = TCPSocket.new(@host, @port)
      rescue Errno::ECONNREFUSED, Errno::ETIMEDOUT, Errno::EHOSTUNREACH => e
        emit("error", { "msg" => "Connection refused: #{e.message}" })
        return
      end

      # handshake
      hs = Protocol.build_signed("handshake", {
        "channel" => @channel,
        "name"    => @identity.display,
        "ts"      => Time.now.utc.to_i.to_s
      }, @identity)
      @socket.write(Protocol.encode(hs))

      ack_raw  = Protocol.read_frame(@socket)
      ack      = Protocol.parse(ack_raw)
      unless ack.payload["ok"] == "true"
        emit("error", { "msg" => ack.payload["reason"] || "handshake recusado" })
        return
      end
      emit("sys", { "body" => ack.payload["motd"] || "conectado" })
      # avisa a TUI que o próprio usuário entrou (para adicionar à lista de online)
      emit("join", {
        "from"   => @identity.display,
        "pubkey" => @identity.pubkey_hex
      })

      reader = Thread.new { read_loop }
      write_loop

      reader.kill rescue nil
      @socket.close rescue nil
    rescue => e
      emit("error", { "msg" => "erro inesperado: #{e.message}" })
    end

    private

    def read_loop
      loop do
        raw   = Protocol.read_frame(@socket) or break
        frame = Protocol.parse(raw)

        case frame.type
        when "chat_message"
          p = frame.payload
          emit("msg", {
            "from"   => p["from_name"] || "anon",
            "pubkey" => p["from_pubkey"] || "",
            "body"   => p["body"],
            "ts"     => p["ts"].to_i,
            "dm"     => false,
            "sys"    => p["from_name"] == "✦sistema✦"
          })

        when "chat_dm"
          p = frame.payload
          emit("msg", {
            "from"   => p["from_name"] || "anon",
            "pubkey" => p["from_pubkey"] || "",
            "body"   => p["body"],
            "ts"     => p["ts"].to_i,
            "dm"     => true,
            "sys"    => false
          })

        when "post_announce"
          # servidor anuncia novo post — solicita o conteúdo
          p = frame.payload
          board   = p["board"].to_s
          post_id = p["post_id"].to_s
          req = Protocol.build_signed("post_request", {
            "board"   => board,
            "post_id" => post_id
          }, @identity)
          @socket.write(Protocol.encode(req)) rescue nil

        when "post_data"
          # recebeu o post completo — persiste localmente e notifica a TUI
          p = frame.payload
          board   = p["board"].to_s
          post_id = p["post_id"].to_s
          save_post_locally(p)
          emit("post_new", {
            "board"   => board,
            "post_id" => post_id
          })

        when "pong"
          # silencioso
        end
      end
    rescue => e
      emit("error", { "msg" => "desconectado: #{e.message}" })
    end

    def write_loop
      loop do
        line = $stdin.gets or break
        cmd  = JSON.parse(line.strip) rescue next

        case cmd["type"]
        when "send"
          body = cmd["body"].to_s.strip
          next if body.empty?
          frame = Protocol.build_signed("chat_message", {
            "body" => body,
            "ts"   => Time.now.utc.to_i.to_s
          }, @identity)
          @socket.write(Protocol.encode(frame))

        when "post"
          # TUI criou um post localmente — anuncia para o servidor
          # cmd: { "type":"post", "board":"a", "post_id":"482" }
          frame = Protocol.build_signed("post_announce", {
            "board"   => cmd["board"].to_s,
            "post_id" => cmd["post_id"].to_s,
            "ts"      => Time.now.utc.to_i.to_s
          }, @identity)
          @socket.write(Protocol.encode(frame))

        when "dm"
          frame = Protocol.build_signed("chat_dm", {
            "to_pubkey" => cmd["to"].to_s,
            "body"      => cmd["body"].to_s,
            "ts"        => Time.now.utc.to_i.to_s
          }, @identity)
          @socket.write(Protocol.encode(frame))

        when "ping"
          frame = Protocol.build_signed("ping", {
            "ts" => Time.now.utc.to_i.to_s
          }, @identity)
          @socket.write(Protocol.encode(frame))

        when "quit"
          break
        end
      end
    end

    def emit(type, data)
      $stdout.puts({ "type" => type }.merge(data).to_json)
      $stdout.flush
    end

    # Persiste post_data recebido da rede no NDJSON local.
    # Verifica assinatura do autor antes de salvar.
    def save_post_locally(payload)
      board    = payload["board"].to_s
      post_id  = payload["post_id"].to_s
      body     = payload["body"].to_s
      subject  = payload["subject"].to_s
      author   = payload["author"].to_s
      orig_sig = payload["orig_sig"].to_s

      # verifica assinatura canônica: "board|id|body|subject"
      canonical = "#{board}|#{post_id}|#{body}|#{subject}".b
      unless verify_sig(author, orig_sig, canonical)
        emit("error", { "msg" => "post_data: assinatura inválida de #{author[0,12]}" })
        return
      end

      db_dir = File.join(Dir.home, ".txtboard", "db")
      FileUtils.mkdir_p(db_dir)
      path = File.join(db_dir, "posts.ndjson")
      ndjson_id = "#{board}:#{post_id}"

      # não duplica
      return if File.exist?(path) &&
                File.foreach(path).any? { |l| l.include?("\"_id\":\"#{ndjson_id}\"") }

      record = {
        "_id"           => ndjson_id,
        "board"         => board,
        "id"            => post_id.to_i,
        "author_pubkey" => author,
        "author_name"   => payload["author_name"].to_s,
        "subject"       => subject,
        "body"          => body,
        "reply_to"      => payload["reply_to"].to_i,
        "images"        => (JSON.parse(payload["images"].to_s) rescue []),
        "sig"           => orig_sig,
        "created_at"    => payload["created_at"].to_s,
        "received_at"   => Time.now.utc.iso8601,
      }

      # flock para evitar race com outros processos
      File.open(path + ".lock", File::CREAT | File::RDWR) do |lf|
        lf.flock(File::LOCK_EX)
        File.open(path, "a") { |f| f.puts(record.to_json) }
        lf.flock(File::LOCK_UN)
      end
    rescue => e
      emit("error", { "msg" => "save_post: #{e.message}" })
    end

    def verify_sig(pubkey_hex, sig_hex, msg)
      return true if pubkey_hex.empty? || sig_hex.empty?
      pk  = Ed25519::VerifyKey.new([pubkey_hex].pack("H*"))
      sig = [sig_hex].pack("H*")
      pk.verify(sig, msg)
      true
    rescue
      false
    end
  end
end

# ── entry point CLI ──────────────────────────────────────────────
if __FILE__ == $0
  host         = ARGV[0] || "127.0.0.1"
  port         = (ARGV[1] || 7667).to_i
  channel      = ARGV[2] || "geral"
  display_name = ARGV[3]  # nome escolhido pelo utilizador (opcional)

  begin
    identity = Txtboard::Identity.new
    # sobrescreve display_name se passado pela TUI
    identity.instance_variable_set(:@display_name, display_name) if display_name && !display_name.empty?
  rescue => e
    $stderr.puts "erro ao carregar identidade: #{e.message}"
    exit 1
  end

  client = Txtboard::RelayClient.new(host: host, port: port, channel: channel,
                                     identity: identity)
  client.run
end
