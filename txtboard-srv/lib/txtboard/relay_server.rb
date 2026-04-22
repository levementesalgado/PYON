# txtboard/relay_server.rb
# Relay TCP + sync de posts integrado.
# Cada conexão é autenticada via Ed25519.
# Posts recebidos via post_data são persistidos em db/posts.ndjson
# e anunciados (post_announce) a todos os outros peers do canal.

require "socket"
require "thread"
require "json"
require_relative "protocol"
require_relative "ban_list"
require_relative "identity"

module Txtboard
  class RelayServer
    def initialize(host: "127.0.0.1", port: 7667,
                   ban_list: nil, identity: nil, db_dir: nil)
      @host      = host
      @port      = port
      @ban_list  = ban_list || BanList.new
      @identity  = identity || Identity.new
      @clients   = {}        # pubkey => { io:, name:, channel: }
      @mu        = Mutex.new
      @db_dir    = db_dir || File.join(Dir.home, ".txtboard", "db")
      FileUtils.mkdir_p(@db_dir) rescue nil
      # garante que o arquivo existe para evitar race na primeira postagem
      FileUtils.touch(File.join(@db_dir, "posts.ndjson")) rescue nil
    end

    def run
      client_count = 0
      $logger&.info("relay: #{@host}:#{@port}")
      $logger&.info("db: #{@db_dir}")
      $logger&.info("nó: #{@identity.pubkey_hex[0,16]}…")
      server = TCPServer.new(@host, @port)
      $logger&.info("aguardando conexões…")
      loop do
        begin
          sock = server.accept
          peer = sock.peeraddr(false)[3] rescue "?"
          client_count += 1
          $logger&.info("TCP: nova conexão de #{peer} (total: #{client_count})")
          Thread.new(sock) { |s| handle_client(s) }
        rescue => e
          $logger&.error("accept: #{e.message}")
        end
      end
    end
    private

    # ── conexão ──────────────────────────────────────────────────

    def handle_client(io)
      io.setsockopt(Socket::IPPROTO_TCP, Socket::TCP_NODELAY, 1) rescue nil
      peer = io.peeraddr(false)[3] rescue "?"

      frame = read_frame(io)
      unless frame
        $logger&.warn("handshake: conexão de #{peer} fechou sem enviar nada")
        return
      end
      unless frame.type == "handshake"
        $logger&.warn("handshake: tipo inesperado '#{frame.type}' de #{peer}")
        send_frame(io, "handshake_ack", { "ok" => "false", "reason" => "esperava handshake" })
        return
      end

      pubkey  = frame.payload["pubkey"].to_s
      channel = frame.payload["channel"] || "geral"
      name    = frame.payload["name"]    || "anon:#{pubkey[0,8]}"

      $logger&.info("handshake: #{name} (#{pubkey[0,12]}…) → ##{channel} de #{peer}")

      begin
        Protocol.verify!(frame)
      rescue Protocol::SignatureError => e
        $logger&.warn("handshake: assinatura inválida de #{name} (#{pubkey[0,12]}…): #{e.message}")
        send_frame(io, "handshake_ack", { "ok" => "false", "reason" => e.message })
        return
      end

      if @ban_list.banned?(pubkey)
        $logger&.warn("handshake: acesso negado — #{name} (#{pubkey[0,12]}…) está banide")
        send_frame(io, "handshake_ack", { "ok" => "false", "reason" => "*zap!!* banide (˘︹˘)" })
        return
      end

      send_signed(io, "handshake_ack", {
        "ok" => "true", "channel" => channel,
        "motd" => "*kyaa~!* bem-vinde, #{name}! (◕‿◕✿)"
      })

      @mu.synchronize { @clients[pubkey] = { io: io, name: name, channel: channel } }
      online = @mu.synchronize { @clients.count { |_, c| c[:channel] == channel } }
      $logger&.info("[##{channel}] #{name} conectou (#{online} online no canal)")
      broadcast_sys(channel, "*pyon!* #{name} entrou! (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧", except: pubkey)

      # anuncia posts existentes da board ao recém-chegado
      announce_board_to(io, channel, pubkey)

      begin
        loop do
          frame = read_frame(io) or break
          process(frame, from_pubkey: pubkey, from_name: name,
                  channel: channel, io: io)
        end
      rescue => e
        $logger&.warn("[##{channel}] #{name} desconectou com erro: #{e.message}")
      ensure
        @mu.synchronize { @clients.delete(pubkey) }
        remaining = @mu.synchronize { @clients.count { |_, c| c[:channel] == channel } }
        $logger&.info("[##{channel}] #{name} saiu (#{remaining} online no canal)")
        broadcast_sys(channel, "*plop...* #{name} saiu. (｡•́︿•̀｡)", except: pubkey)
        io.close rescue nil
      end
    end

    # ── processamento de frames ───────────────────────────────────

    def process(frame, from_pubkey:, from_name:, channel:, io:)
      begin
        Protocol.verify!(frame) if frame.payload["pubkey"]
      rescue Protocol::SignatureError => e
        $logger&.warn("sig inválida de #{from_name} (#{from_pubkey[0,12]}…): #{e.message}")
        return
      end
      return if @ban_list.banned?(from_pubkey)

      $logger&.debug("frame: #{frame.type} de #{from_name} em ##{channel}")

      case frame.type
      when "ping"
        $logger&.debug("ping de #{from_name}")
        send_signed(io, "pong", { "ts" => Time.now.utc.to_i.to_s })

      when "chat_message"
        body = frame.payload["body"].to_s.strip
        if body.empty?
          $logger&.debug("chat_message vazia de #{from_name} — ignorada")
          return
        end
        if body.bytesize > 2048
          $logger&.warn("chat_message de #{from_name} muito grande (#{body.bytesize} bytes) — ignorada")
          return
        end
        online = @mu.synchronize { @clients.count { |_, c| c[:channel] == channel } }
        $logger&.info("[##{channel}] #{from_name}: #{body[0,80]}#{body.length > 80 ? "…" : ""} (broadcast para #{online-1} peers)")
        broadcast(channel, "chat_message", {
          "from_pubkey" => from_pubkey, "from_name" => from_name,
          "body" => body, "ts" => Time.now.utc.to_i.to_s
        }, except: from_pubkey)

      when "chat_dm"
        to = frame.payload["to_pubkey"].to_s
        body = frame.payload["body"].to_s.strip
        $logger&.info("DM: #{from_name} → #{to[0,12]}… (#{body.bytesize} bytes)")
        deliver_dm(from_pubkey: from_pubkey, from_name: from_name,
                   to_pubkey: to, body: body)

      # ── sync de posts ─────────────────────────────────────────

      when "post_announce"
        board   = frame.payload["board"].to_s
        post_id = frame.payload["post_id"].to_s
        ndjson_id = "#{board}:#{post_id}"

        if post_exists?(ndjson_id)
          $logger&.debug("sync: #{ndjson_id} já existe — ignorando anúncio")
        else
          $logger&.info("sync: #{from_name} anuncia #{ndjson_id} — solicitando…")
          req = Protocol.build_signed("post_request",
            { "board" => board, "post_id" => post_id }, @identity)
          io.write(Protocol.encode(req)) rescue nil
        end

      when "post_request"
        board   = frame.payload["board"].to_s
        post_id = frame.payload["post_id"].to_s
        post    = load_post("#{board}:#{post_id}")
        if post
          $logger&.info("sync: enviando #{board}:#{post_id} para #{from_name}")
          send_signed(io, "post_data", post_payload(post))
        else
          $logger&.warn("sync: #{from_name} pediu #{board}:#{post_id} — não encontrado")
        end

      when "post_data"
        board   = frame.payload["board"].to_s
        post_id = frame.payload["post_id"].to_s
        $logger&.info("sync: recebendo post_data #{board}:#{post_id} de #{from_name}")
        receive_post(frame.payload, from_pubkey: from_pubkey,
                     channel: channel, sender_io: io)

      else
        $logger&.warn("frame desconhecido: '#{frame.type}' de #{from_name} — ignorado")
      end
    end

    # ── persistência de posts ─────────────────────────────────────

    def posts_path
      File.join(@db_dir, "posts.ndjson")
    end

    def post_exists?(ndjson_id)
      return false unless File.exist?(posts_path)
      needle = "\"_id\":\"#{ndjson_id}\""
      File.foreach(posts_path).any? { |l| l.include?(needle) }
    end

    def load_post(ndjson_id)
      return nil unless File.exist?(posts_path)
      needle = "\"_id\":\"#{ndjson_id}\""
      File.foreach(posts_path) do |line|
        next unless line.include?(needle)
        return JSON.parse(line) rescue nil
      end
      nil
    end

    def save_post(record)
      lock_path = posts_path + ".lock"
      File.open(lock_path, File::CREAT | File::RDWR) do |lf|
        lf.flock(File::LOCK_EX)
        unless post_exists?(record["_id"])
          File.open(posts_path, "a") { |f| f.puts(record.to_json) }
          $logger&.info("post salvo: #{record["_id"]} (autor: #{record["author_name"]})")
        else
          $logger&.debug("post duplicado ignorado: #{record["_id"]}")
        end
        lf.flock(File::LOCK_UN)
      end
    end

    def next_post_id(board)
      return 1 unless File.exist?(posts_path)
      prefix = "\"board\":\"#{board}\""
      max = 0
      File.foreach(posts_path) do |line|
        next unless line.include?(prefix)
        rec = JSON.parse(line) rescue next
        id  = rec["id"].to_i
        max = id if id > max
      end
      max + 1
    end

    def post_payload(post)
      {
        "board"       => post["board"].to_s,
        "post_id"     => post["id"].to_s,
        "body"        => post["body"].to_s,
        "subject"     => post["subject"].to_s,
        "reply_to"    => post["reply_to"].to_s,
        "images"      => (post["images"] || []).to_json,
        "author"      => post["author_pubkey"].to_s,
        "author_name" => post["author_name"].to_s,
        "created_at"  => post["created_at"].to_s,
        "orig_sig"    => post["sig"].to_s,
      }
    end

    # Recebe post_data, valida assinatura do autor, persiste e faz broadcast.
    def receive_post(payload, from_pubkey:, channel:, sender_io:)
      board    = payload["board"].to_s
      post_id  = payload["post_id"].to_s
      ndjson_id = "#{board}:#{post_id}"

      return if post_exists?(ndjson_id)

      # valida assinatura original do autor
      author_key = payload["author"].to_s
      orig_sig   = payload["orig_sig"].to_s
      body       = payload["body"].to_s
      subject    = payload["subject"].to_s

      canonical = "#{board}|#{post_id}|#{body}|#{subject}"
      unless verify_author_sig(author_key, orig_sig, canonical)
        $logger&.warn("sync: assinatura inválida do autor em #{ndjson_id} (autor: #{author_key[0,12]}…) — descartado")
        return
      end

      record = {
        "_id"          => ndjson_id,
        "board"        => board,
        "id"           => post_id.to_i,
        "author_pubkey"=> author_key,
        "author_name"  => payload["author_name"].to_s,
        "subject"      => subject,
        "body"         => body,
        "reply_to"     => payload["reply_to"].to_i,
        "images"       => (JSON.parse(payload["images"].to_s) rescue []),
        "sig"          => orig_sig,
        "created_at"   => payload["created_at"].to_s,
        "received_at"  => Time.now.utc.iso8601,
        "received_from"=> from_pubkey,
      }

      save_post(record)

      # re-anuncia para os outros peers (exceto quem enviou)
      announce = Protocol.build_signed("post_announce", {
        "board"   => board,
        "post_id" => post_id,
        "ts"      => Time.now.utc.to_i.to_s
      }, @identity)

      targets = @mu.synchronize do
        @clients.reject { |pk, c| pk == from_pubkey || c[:channel] != channel }.values
      end
      $logger&.info("sync: re-anunciando #{ndjson_id} para #{targets.size} peers no canal ##{channel}")
      targets.each { |c| c[:io].write(Protocol.encode(announce)) rescue nil }

      # notifica via chat que um post novo chegou
      broadcast_sys(channel,
        "*ding~!* novo post em /#{board}/#{post_id} (◕‿◕✿)",
        except: from_pubkey)
    end

    # Ao conectar, anuncia posts existentes da board ao novo peer.
    def announce_board_to(io, channel, pubkey)
      board = channel.sub(/^#/, "")
      return unless File.exist?(posts_path)
      prefix = "\"board\":\"#{board}\""
      count = 0
      File.foreach(posts_path) do |line|
        next unless line.include?(prefix)
        rec = JSON.parse(line) rescue next
        ann = Protocol.build_signed("post_announce", {
          "board"   => board,
          "post_id" => rec["id"].to_s,
          "ts"      => rec["created_at"].to_s
        }, @identity)
        io.write(Protocol.encode(ann)) rescue nil
        count += 1
      end
      $logger&.info("sync: anunciados #{count} posts da board /#{board}/ ao novo peer #{pubkey[0,12]}…")
    rescue => e
      $logger&.error("announce_board_to: #{e.message}")
    end

    # ── roteamento de chat ────────────────────────────────────────

    def broadcast(channel, type, payload, except: nil)
      targets = @mu.synchronize do
        @clients.select { |pk, c| c[:channel] == channel && pk != except }.values
      end
      # fire-and-forget: cada envio em thread separada para não bloquear
      targets.each do |c|
        Thread.new { send_signed(c[:io], type, payload) rescue nil }
      end
    end

    def broadcast_sys(channel, text, except: nil)
      broadcast(channel, "chat_message", {
        "from_pubkey" => @identity.pubkey_hex,
        "from_name"   => "✦sistema✦",
        "body"        => text,
        "ts"          => Time.now.utc.to_i.to_s
      }, except: except)
    end

    def deliver_dm(from_pubkey:, from_name:, to_pubkey:, body:)
      return if body.empty? || to_pubkey.empty?
      target = @mu.synchronize { @clients[to_pubkey] }
      src    = @mu.synchronize { @clients[from_pubkey] }
      unless target
        $logger&.warn("DM: destino #{to_pubkey[0,12]}… offline — notificando #{from_name}")
        send_signed(src[:io], "chat_message", {
          "from_pubkey" => @identity.pubkey_hex, "from_name" => "✦sistema✦",
          "body" => "*pip...* #{to_pubkey[0,12]} offline. (｡•́︿•̀｡)",
          "ts"   => Time.now.utc.to_i.to_s
        }) rescue nil
        return
      end
      $logger&.info("DM entregue: #{from_name} → #{target[:name]} (#{body.bytesize} bytes)")
      send_signed(target[:io], "chat_dm", {
        "from_pubkey" => from_pubkey, "from_name" => from_name,
        "body" => body, "ts" => Time.now.utc.to_i.to_s
      }) rescue nil
      send_signed(src[:io], "chat_message", {
        "from_pubkey" => @identity.pubkey_hex, "from_name" => "✦sistema✦",
        "body" => "*piiing!* DM para #{target[:name]}! ♡",
        "ts"   => Time.now.utc.to_i.to_s
      }) rescue nil
    end

    # ── crypto helpers ────────────────────────────────────────────

    def verify_author_sig(pubkey_hex, sig_hex, msg)
      return true if pubkey_hex.empty? || sig_hex.empty?
      Identity.verify(pubkey_hex, sig_hex, msg.b)
    rescue => e
      $logger&.warn("verify_author_sig falhou (#{pubkey_hex[0,12]}…): #{e.message}")
      false
    end

    # ── I/O helpers ───────────────────────────────────────────────

    def read_frame(io)
      raw = Protocol.read_frame(io)
      Protocol.parse(raw)
    rescue EOFError, IOError, Protocol::ProtocolError
      nil
    end

    def send_frame(io, type, payload)
      io.write(Protocol.encode(Protocol.build(type, payload)))
    end

    def send_signed(io, type, payload)
      io.write(Protocol.encode(Protocol.build_signed(type, payload, @identity)))
    end
  end
end
