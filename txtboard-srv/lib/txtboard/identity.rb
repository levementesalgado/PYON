# txtboard/identity.rb
# Lê ~/.txtboard/identity.json produzido pelo txtboard-core (Rust).
# Expõe sign/verify compatíveis com protocol.rb.

require "json"
require "ed25519"

module Txtboard
  class Identity
    attr_reader :pubkey_hex, :access_code, :display_name

    def initialize(path = nil)
      path ||= File.join(Dir.home, ".txtboard", "identity.json")
      raise "identidade não encontrada em #{path} — rode txtboard-core primeiro" unless File.exist?(path)

      data = JSON.parse(File.read(path))
      @secret_hex   = data.fetch("secret_hex")
      @pubkey_hex   = data.fetch("pubkey_hex")
      @access_code  = data.fetch("access_code")
      @display_name = data["display_name"]

      secret_bytes = [@secret_hex].pack("H*")
      @signing_key = Ed25519::SigningKey.new(secret_bytes)
    end

    # Retorna assinatura hex (128 chars)
    def sign(bytes)
      @signing_key.sign(bytes).unpack1("H*")
    end

    def self.verify(pubkey_hex, sig_hex, bytes)
      pk  = Ed25519::VerifyKey.new([pubkey_hex].pack("H*"))
      sig = [sig_hex].pack("H*")
      pk.verify(sig, bytes)
      true
    rescue Ed25519::VerifyError
      false
    end

    def display
      @display_name || "anon:#{@pubkey_hex[0, 8]}"
    end

    def banned?(ban_list)
      ban_list.include?(@pubkey_hex)
    end
  end
end
