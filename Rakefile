#!/usr/bin/env rake
require "bundler/gem_tasks"


require 'rake/extensiontask'
Rake::ExtensionTask.new('heap_dump')

desc "Simple dump test,just to check if extension compiles and does not segfault on simple dump"
task :test => :compile do
  require 'heap_dump'
  puts "Dumping..."
  HeapDump.dump
end

task :default => :test