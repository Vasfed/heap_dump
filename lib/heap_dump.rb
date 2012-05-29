require "heap_dump/version"

# need to require ruby-internal before our extension so that these extensions are loaded and linked
require 'internal/node'
require 'yajl'

#TODO: more cross-platform require for extension
require 'heap_dump.bundle'

module HeapDump
  # Dumps ruby object space to file
  def self.dump filename='dump.json', gc_before_dump=true
    GC.start if gc_before_dump
    return dump_ext(filename)
  end
end
