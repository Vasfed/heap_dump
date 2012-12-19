#!/usr/bin/env ruby
#encoding: utf-8


$LOAD_PATH.unshift File.expand_path(File.dirname(__FILE__))

require 'parser'

require 'gc_stuff'
require 'typed_data_stuff'


class Generator

  attr_reader :parser
  attr_reader :ruby_src_dir

  def initialize custom_ruby_src=find_ruby_src
    @parser = Parser.new custom_ruby_src
    #REFACTOR: ruby_src_dir belongs here, not in parser
    @ruby_src_dir = parser.ruby_src_dir
    @known_types = Set.new

    @src_buf = []
  end

  def find_ruby_src
    bindir = RbConfig::CONFIG['bindir']
    if bindir =~ %r{(^.*/\.rbenv/versions)/([^/]+)/bin$}
      ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/ruby-#{$2}"
      ARGV << "--with-ruby-include=#{ruby_include}"
    elsif bindir =~ %r{(^.*/\.rvm/rubies)/([^/]+)/bin$}
      ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/#{$2}"
      ruby_include = "#{ENV['rvm_path']}/src/#{$2}" unless File.exist?(ruby_include)
      ARGV << "--with-ruby-include=#{ruby_include}"
    end
    ruby_include
  end

  include GCStuff
  include GeneratorTypedDataStuff


  # check if typename is available to gem
  def known_typename? typename
    typename && (@known_types.include?(typename) || non_internal_typenames.include?(typename))
  end

  # typenames that are available to gems without hacking
  def non_internal_typenames
    @non_internal_typenames ||= begin
      #TODO: generate this source from heapdump itself:
      tree = parser.parse('dummy_gem.c')

      types = tree.parser.type_names
      tree.entities.each{|n|
        #TODO: does this include typedefs to known types?
        types << n.type.name if n.is_a?(C::Declaration) && n.type.name && n.type.members
      }
      types
    end
  end

  def add_src src
    #@src_buf << src
    puts src
    puts
  end

  #REFACTOR
  def type_dependencies tree, item
    new_types = Set.new

    item.postorder{|n|
      if n.is_a?(C::DirectType) # indirect have direct on leafs
        #FIXME: clean this up!!!!:
        next unless n.name
        #FIXME: name collisions vs scopes!
        new_types << n.name unless known_typename?(n.name)
        new_types << n.name if n.name && !n.members
      end
    }

    new_types.each{|typename|
      next if known_typename?(typename)
      @known_types << typename
      # puts "Descending to #{typename}"
      type = tree.find_type typename
      unless type
        puts "// cannot find definition for #{typename}"
        next
      end
      type_dependencies tree, type
      add_src type.to_s
    }
  end


  def generate
    handle_gc_stuff
    handle_named_data_types
  end

end


g = Generator.new # "/Users/vasfed/work/heap_dump/tmp/tcs-ruby_1_9_3"

g.generate




