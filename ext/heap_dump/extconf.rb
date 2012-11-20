#!/usr/bin/env ruby
#encoding: utf-8

# autodetect ruby headers
unless ARGV.any? {|arg| arg.include?('--with-ruby-include') }
  require 'rbconfig'
  bindir = RbConfig::CONFIG['bindir']
  if bindir =~ %r{(^.*/\.rbenv/versions)/([^/]+)/bin$}
    ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/ruby-#{$2}"
    ARGV << "--with-ruby-include=#{ruby_include}"
  elsif bindir =~ %r{(^.*/\.rvm/rubies)/([^/]+)/bin$}
    ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/#{$2}"
    ruby_include = "#{ENV['rvm_path']}/src/#{$2}" unless File.exist?(ruby_include)
    ARGV << "--with-ruby-include=#{ruby_include}"
  end
end

require 'mkmf'
require 'debugger/ruby_core_source'

def find_spec name,*requirements
  return Gem::Specification.find_by_name(name, *requirements) if Gem::Specification.respond_to? :find_by_name

  requirements = Gem::Requirement.default if requirements.empty?

  gem = Gem::Dependency.new(name, *requirements)
  matches = Gem.source_index.find_name(gem.name, gem.requirement)
  raise "No matching #{name} gem!" unless matches.any?
  matches.find { |spec|
    Gem.loaded_specs[gem.name] == spec
    } or matches.last
end

def find_gem_dir(name, *req)
  gem = find_spec(name, *req)
  return gem.gem_dir if gem.respond_to? :gem_dir
  gem.full_gem_path
end


gemspec = File.expand_path(File.join(File.dirname(__FILE__), '../../heap_dump.gemspec'))
spec = instance_eval(File.read(gemspec), gemspec).dependencies.find{|d|d.name == 'yajl-ruby'}
#$defs.push(format("-DREQUIRED_YAJL_VERSION=\\"%s\\"", spec.requirement)) #does not work in this form :(

yajl = find_gem_dir(spec.name, spec.requirement)
find_header('api/yajl_gen.h', File.join(yajl, 'ext', 'yajl'))

#TODO: inject ruby version
unless find_header("gc_internal.h", File.join(File.dirname(__FILE__),'specific', "ruby-#{RUBY_VERSION}")) && have_header("gc_internal.h")
  raise "Do not have internal structs for your ruby version"
end

hdrs = proc {
  res = %w{
    vm_core.h
    iseq.h
    node.h
    method.h
  }.all?{|hdr| have_header(hdr)}
  # atomic.h
  # constant.h

  #optional:
  %w{
    constant.h
    internal.h
    gc.h
    }.each{|h| have_header(h)}

  have_struct_member("rb_iseq_t", "filename", "vm_core.h")
  have_struct_member("rb_binding_t", "filename", "vm_core.h")
  have_struct_member("rb_control_frame_t", "bp", "vm_core.h")
  have_struct_member("rb_thread_t", "thrown_errinfo", "vm_core.h")
  have_struct_member("rb_event_hook_t", "data", "vm_core.h")
  


  have_struct_member("rb_iseq_t", "location", "vm_core.h")
  #have_struct_member("rb_iseq_location_t", "filename", "vm_core.h")
  have_struct_member("rb_block_t", "klass", "vm_core.h")
  have_struct_member("rb_block_t", "lfp", "vm_core.h")

  res
}

dir_config("ruby") # allow user to pass in non-standard core include directory

if !Debugger::RubyCoreSource::create_makefile_with_core(hdrs, "heap_dump")
  STDERR.print("Makefile creation failed\n")
  STDERR.print("*************************************************************\n\n")
  STDERR.print("  NOTE: If your headers were not found, try passing\n")
  STDERR.print("        --with-ruby-include=PATH_TO_HEADERS      \n\n")
  STDERR.print("*************************************************************\n\n")
  exit(1)
end
