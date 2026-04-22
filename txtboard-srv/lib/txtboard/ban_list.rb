# txtboard/ban_list.rb
# Lê bans.ndjson produzido pelo core Rust.
# Thread-safe para leitura concorrente.

require "json"
require "monitor"

module Txtboard
  class BanList
    include MonitorMixin

    def initialize(db_dir = nil)
      super()
      @path    = File.join(db_dir || File.join(Dir.home, ".txtboard", "db"), "bans.ndjson")
      @banned  = {}
      reload
    end

    def banned?(pubkey)
      mon_synchronize { @banned.key?(pubkey) }
    end

    def reload
      mon_synchronize do
        @banned = {}
        return unless File.exist?(@path)
        File.foreach(@path) do |line|
          next if line.strip.empty?
          rec = JSON.parse(line) rescue next
          @banned[rec["pubkey"]] = rec
        end
      end
    end

    # Usado pelo admin via comando de ban (persiste no arquivo)
    def add(pubkey, reason: nil, banned_by:)
      rec = {
        "_id"       => pubkey,
        "pubkey"    => pubkey,
        "reason"    => reason,
        "banned_at" => Time.now.utc.iso8601,
        "banned_by" => banned_by
      }
      mon_synchronize do
        @banned[pubkey] = rec
        File.open(@path, "a") { |f| f.puts(rec.to_json) }
      end
    end

    def remove(pubkey)
      mon_synchronize do
        @banned.delete(pubkey)
        # reescreve sem o banido
        lines = File.exist?(@path) ? File.readlines(@path) : []
        File.open(@path, "w") do |f|
          lines.each do |l|
            rec = JSON.parse(l) rescue next
            f.puts(l) unless rec["pubkey"] == pubkey
          end
        end
      end
    end

    def all = mon_synchronize { @banned.values }
  end
end
