#!/usr/bin/env ruby
$LOAD_PATH.unshift(File.join(__dir__, "lib"))

require "logger"
require "optparse"
require "fileutils"
require "txtboard/identity"
require "txtboard/ban_list"
require "txtboard/relay_server"

$logger = Logger.new($stdout)
$logger.formatter = proc { |sev, _, _, msg| "#{sev[0]} #{msg}\n" }
$logger.level = Logger::INFO

opts = { relay_port: 7667, host: "127.0.0.1" }
OptionParser.new do |o|
  o.on("--relay-port N", Integer) { |v| opts[:relay_port] = v }
  o.on("--host H")                { |v| opts[:host] = v }
  o.on("--debug")                 { $logger.level = Logger::DEBUG }
end.parse!

db_dir   = File.join(Dir.home, ".txtboard", "db")
identity = Txtboard::Identity.new
ban_list = Txtboard::BanList.new(db_dir)

$logger.info("nó: #{identity.pubkey_hex[0,16]}… | acesso: #{identity.access_code}")
$logger.info("relay TCP em #{opts[:host]}:#{opts[:relay_port]}")
$logger.info("db: #{db_dir}")
$logger.info("(rode sob torsocks para expor como .onion)")

Txtboard::RelayServer.new(
  host:     opts[:host],
  port:     opts[:relay_port],
  ban_list: ban_list,
  identity: identity,
  db_dir:   db_dir
).run
