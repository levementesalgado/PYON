# txtboard/protocol.rb
#
# Protocolo de wire sobre TCP (torsocks).
# Cada frame é um MessagePack array:
#
#   [ version, type, payload_hash ]
#
# version  : Integer (atualmente 1)
# type     : Symbol serializado como String
# payload  : Hash com campos do frame
#
# Todos os frames que carregam conteúdo de usuário incluem:
#   pubkey  : String hex (64 chars) — chave pública Ed25519 do autor
#   sig     : String hex (128 chars) — assinatura Ed25519 do payload canônico
#
# O receptor SEMPRE verifica a assinatura antes de processar.
# Frames de nós banidos são silenciosamente descartados.

require "msgpack"
require "ed25519"

module Txtboard
  module Protocol
    VERSION = 1

    # ── tipos de frame ────────────────────────────────────────────
    TYPES = %w[
      handshake          # apresentação inicial entre nós
      handshake_ack      # confirmação de handshake
      ping               # keepalive
      pong

      post_announce      # "tenho um post novo em /board/id"
      post_request       # "me manda o post /board/id"
      post_data          # payload completo do post

      image_announce     # "tenho imagem /board/id/filename"
      image_request
      image_chunk        # transferência em chunks (64 KB)
      image_end

      chat_message       # mensagem de relay
      chat_dm            # DM direto entre dois nós

      ban_broadcast      # admin anuncia ban de pubkey
      peer_list          # lista de peers conhecidos
    ].freeze

    # ── construção de frames ──────────────────────────────────────

    def self.build(type, payload = {})
      raise ArgumentError, "tipo desconhecido: #{type}" unless TYPES.include?(type.to_s)
      MessagePack.pack([VERSION, type.to_s, payload])
    end

    # Constrói frame assinado. identity deve responder a #sign(bytes) → hex
    def self.build_signed(type, payload, identity)
      canonical = canonical_bytes(type, payload)
      sig       = identity.sign(canonical)
      build(type, payload.merge("sig" => sig, "pubkey" => identity.pubkey_hex))
    end

    # ── parsing ───────────────────────────────────────────────────

    ParsedFrame = Struct.new(:version, :type, :payload, keyword_init: true)

    def self.parse(raw_bytes)
      arr = MessagePack.unpack(raw_bytes)
      raise ProtocolError, "frame malformado" unless arr.is_a?(Array) && arr.size == 3
      version, type, payload = arr
      raise ProtocolError, "versão incompatível: #{version}" unless version == VERSION
      raise ProtocolError, "tipo inválido: #{type}" unless TYPES.include?(type)
      ParsedFrame.new(version: version, type: type, payload: payload)
    rescue MessagePack::MalformedFormatError => e
      raise ProtocolError, "msgpack inválido: #{e.message}"
    end

    # ── verificação de assinatura ─────────────────────────────────

    def self.verify!(frame)
      p = frame.payload
      pubkey_hex = p["pubkey"] or raise ProtocolError, "sem pubkey"
      sig_hex    = p["sig"]    or raise ProtocolError, "sem sig"

      payload_without_sig = p.reject { |k, _| k == "sig" }
      canonical = canonical_bytes(frame.type, payload_without_sig)

      pk_bytes  = [pubkey_hex].pack("H*")
      sig_bytes = [sig_hex].pack("H*")

      vk  = Ed25519::VerifyKey.new(pk_bytes)
      vk.verify(sig_bytes, canonical)
    rescue Ed25519::VerifyError
      raise SignatureError, "assinatura inválida de #{p["pubkey"]&.slice(0, 12)}"
    end

    # Bytes canônicos para assinatura: "VERSION|type|key1=val1|key2=val2..." (chaves sorted)
    def self.canonical_bytes(type, payload)
      parts = payload
        .reject { |k, _| k == "sig" }
        .sort_by { |k, _| k.to_s }
        .map { |k, v| "#{k}=#{v}" }
      "#{VERSION}|#{type}|#{parts.join("|")}".b
    end

    # ── framing sobre stream TCP ──────────────────────────────────
    # Prefixo de 4 bytes (big-endian uint32) com tamanho do payload.

    def self.encode(raw_frame)
      [raw_frame.bytesize].pack("N") + raw_frame
    end

    # Lê um frame completo de um IO. Bloqueia até ter dados suficientes.
    def self.read_frame(io)
      len_bytes = io.read(4) or raise EOFError
      len = len_bytes.unpack1("N")
      raise ProtocolError, "frame absurdo: #{len} bytes" if len > 4 * 1024 * 1024
      io.read(len) or raise EOFError
    end

    # ── erros ─────────────────────────────────────────────────────
    class ProtocolError  < StandardError; end
    class SignatureError < StandardError; end
  end
end
