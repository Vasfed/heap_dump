require "heap_dump/version"

require 'rbconfig'
require "heap_dump.#{RbConfig::CONFIG['DLEXT']}"

module HeapDump
  # Dumps ruby object space to file
  def self.dump filename='dump.json', gc_before_dump=true
    GC.start if gc_before_dump
    return dump_ext(filename)
  end
end
