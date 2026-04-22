# txtboard/sync.rb
#
# Protocolo de sync de posts entre nós.
#
# Fluxo:
#   Nó A (tem post novo)          Nó B (quer o post)
#   ──────────────────────────────────────────────────
#   post_announce ──────────────▶
#                                 verifica se já tem
#                   ◀────────── post_request
#   post_data ──────────────────▶
#                                 verifica sig → salva
#
# Imagens seguem fluxo similar com chunks de 64 KB.

require_relative "protocol"
require_relative "identity"
require "json"

module Txtboard
  module Sync

    CHUNK_SIZE = 64 * 1024  # 64 KB por chunk de imagem

    # ── lado emissor ─────────────────────────────────────────────

    # Anuncia um post para um peer (io = socket já conectado).
    def self.announce_post(io, board:, post_id:, identity:)
      frame = Protocol.build_signed("post_announce", {
        "board"   => board,
        "post_id" => post_id.to_s,
        "ts"      => Time.now.utc.to_i.to_s
      }, identity)
      io.write(Protocol.encode(frame))
    end

    # Envia payload completo de um post em resposta a post_request.
    def self.send_post(io, post_json:, identity:)
      post = JSON.parse(post_json)
      frame = Protocol.build_signed("post_data", {
        "board"      => post["board"],
        "post_id"    => post["id"].to_s,
        "body"       => post["body"],
        "subject"    => post["subject"].to_s,
        "reply_to"   => post["reply_to"].to_s,
        "images"     => post["images"].to_json,
        "author"     => post["author_pubkey"],
        "author_name"=> post["author_name"].to_s,
        "created_at" => post["created_at"].to_s,
        "orig_sig"   => post["sig"]          # assinatura original do autor
      }, identity)
      io.write(Protocol.encode(frame))
    end

    # Envia imagem em chunks.
    def self.send_image(io, board:, post_id:, filename:, data:, identity:)
      chunk_index = 0
      data.bytes.each_slice(CHUNK_SIZE) do |chunk|
        bytes = chunk.pack("C*")
        frame = Protocol.build_signed("image_chunk", {
          "board"    => board,
          "post_id"  => post_id.to_s,
          "filename" => filename,
          "index"    => chunk_index.to_s,
          "data_hex" => bytes.unpack1("H*")
        }, identity)
        io.write(Protocol.encode(frame))
        chunk_index += 1
      end

      frame = Protocol.build_signed("image_end", {
        "board"    => board,
        "post_id"  => post_id.to_s,
        "filename" => filename,
        "chunks"   => chunk_index.to_s
      }, identity)
      io.write(Protocol.encode(frame))
    end

    # ── lado receptor ─────────────────────────────────────────────

    # Processa um frame de sync recebido.
    # store_cb   : lambda { |type, data| } — persiste no store local
    # request_cb : lambda { |board, post_id| } — requisita post ao remetente
    # have_cb    : lambda { |board, post_id| bool } — verifica se já tem o post
    # ban_list   : BanList
    def self.handle(frame, io:, identity:, ban_list:,
                    have_cb:, request_cb:, store_cb:,
                    image_chunks: {})

      # verifica assinatura em todos os frames com pubkey
      if frame.payload["pubkey"]
        begin
          Protocol.verify!(frame)
        rescue Protocol::SignatureError => e
          $logger&.warn("sync: #{e.message} — descartado")
          return
        end

        # descarta conteúdo de banidos
        sender = frame.payload["pubkey"]
        if ban_list.banned?(sender)
          $logger&.info("sync: frame de banido #{sender[0, 12]} descartado")
          return
        end
      end

      case frame.type
      when "post_announce"
        board   = frame.payload["board"]
        post_id = frame.payload["post_id"]
        unless have_cb.call(board, post_id)
          req = Protocol.build_signed("post_request", {
            "board"   => board,
            "post_id" => post_id
          }, identity)
          io.write(Protocol.encode(req))
        end

      when "post_data"
        p = frame.payload
        # verifica assinatura original do autor (não do relay)
        orig_sig    = p["orig_sig"]
        author_key  = p["author"]
        canonical   = "#{p["board"]}|#{p["post_id"]}|#{p["body"]}"
        unless Identity.verify(author_key, orig_sig, canonical.b)
          $logger&.warn("sync: assinatura do autor inválida em #{p["board"]}/#{p["post_id"]}")
          return
        end
        store_cb.call("post", p)

      when "image_chunk"
        p   = frame.payload
        key = "#{p["board"]}/#{p["post_id"]}/#{p["filename"]}"
        image_chunks[key] ||= []
        image_chunks[key][p["index"].to_i] = [p["data_hex"]].pack("H*")

      when "image_end"
        p   = frame.payload
        key = "#{p["board"]}/#{p["post_id"]}/#{p["filename"]}"
        chunks = image_chunks.delete(key) || []
        data   = chunks.join
        store_cb.call("image", p.merge("data" => data))

      when "ban_broadcast"
        # apenas admin pode emitir ban_broadcast — verifica pubkey do admin
        # (admin_pubkey configurado localmente; ignora se não bater)
        p = frame.payload
        admin_key = ENV.fetch("TXTBOARD_ADMIN_PUBKEY", "")
        if admin_key.empty? || p["pubkey"] == admin_key
          ban_list.add(p["target_pubkey"],
            reason:    p["reason"],
            banned_by: p["pubkey"])
          $logger&.info("sync: ban aplicado em #{p["target_pubkey"][0, 12]}")
        end

      when "peer_list"
        peers = frame.payload["peers"] || []
        store_cb.call("peers", peers)
      end
    end
  end
end
